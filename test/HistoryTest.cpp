// Tests for per-room chat history (/history).
#include "Test.hpp"
#include "ServerHarness.hpp"
#include "../include/ChatHistory.hpp"
#include "../include/Database.hpp"
#include "../include/Protocol.hpp"
#include "../include/Server.hpp"
#include <chrono>
#include <cstdio>
#include <ctime>
#include <sqlite3.h>
#include <stdexcept>
#include <thread>
#include <unistd.h>

using Harness::Client;

namespace {
// The server's detached threads use these until the test process exits, so
// they are deliberately never freed.
ChatHistory* newHistory() {
    auto* db = new Database(":memory:");
    return new ChatHistory(*db);
}

int startServerWithHistory(ChatHistory* history) {
    ServerOptions options;
    options.history = history;
    return Harness::startServer(options);
}
}

// ── ChatHistory ──────────────────────────────────────────────────────

TEST(history_returns_messages_oldest_first) {
    Database db(":memory:");
    ChatHistory history(db);
    CHECK(history.record("lobby", "alice", "first"));
    CHECK(history.record("lobby", "bob", "second"));
    bool more = true;
    auto entries = history.recent("lobby", 10, &more);
    CHECK(entries.size() == 2);
    CHECK(entries[0].sender == "alice" && entries[0].body == "first");
    CHECK(entries[1].sender == "bob" && entries[1].body == "second");
    CHECK(!more);
    long long now = std::time(nullptr);
    CHECK(entries[0].createdAt >= now - 5 && entries[0].createdAt <= now);
}

TEST(history_is_separate_per_room) {
    Database db(":memory:");
    ChatHistory history(db);
    CHECK(history.record("lobby", "alice", "in lobby"));
    CHECK(history.record("dev", "bob", "in dev"));
    auto lobby = history.recent("lobby", 10);
    auto dev = history.recent("dev", 10);
    CHECK(lobby.size() == 1 && lobby[0].body == "in lobby");
    CHECK(dev.size() == 1 && dev[0].body == "in dev");
    CHECK(history.recent("empty", 10).empty());
}

TEST(history_limit_keeps_newest_and_reports_more) {
    Database db(":memory:");
    ChatHistory history(db);
    for (int i = 0; i < 5; ++i)
        CHECK(history.record("lobby", "alice", "m" + std::to_string(i)));
    bool more = false;
    auto entries = history.recent("lobby", 3, &more);
    CHECK(entries.size() == 3);
    CHECK(entries[0].body == "m2" && entries[2].body == "m4");
    CHECK(more);
}

TEST(history_format_matches_protocol) {
    std::vector<HistoryEntry> entries = {{1700000000, "alice", "hi: there"}};
    CHECK(Protocol::formatHistory("my room", entries, true) ==
          "~history-begin 1 my room\n"
          "~history 1700000000 alice: hi: there\n"
          "~history-end my room\n");
    CHECK(Protocol::formatHistory("lobby", {}, false) == "~history-begin 0 lobby\n~history-end lobby\n");
}

// ── Server ───────────────────────────────────────────────────────────

TEST(history_command_returns_current_rooms_messages) {
    Harness::isolated([] {
        int port = startServerWithHistory(newHistory());
        {
            Client alice(port), bob(port);
            CHECK(alice.nick("alice"));
            CHECK(bob.nick("bob"));
            alice.send("hello lobby\n");
            CHECK(bob.waitFor("alice: hello lobby\n"));
            bob.send("/join dev\nhello dev\n");
            CHECK(bob.waitFor("bob: hello dev\n"));
        }

        // A newcomer sees each room's own history, and only that room's.
        Client carol(port);
        carol.send("/history\n");
        CHECK(carol.waitFor("~history-end lobby\n"));
        CHECK(carol.received.find("~history-begin 0 lobby\n") != std::string::npos);
        CHECK(carol.received.find(" alice: hello lobby\n") != std::string::npos);
        CHECK(carol.received.find("hello dev") == std::string::npos);

        carol.received.clear();
        carol.send("/join dev\n/history\n");
        CHECK(carol.waitFor("~history-end dev\n"));
        CHECK(carol.received.find(" bob: hello dev\n") != std::string::npos);
        CHECK(carol.received.find("hello lobby") == std::string::npos);
    });
}

