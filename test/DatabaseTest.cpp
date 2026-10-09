// Tests for the SQLite schema created by Database.
#include "Test.hpp"
#include "../include/Database.hpp"
#include <sqlite3.h>
#include <cstdio>
#include <ctime>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>
#include <unistd.h>

namespace {

// Runs a statement and reports whether SQLite accepted it.
bool tryExec(sqlite3* db, const std::string& sql) {
    return sqlite3_exec(db, sql.c_str(), nullptr, nullptr, nullptr) == SQLITE_OK;
}

void exec(sqlite3* db, const std::string& sql) {
    char* err = nullptr;
    if (sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &err) != SQLITE_OK) {
        std::string msg = err ? err : "unknown error";
        sqlite3_free(err);
        throw std::runtime_error("exec failed: " + msg + " in: " + sql);
    }
}

// Returns the first column of the first row as an integer.
long long queryInt(sqlite3* db, const std::string& sql) {
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK)
        throw std::runtime_error(std::string("prepare failed: ") + sqlite3_errmsg(db));
    long long value = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW)
        value = sqlite3_column_int64(stmt, 0);
    sqlite3_finalize(stmt);
    return value;
}

std::set<std::string> columnsOf(sqlite3* db, const std::string& table) {
    sqlite3_stmt* stmt = nullptr;
    std::string sql = "PRAGMA table_info(" + table + ");";
    sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr);
    std::set<std::string> cols;
    while (sqlite3_step(stmt) == SQLITE_ROW)
        cols.insert(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1)));
    sqlite3_finalize(stmt);
    return cols;
}

std::string tempDbPath() {
    char path[] = "/tmp/libchat_test_XXXXXX";
    int fd = mkstemp(path);
    if (fd == -1)
        throw std::runtime_error("mkstemp failed");
    close(fd);
    return path;
}

void removeDb(const std::string& path) {
    std::remove(path.c_str());
    std::remove((path + "-wal").c_str());
    std::remove((path + "-shm").c_str());
}

// Inserts one user and returns its id.
long long addUser(sqlite3* db, const std::string& name) {
    exec(db, "INSERT INTO users (username, password_hash) VALUES ('" + name + "', 'hash');");
    return sqlite3_last_insert_rowid(db);
}

long long lobbyId(sqlite3* db) {
    return queryInt(db, "SELECT id FROM rooms WHERE name = 'lobby';");
}

} // namespace

// ── Tests ───────────────────────────────────────────────────────────

TEST(db_tables_have_expected_columns) {
    Database d(":memory:");
    sqlite3* db = d.handle();
    CHECK((columnsOf(db, "users") ==
           std::set<std::string>{"id", "username", "password_hash", "is_admin", "created_at"}));
    CHECK((columnsOf(db, "sessions") ==
           std::set<std::string>{"token_hash", "user_id", "expires_at"}));
    CHECK((columnsOf(db, "messages") ==
           std::set<std::string>{"id", "room_id", "user_id", "sender", "body", "created_at"}));
    CHECK((columnsOf(db, "rooms") == std::set<std::string>{"id", "name", "created_at"}));
}

TEST(db_schema_version_is_set) {
    Database d(":memory:");
    CHECK(queryInt(d.handle(), "PRAGMA user_version;") == 3);
}

TEST(db_foreign_keys_enabled) {
    Database d(":memory:");
    CHECK(queryInt(d.handle(), "PRAGMA foreign_keys;") == 1);
}

TEST(db_lobby_room_seeded) {
    Database d(":memory:");
    CHECK(queryInt(d.handle(), "SELECT COUNT(*) FROM rooms WHERE name = 'lobby';") == 1);
}

TEST(db_reopen_is_idempotent_and_keeps_data) {
    std::string path = tempDbPath();
    try {
        {
            Database d(path);
            addUser(d.handle(), "alice");
        }
        Database d(path);
        sqlite3* db = d.handle();
        CHECK(queryInt(db, "PRAGMA user_version;") == 3);
        CHECK(queryInt(db, "SELECT COUNT(*) FROM rooms WHERE name = 'lobby';") == 1);
        CHECK(queryInt(db, "SELECT COUNT(*) FROM users WHERE username = 'alice';") == 1);
    } catch (...) {
        removeDb(path);
        throw;
    }
    removeDb(path);
}

TEST(db_user_defaults) {
    Database d(":memory:");
    sqlite3* db = d.handle();
    long long before = std::time(nullptr);
    long long id = addUser(db, "alice");
    long long after = std::time(nullptr);
    CHECK(queryInt(db, "SELECT is_admin FROM users WHERE id = " + std::to_string(id)) == 0);
    long long created = queryInt(db, "SELECT created_at FROM users WHERE id = " + std::to_string(id));
    CHECK(created >= before && created <= after);
}

TEST(db_username_unique_case_insensitive) {
    Database d(":memory:");
    sqlite3* db = d.handle();
    addUser(db, "alice");
    CHECK(!tryExec(db, "INSERT INTO users (username, password_hash) VALUES ('alice', 'h');"));
    CHECK(!tryExec(db, "INSERT INTO users (username, password_hash) VALUES ('ALICE', 'h');"));
}

