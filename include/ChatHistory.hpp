#ifndef CHATHISTORY_HPP
#define CHATHISTORY_HPP

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

class Database;

struct HistoryEntry {
    std::int64_t createdAt;  // unix seconds
    std::string sender;
    std::string body;
};

// Stores rooms and chat messages, keeping messages only for a limited time
// (24 hours by default). Safe to use from several threads. Errors are reported
// by return value so a database problem never takes down a client thread.
class ChatHistory {
public:
    static constexpr std::int64_t DEFAULT_RETENTION_SECONDS = 24 * 60 * 60;

    explicit ChatHistory(Database& db, std::int64_t retentionSeconds = DEFAULT_RETENTION_SECONDS);

    // Saves a room if it isn't stored yet.
    bool addRoom(const std::string& room);

    // Saves a message, creating the room if it doesn't exist yet.
    bool record(const std::string& room, const std::string& sender, const std::string& body);

    // Returns up to `limit` of the room's newest messages, oldest first. Only
    // messages inside the retention period are returned, even if older ones
    // haven't been deleted yet. Sets *hasMore if older messages exist beyond
    // the limit (within the retention period).
    std::vector<HistoryEntry> recent(const std::string& room, int limit, bool* hasMore = nullptr);

    // Deletes messages older than the retention period, then rooms that have no
    // messages left and were created before it (lobby is always kept), and gives
    // the freed space back to the file system. Returns the number of messages
    // deleted, or -1 on error.
    int pruneExpired();

    std::int64_t retentionSeconds() const { return retention; }

private:
    Database& db;
    std::int64_t retention;
    std::mutex mutex;
};

#endif