TEST(history_without_database_is_empty) {
    Harness::isolated([] {
        int port = Harness::startServer();
        Client a(port);
        a.send("hi\n/history\n");
        CHECK(a.waitFor("~history-begin 0 lobby\n~history-end lobby\n"));
    });
}

TEST(history_does_not_include_commands_or_system_messages) {
    Harness::isolated([] {
        int port = startServerWithHistory(newHistory());
        Client a(port);
        CHECK(a.nick("alice"));
        a.send("/who\n/rooms\n/join dev\n/join lobby\nreal message\n");
        CHECK(a.waitFor("alice: real message\n"));
        a.received.clear();
        a.send("/history\n");
        CHECK(a.waitFor("~history-end lobby\n"));
        auto begin = a.received.find("~history-begin");
        std::string reply = a.received.substr(begin);
        CHECK(reply.find("real message") != std::string::npos);
        CHECK(reply.find("/who") == std::string::npos);
        CHECK(reply.find("has entered") == std::string::npos);
        CHECK(reply.find("changed their nickname") == std::string::npos);
    });
}

TEST(history_survives_server_restart_with_same_database) {
    Harness::isolated([] {
        auto* history = newHistory();
        int first = startServerWithHistory(history);
        {
            Client a(first);
            CHECK(a.nick("alice"));
            a.send("remember me\n");
            CHECK(a.waitFor("alice: remember me\n"));
        }
        int second = startServerWithHistory(history);
        Client b(second);
        b.send("/history\n");
        CHECK(b.waitFor(" alice: remember me\n"));
    });
}

// ── 24-hour retention ────────────────────────────────────────────────

namespace {
long long now() { return std::time(nullptr); }

// Runs SQL on its own connection, so tests never share the server's connection
// across threads. Returns the first column of the first row (0 if none).
long long sqlOnFile(const std::string& path, const std::string& sql) {
    sqlite3* db = nullptr;
    sqlite3_open(path.c_str(), &db);
    sqlite3_busy_timeout(db, 2000);
    sqlite3_stmt* stmt = nullptr;
    long long value = 0;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK &&
        sqlite3_step(stmt) == SQLITE_ROW)
        value = sqlite3_column_int64(stmt, 0);
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return value;
}

void execOn(Database& db, const std::string& sql) {
    char* err = nullptr;
    if (sqlite3_exec(db.handle(), sql.c_str(), nullptr, nullptr, &err) != SQLITE_OK) {
        std::string msg = err ? err : "unknown";
        sqlite3_free(err);
        throw std::runtime_error("sql failed: " + msg);
    }
}

// Inserts a message `ageSeconds` old into `room` (which must exist).
void addAgedMessage(Database& db, const std::string& room, const std::string& body, long long ageSeconds) {
    execOn(db, "INSERT INTO messages (room_id, sender, body, created_at) "
               "SELECT id, 'old', '" + body + "', " + std::to_string(now() - ageSeconds) +
               " FROM rooms WHERE name = '" + room + "';");
}

std::string tempPath() {
    char path[] = "/tmp/libchat_history_XXXXXX";
    close(mkstemp(path));
    return path;
}

void removeFile(const std::string& path) {
    for (const char* suffix : {"", "-wal", "-shm"})
        std::remove((path + suffix).c_str());
}

constexpr long long DAY = 24 * 60 * 60;
}

TEST(history_hides_messages_older_than_24_hours) {
    Database db(":memory:");
    ChatHistory history(db);
    addAgedMessage(db, "lobby", "two days old", 2 * DAY);
    addAgedMessage(db, "lobby", "just expired", DAY + 5);
    addAgedMessage(db, "lobby", "almost a day old", DAY - 60);
    bool more = true;
    auto entries = history.recent("lobby", 10, &more);
    CHECK(entries.size() == 1);
    CHECK(entries[0].body == "almost a day old");
    CHECK(!more);
}

TEST(history_prune_deletes_only_expired_messages) {
    Database db(":memory:");
    ChatHistory history(db);
    addAgedMessage(db, "lobby", "expired", DAY + 5);
    addAgedMessage(db, "lobby", "expired too", 3 * DAY);
    addAgedMessage(db, "lobby", "fresh", 60);
    CHECK(history.record("lobby", "alice", "brand new"));

    CHECK(history.pruneExpired() == 2);
    auto entries = history.recent("lobby", 10);
    CHECK(entries.size() == 2);
    CHECK(entries[0].body == "fresh" && entries[1].body == "brand new");
    CHECK(history.pruneExpired() == 0);
}