TEST(db_is_admin_must_be_boolean) {
    Database d(":memory:");
    sqlite3* db = d.handle();
    CHECK(tryExec(db, "INSERT INTO users (username, password_hash, is_admin) VALUES ('a', 'h', 1);"));
    CHECK(!tryExec(db, "INSERT INTO users (username, password_hash, is_admin) VALUES ('b', 'h', 2);"));
}

TEST(db_required_columns_reject_null) {
    Database d(":memory:");
    sqlite3* db = d.handle();
    CHECK(!tryExec(db, "INSERT INTO users (username, password_hash) VALUES (NULL, 'h');"));
    CHECK(!tryExec(db, "INSERT INTO users (username, password_hash) VALUES ('a', NULL);"));
    CHECK(!tryExec(db, "INSERT INTO rooms (name) VALUES (NULL);"));
    long long uid = addUser(db, "alice");
    std::string lobby = std::to_string(lobbyId(db));
    CHECK(!tryExec(db, "INSERT INTO messages (room_id, sender, body) VALUES (" + lobby + ", 'a', NULL);"));
    CHECK(!tryExec(db, "INSERT INTO messages (room_id, sender, body) VALUES (" + lobby + ", NULL, 'hi');"));
    CHECK(!tryExec(db, "INSERT INTO messages (room_id, sender, body) VALUES (NULL, 'a', 'hi');"));
    CHECK(!tryExec(db, "INSERT INTO sessions (token_hash, user_id, expires_at) VALUES ('t', " +
                           std::to_string(uid) + ", NULL);"));
}

TEST(db_room_name_unique) {
    Database d(":memory:");
    sqlite3* db = d.handle();
    CHECK(tryExec(db, "INSERT INTO rooms (name) VALUES ('general');"));
    CHECK(!tryExec(db, "INSERT INTO rooms (name) VALUES ('general');"));
}

TEST(db_session_requires_existing_user) {
    Database d(":memory:");
    CHECK(!tryExec(d.handle(), "INSERT INTO sessions VALUES ('t', 999, 0);"));
}

TEST(db_session_token_unique) {
    Database d(":memory:");
    sqlite3* db = d.handle();
    std::string uid = std::to_string(addUser(db, "alice"));
    CHECK(tryExec(db, "INSERT INTO sessions VALUES ('t', " + uid + ", 0);"));
    CHECK(!tryExec(db, "INSERT INTO sessions VALUES ('t', " + uid + ", 0);"));
}

TEST(db_message_requires_existing_room_and_user) {
    Database d(":memory:");
    sqlite3* db = d.handle();
    std::string uid = std::to_string(addUser(db, "alice"));
    std::string lobby = std::to_string(lobbyId(db));
    CHECK(!tryExec(db, "INSERT INTO messages (room_id, user_id, sender, body) VALUES (999, " + uid + ", 'alice', 'hi');"));
    CHECK(!tryExec(db, "INSERT INTO messages (room_id, user_id, sender, body) VALUES (" + lobby + ", 999, 'alice', 'hi');"));
    CHECK(tryExec(db, "INSERT INTO messages (room_id, user_id, sender, body) VALUES (" + lobby + ", " + uid + ", 'alice', 'hi');"));
}

TEST(db_guest_messages_need_no_user) {
    Database d(":memory:");
    sqlite3* db = d.handle();
    std::string lobby = std::to_string(lobbyId(db));
    CHECK(tryExec(db, "INSERT INTO messages (room_id, sender, body) VALUES (" + lobby + ", 'guest', 'hi');"));
    CHECK(queryInt(db, "SELECT COUNT(*) FROM messages WHERE user_id IS NULL") == 1);
}

TEST(db_deleting_user_removes_sessions_and_keeps_messages) {
    Database d(":memory:");
    sqlite3* db = d.handle();
    std::string alice = std::to_string(addUser(db, "alice"));
    std::string bob = std::to_string(addUser(db, "bob"));
    std::string lobby = std::to_string(lobbyId(db));
    exec(db, "INSERT INTO sessions VALUES ('a', " + alice + ", 0), ('b', " + bob + ", 0);");
    exec(db, "INSERT INTO messages (room_id, user_id, sender, body) VALUES (" + lobby + ", " + alice +
                 ", 'alice', 'hi'), (" + lobby + ", " + bob + ", 'bob', 'yo');");

    exec(db, "DELETE FROM users WHERE id = " + alice + ";");

    CHECK(queryInt(db, "SELECT COUNT(*) FROM sessions WHERE user_id = " + alice) == 0);
    // Her messages stay in the room history, no longer linked to an account.
    CHECK(queryInt(db, "SELECT COUNT(*) FROM messages WHERE user_id = " + alice) == 0);
    CHECK(queryInt(db, "SELECT COUNT(*) FROM messages WHERE sender = 'alice' AND user_id IS NULL") == 1);
    // Other users' rows are untouched.
    CHECK(queryInt(db, "SELECT COUNT(*) FROM sessions WHERE user_id = " + bob) == 1);
    CHECK(queryInt(db, "SELECT COUNT(*) FROM messages WHERE user_id = " + bob) == 1);
}

