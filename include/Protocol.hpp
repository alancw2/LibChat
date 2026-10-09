#ifndef PROTOCOL_HPP
#define PROTOCOL_HPP

#include <cstddef>
#include <optional>
#include <string>
#include <sys/socket.h>
#include <vector>
#include "ChatHistory.hpp"

namespace Protocol {
    // Lines longer than this are cut off and delivered without waiting for '\n',
    // so a client that never sends a newline can't grow the buffer forever.
    inline constexpr std::size_t MAX_LINE_LENGTH = 4096;

    // TCP is a byte stream: one recv() can hold several messages or part of one.
    // LineBuffer collects the bytes and hands back one complete '\n'-terminated
    // line at a time, with the trailing "\r\n" or "\n" removed.
    class LineBuffer {
    public:
        void append(const char* data, std::size_t length);
        std::optional<std::string> nextLine();
        std::size_t pending() const;

    private:
        std::string buffer;
    };

    // How many of a room's newest messages /history sends.
    inline constexpr int HISTORY_LIMIT = 50;

    // Reply to /history, oldest message first:
    //   ~history-begin <more> <room>           more is 1 if older messages exist
    //   ~history <unix seconds> <sender>: <body>
    //   ~history-end <room>
    // Lines start with '~', which nicknames and peer addresses never do.
    std::string formatHistory(const std::string& room, const std::vector<HistoryEntry>& entries, bool more);

    // Formats a peer address as "ip:port" (IPv4) or "[ip]:port" (IPv6).
    // Returns an empty string for unsupported address families.
    std::string formatPeerAddress(const sockaddr* address);
}

#endif