TEST(history_custom_retention_period) {
    Database db(":memory:");
    ChatHistory history(db, 60);
    addAgedMessage(db, "lobby", "two minutes old", 120);
    addAgedMessage(db, "lobby", "thirty seconds old", 30);
    CHECK(history.recent("lobby", 10).size() == 1);
    CHECK(history.pruneExpired() == 1);
}

TEST(history_prune_removes_rooms_unused_for_24_hours) {
    Database db(":memory:");
    ChatHistory history(db);
    std::string old = std::to_string(now() - 2 * DAY);
    execOn(db, "UPDATE rooms SET created_at = " + old + " WHERE name = 'lobby';");
    execOn(db, "INSERT INTO rooms (name, created_at) VALUES "
               "('stale', " + old + "), ('was busy', " + old + "), ('still busy', " + old + ");");
    addAgedMessage(db, "was busy", "last message ages ago", DAY + 60);
    addAgedMessage(db, "still busy", "recent", 60);
    CHECK(history.addRoom("fresh"));

    history.pruneExpired();

    auto stored = [&](const std::string& room) {
        sqlite3_stmt* stmt = nullptr;
        sqlite3_prepare_v2(db.handle(), "SELECT COUNT(*) FROM rooms WHERE name = ?;", -1, &stmt, nullptr);
        sqlite3_bind_text(stmt, 1, room.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(stmt);
        bool found = sqlite3_column_int(stmt, 0) == 1;
        sqlite3_finalize(stmt);
        return found;
    };
    CHECK(!stored("stale"));
    CHECK(!stored("was busy"));
    CHECK(stored("still busy"));
    CHECK(stored("fresh"));
    CHECK(stored("lobby"));
}

TEST(history_prune_gives_space_back) {
    std::string path = tempPath();
    try {
        Database db(path);
        ChatHistory history(db);
        std::string body(1000, 'x');
        execOn(db, "BEGIN;");
        for (int i = 0; i < 2000; ++i)
            addAgedMessage(db, "lobby", body, 2 * DAY);
        execOn(db, "COMMIT;");
        long long pagesBefore = sqlOnFile(path, "PRAGMA page_count;");

        CHECK(history.pruneExpired() == 2000);
        CHECK(sqlOnFile(path, "PRAGMA freelist_count;") == 0);
        CHECK(sqlOnFile(path, "PRAGMA page_count;") < pagesBefore / 4);
    } catch (...) {
        removeFile(path);
        throw;
    }
    removeFile(path);
}

TEST(history_join_stores_room) {
    Harness::isolated([] {
        std::string path = tempPath();
        auto* db = new Database(path);
        int port = startServerWithHistory(new ChatHistory(*db));
        Client a(port);
        CHECK(a.nick("alice"));
        a.send("/join quiet room\n");
        CHECK(a.waitFor("alice has entered room quiet room\n"));
        bool stored = false;
        for (int i = 0; i < 50 && !stored; ++i) {
            stored = sqlOnFile(path, "SELECT COUNT(*) FROM rooms WHERE name = 'quiet room';") == 1;
            if (!stored)
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        removeFile(path);
        CHECK(stored);
    });
}

TEST(history_server_deletes_expired_messages_by_itself) {
    Harness::isolated([] {
        std::string path = tempPath();
        auto* db = new Database(path);
        addAgedMessage(*db, "lobby", "expired", DAY + 5);
        addAgedMessage(*db, "lobby", "fresh", 60);
        startServerWithHistory(new ChatHistory(*db));

        long long remaining = -1;
        for (int i = 0; i < 100; ++i) {
            remaining = sqlOnFile(path, "SELECT COUNT(*) FROM messages;");
            if (remaining == 1)
                break;
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        long long fresh = sqlOnFile(path, "SELECT COUNT(*) FROM messages WHERE body = 'fresh';");
        removeFile(path);
        CHECK(remaining == 1);
        CHECK(fresh == 1);
    });
}
