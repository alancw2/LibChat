#include "../include/Database.hpp"
#include <sqlite3.h>
#include <stdexcept>
#include <string>

namespace {
// Timestamps are stored as INTEGER unix seconds so expires_at can be
// compared directly against strftime('%s','now').
constexpr const char* SCHEMA_V1 = R"sql(
CREATE TABLE IF NOT EXISTS users (
    id            INTEGER PRIMARY KEY AUTOINCREMENT,
    username      TEXT    NOT NULL UNIQUE COLLATE NOCASE,
    password_hash TEXT    NOT NULL,
    is_admin      INTEGER NOT NULL DEFAULT 0 CHECK (is_admin IN (0, 1)),
    created_at    INTEGER NOT NULL DEFAULT (strftime('%s', 'now'))
);

CREATE TABLE IF NOT EXISTS sessions (
    token_hash TEXT    PRIMARY KEY,
    user_id    INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    expires_at INTEGER NOT NULL
) WITHOUT ROWID;

CREATE INDEX IF NOT EXISTS idx_sessions_user_id    ON sessions(user_id);
CREATE INDEX IF NOT EXISTS idx_sessions_expires_at ON sessions(expires_at);

CREATE TABLE IF NOT EXISTS rooms (
    id         INTEGER PRIMARY KEY AUTOINCREMENT,
    name       TEXT    NOT NULL UNIQUE,
    created_at INTEGER NOT NULL DEFAULT (strftime('%s', 'now'))
);

CREATE TABLE IF NOT EXISTS messages (
    id         INTEGER PRIMARY KEY AUTOINCREMENT,
    room_id    INTEGER NOT NULL REFERENCES rooms(id) ON DELETE CASCADE,
    user_id    INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    body       TEXT    NOT NULL,
    created_at INTEGER NOT NULL DEFAULT (strftime('%s', 'now'))
);

CREATE INDEX IF NOT EXISTS idx_messages_room_created ON messages(room_id, created_at);
CREATE INDEX IF NOT EXISTS idx_messages_user_id      ON messages(user_id);

-- Every connection starts in "lobby", so it must always exist.
INSERT OR IGNORE INTO rooms (name) VALUES ('lobby');
)sql";

// v2: people can chat as guests (no account), so each message stores the
// sender's display name and user_id becomes optional. Deleting a user keeps
// their messages in the room history. SQLite can't change a column's
// constraints in place, so the table is rebuilt.
constexpr const char* SCHEMA_V2 = R"sql(
CREATE TABLE messages_v2 (
    id         INTEGER PRIMARY KEY AUTOINCREMENT,
    room_id    INTEGER NOT NULL REFERENCES rooms(id) ON DELETE CASCADE,
    user_id    INTEGER REFERENCES users(id) ON DELETE SET NULL,
    sender     TEXT    NOT NULL,
    body       TEXT    NOT NULL,
    created_at INTEGER NOT NULL DEFAULT (strftime('%s', 'now'))
);

INSERT INTO messages_v2 (id, room_id, user_id, sender, body, created_at)
    SELECT m.id, m.room_id, m.user_id, u.username, m.body, m.created_at
    FROM messages m JOIN users u ON u.id = m.user_id;

DROP TABLE messages;
ALTER TABLE messages_v2 RENAME TO messages;

CREATE INDEX idx_messages_room_created ON messages(room_id, created_at);
CREATE INDEX idx_messages_user_id      ON messages(user_id);
)sql";

// v3: expired messages are deleted by age (ChatHistory::pruneExpired), which
// needs an index on created_at to stay fast.
constexpr const char* SCHEMA_V3 = R"sql(
CREATE INDEX IF NOT EXISTS idx_messages_created_at ON messages(created_at);
)sql";

// Applies one schema step and records the new version, all or nothing.
void applyStep(sqlite3* db, const char* sql, int version);

int pragmaInt(sqlite3* db, const char* sql) {
    sqlite3_stmt* stmt = nullptr;
    int value = 0;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK && sqlite3_step(stmt) == SQLITE_ROW)
        value = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);
    return value;
}
}

Database::Database(const std::string& path) {
    if (sqlite3_open(path.c_str(), &db) != SQLITE_OK) {
        std::string err = db ? sqlite3_errmsg(db) : "out of memory";
        sqlite3_close(db);
        db = nullptr;
        throw std::runtime_error("failed to open database: " + err);
    }
    // Foreign keys are off by default in SQLite and must be enabled per connection.
    exec("PRAGMA foreign_keys = ON;");
    exec("PRAGMA journal_mode = WAL;");
    sqlite3_busy_timeout(db, 5000);
    enableIncrementalVacuum();
    migrate();
}

Database::~Database() {
    sqlite3_close(db);
}

sqlite3* Database::handle() const {
    return db;
}

void Database::exec(const char* sql) {
    char* errmsg = nullptr;
    if (sqlite3_exec(db, sql, nullptr, nullptr, &errmsg) != SQLITE_OK) {
        std::string err = errmsg ? errmsg : "unknown error";
        sqlite3_free(errmsg);
        throw std::runtime_error("sqlite error: " + err);
    }
}

// Uses PRAGMA user_version to track which schema version has been applied.
void Database::migrate() {
    int version = pragmaInt(db, "PRAGMA user_version;");
    if (version < 1)
        applyStep(db, SCHEMA_V1, 1);
    if (version < 2)
        applyStep(db, SCHEMA_V2, 2);
    if (version < 3)
        applyStep(db, SCHEMA_V3, 3);
}

// Deleting rows normally leaves the file the same size (SQLite reuses the space
// later). Incremental auto-vacuum lets ChatHistory::pruneExpired hand freed
// space back, so a busy day doesn't leave a large file behind for good.
// An existing file only switches modes after a one-time VACUUM.
void Database::enableIncrementalVacuum() {
    const int INCREMENTAL = 2;
    if (pragmaInt(db, "PRAGMA auto_vacuum;") == INCREMENTAL)
        return;
    exec("PRAGMA auto_vacuum = INCREMENTAL;");
    exec("VACUUM;");
}

namespace {
void applyStep(sqlite3* db, const char* sql, int version) {
    std::string script = std::string("BEGIN;\n") + sql +
                         "\nPRAGMA user_version = " + std::to_string(version) + ";\nCOMMIT;";
    char* errmsg = nullptr;
    if (sqlite3_exec(db, script.c_str(), nullptr, nullptr, &errmsg) != SQLITE_OK) {
        std::string err = errmsg ? errmsg : "unknown error";
        sqlite3_free(errmsg);
        sqlite3_exec(db, "ROLLBACK;", nullptr, nullptr, nullptr);
        throw std::runtime_error("schema migration to v" + std::to_string(version) + " failed: " + err);
    }
}
}