TEST(db_deleting_room_cascades_messages) {
    Database d(":memory:");
    sqlite3* db = d.handle();
    std::string uid = std::to_string(addUser(db, "alice"));
    exec(db, "INSERT INTO rooms (name) VALUES ('general');");
    std::string general = std::to_string(sqlite3_last_insert_rowid(db));
    std::string lobby = std::to_string(lobbyId(db));
    exec(db, "INSERT INTO messages (room_id, user_id, sender, body) VALUES (" + general + ", " + uid +
                 ", 'alice', 'hi'), (" + lobby + ", " + uid + ", 'alice', 'yo');");

    exec(db, "DELETE FROM rooms WHERE id = " + general + ";");

    CHECK(queryInt(db, "SELECT COUNT(*) FROM messages WHERE room_id = " + general) == 0);
    CHECK(queryInt(db, "SELECT COUNT(*) FROM messages WHERE room_id = " + lobby) == 1);
}

TEST(db_expired_sessions_can_be_queried) {
    Database d(":memory:");
    sqlite3* db = d.handle();
    std::string uid = std::to_string(addUser(db, "alice"));
    long long now = std::time(nullptr);
    exec(db, "INSERT INTO sessions VALUES ('old', " + uid + ", " + std::to_string(now - 60) +
                 "), ('new', " + uid + ", " + std::to_string(now + 3600) + ");");
    CHECK(queryInt(db, "SELECT COUNT(*) FROM sessions WHERE expires_at < strftime('%s', 'now');") == 1);
    exec(db, "DELETE FROM sessions WHERE expires_at < strftime('%s', 'now');");
    CHECK(queryInt(db, "SELECT COUNT(*) FROM sessions WHERE token_hash = 'new';") == 1);
}

TEST(db_open_invalid_path_throws) {
    bool threw = false;
    try {
        Database d("/nonexistent_dir/libchat.db");
    } catch (const std::runtime_error&) {
        threw = true;
    }
    CHECK(threw);
}

TEST(db_migrates_v1_database_to_current) {
    std::string path = tempDbPath();
    try {
        // Build a database exactly as schema v1 left it, with one message.
        sqlite3* raw = nullptr;
        sqlite3_open(path.c_str(), &raw);
        exec(raw, R"sql(
            CREATE TABLE users (id INTEGER PRIMARY KEY AUTOINCREMENT,
                username TEXT NOT NULL UNIQUE COLLATE NOCASE, password_hash TEXT NOT NULL,
                is_admin INTEGER NOT NULL DEFAULT 0 CHECK (is_admin IN (0, 1)),
                created_at INTEGER NOT NULL DEFAULT (strftime('%s', 'now')));
            CREATE TABLE sessions (token_hash TEXT PRIMARY KEY,
                user_id INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,
                expires_at INTEGER NOT NULL) WITHOUT ROWID;
            CREATE TABLE rooms (id INTEGER PRIMARY KEY AUTOINCREMENT, name TEXT NOT NULL UNIQUE,
                created_at INTEGER NOT NULL DEFAULT (strftime('%s', 'now')));
            CREATE TABLE messages (id INTEGER PRIMARY KEY AUTOINCREMENT,
                room_id INTEGER NOT NULL REFERENCES rooms(id) ON DELETE CASCADE,
                user_id INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,
                body TEXT NOT NULL, created_at INTEGER NOT NULL DEFAULT (strftime('%s', 'now')));
            INSERT INTO rooms (name) VALUES ('lobby');
            INSERT INTO users (username, password_hash) VALUES ('alice', 'h');
            INSERT INTO messages (room_id, user_id, body, created_at) VALUES (1, 1, 'old hello', 1700000000);
            PRAGMA user_version = 1;
        )sql");
        sqlite3_close(raw);

        Database d(path);
        sqlite3* db = d.handle();
        CHECK(queryInt(db, "PRAGMA user_version;") == 3);
        CHECK(queryInt(db, "SELECT COUNT(*) FROM messages WHERE sender = 'alice' AND body = 'old hello' "
                           "AND user_id = 1 AND created_at = 1700000000") == 1);
        CHECK(tryExec(db, "INSERT INTO messages (room_id, sender, body) VALUES (1, 'guest', 'hi');"));
    } catch (...) {
        removeDb(path);
        throw;
    }
    removeDb(path);
}

TEST(db_new_database_uses_incremental_vacuum_and_age_index) {
    std::string path = tempDbPath();
    try {
        Database d(path);
        sqlite3* db = d.handle();
        CHECK(queryInt(db, "PRAGMA auto_vacuum;") == 2);
        CHECK(queryInt(db, "SELECT COUNT(*) FROM sqlite_master WHERE type = 'index' "
                           "AND name = 'idx_messages_created_at'") == 1);
    } catch (...) {
        removeDb(path);
        throw;
    }
    removeDb(path);
}
