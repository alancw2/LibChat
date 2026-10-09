#ifndef DATABASE_HPP
#define DATABASE_HPP

#include <string>

struct sqlite3;

// Owns a SQLite connection and makes sure the schema exists on open.
class Database {
public:
    explicit Database(const std::string& path);
    ~Database();

    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;

    sqlite3* handle() const;

private:
    void exec(const char* sql);
    void migrate();
    void enableIncrementalVacuum();

    sqlite3* db = nullptr;
};

#endif
