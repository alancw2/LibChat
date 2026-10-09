#include "../include/ChatHistory.hpp"
#include "../include/Database.hpp"
#include <algorithm>
#include <sqlite3.h>

namespace {
// Finalizes a prepared statement when it goes out of scope.
struct Statement {
    sqlite3_stmt* stmt = nullptr;
    Statement(sqlite3* db, const char* sql) { sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr); }
    ~Statement() { sqlite3_finalize(stmt); }
    explicit operator bool() const { return stmt != nullptr; }
};

void bindText(sqlite3_stmt* stmt, int index, const std::string& text) {
    sqlite3_bind_text(stmt, index, text.data(), static_cast<int>(text.size()), SQLITE_TRANSIENT);
}

std::string columnText(sqlite3_stmt* stmt, int index) {
    auto* text = reinterpret_cast<const char*>(sqlite3_column_text(stmt, index));
    return text ? std::string(text, sqlite3_column_bytes(stmt, index)) : std::string();
}
}

ChatHistory::ChatHistory(Database& db, std::int64_t retentionSeconds)
    : db(db), retention(retentionSeconds) {}

bool ChatHistory::addRoom(const std::string& room) {
    std::scoped_lock lock(mutex);
    Statement insert(db.handle(), "INSERT OR IGNORE INTO rooms (name) VALUES (?);");
    if (!insert)
        return false;
    bindText(insert.stmt, 1, room);
    return sqlite3_step(insert.stmt) == SQLITE_DONE;
}

bool ChatHistory::record(const std::string& room, const std::string& sender, const std::string& body) {
    std::scoped_lock lock(mutex);
    sqlite3* handle = db.handle();

    Statement createRoom(handle, "INSERT OR IGNORE INTO rooms (name) VALUES (?);");
    if (!createRoom)
        return false;
    bindText(createRoom.stmt, 1, room);
    if (sqlite3_step(createRoom.stmt) != SQLITE_DONE)
        return false;

    Statement insert(handle,
        "INSERT INTO messages (room_id, sender, body) "
        "SELECT id, ?, ? FROM rooms WHERE name = ?;");
    if (!insert)
        return false;
    bindText(insert.stmt, 1, sender);
    bindText(insert.stmt, 2, body);
    bindText(insert.stmt, 3, room);
    return sqlite3_step(insert.stmt) == SQLITE_DONE;
}

std::vector<HistoryEntry> ChatHistory::recent(const std::string& room, int limit, bool* hasMore) {
    std::scoped_lock lock(mutex);
    std::vector<HistoryEntry> entries;
    if (hasMore)
        *hasMore = false;

    // Fetch one extra row to learn whether older messages exist. Ordering by id
    // keeps messages sent within the same second in the order they were sent.
    Statement query(db.handle(),
        "SELECT m.created_at, m.sender, m.body FROM messages m "
        "JOIN rooms r ON r.id = m.room_id "
        "WHERE r.name = ? AND m.created_at >= CAST(strftime('%s', 'now') AS INTEGER) - ? "
        "ORDER BY m.id DESC LIMIT ?;");
    if (!query)
        return entries;
    bindText(query.stmt, 1, room);
    sqlite3_bind_int64(query.stmt, 2, retention);
    sqlite3_bind_int(query.stmt, 3, limit + 1);

    while (sqlite3_step(query.stmt) == SQLITE_ROW) {
        entries.push_back({sqlite3_column_int64(query.stmt, 0),
                           columnText(query.stmt, 1),
                           columnText(query.stmt, 2)});
    }
    if (static_cast<int>(entries.size()) > limit) {
        entries.pop_back();
        if (hasMore)
            *hasMore = true;
    }
    std::reverse(entries.begin(), entries.end());
    return entries;
}

int ChatHistory::pruneExpired() {
    std::scoped_lock lock(mutex);
    sqlite3* handle = db.handle();
    int deleted = 0;
    int deletedRooms = 0;
    {
        Statement messages(handle,
            "DELETE FROM messages WHERE created_at < CAST(strftime('%s', 'now') AS INTEGER) - ?;");
        if (!messages)
            return -1;
        sqlite3_bind_int64(messages.stmt, 1, retention);
        if (sqlite3_step(messages.stmt) != SQLITE_DONE)
            return -1;
        deleted = sqlite3_changes(handle);
    }
    {
        // A room disappears once it has gone a full retention period without
        // messages. Someone can still be in it; it's stored again on their next
        // message or /join.
        Statement rooms(handle,
            "DELETE FROM rooms WHERE name <> 'lobby' "
            "AND created_at < CAST(strftime('%s', 'now') AS INTEGER) - ? "
            "AND NOT EXISTS (SELECT 1 FROM messages m WHERE m.room_id = rooms.id);");
        if (!rooms)
            return -1;
        sqlite3_bind_int64(rooms.stmt, 1, retention);
        if (sqlite3_step(rooms.stmt) != SQLITE_DONE)
            return -1;
        deletedRooms = sqlite3_changes(handle);
    }
    // Statements are finalized above, so nothing blocks giving the space back.
    if (deleted > 0 || deletedRooms > 0)
        sqlite3_exec(handle, "PRAGMA incremental_vacuum;", nullptr, nullptr, nullptr);
    return deleted;
}
