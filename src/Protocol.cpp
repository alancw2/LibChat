#include "../include/Protocol.hpp"
#include <arpa/inet.h>
#include <format>
#include <netinet/in.h>

namespace Protocol {

void LineBuffer::append(const char* data, std::size_t length) {
    buffer.append(data, length);
}

std::optional<std::string> LineBuffer::nextLine() {
    std::size_t newline = buffer.find('\n');
    std::string line;
    if (newline != std::string::npos && newline <= MAX_LINE_LENGTH) {
        line = buffer.substr(0, newline);
        buffer.erase(0, newline + 1);
    } else if (buffer.size() >= MAX_LINE_LENGTH) {
        line = buffer.substr(0, MAX_LINE_LENGTH);
        buffer.erase(0, MAX_LINE_LENGTH);
    } else {
        return std::nullopt;
    }
    if (!line.empty() && line.back() == '\r')
        line.pop_back();
    return line;
}

std::size_t LineBuffer::pending() const {
    return buffer.size();
}

std::string formatHistory(const std::string& room, const std::vector<HistoryEntry>& entries, bool more) {
    std::string out = std::format("~history-begin {} {}\n", more ? 1 : 0, room);
    for (const auto& e : entries)
        out += std::format("~history {} {}: {}\n", e.createdAt, e.sender, e.body);
    out += std::format("~history-end {}\n", room);
    return out;
}

std::string formatPeerAddress(const sockaddr* address) {
    char ip[INET6_ADDRSTRLEN] = {0};
    if (address->sa_family == AF_INET) {
        auto* v4 = reinterpret_cast<const sockaddr_in*>(address);
        inet_ntop(AF_INET, &v4->sin_addr, ip, sizeof(ip));
        // Pass std::string, not the char array: std::format prints the whole
        // array, including the NUL bytes and garbage after the address.
        return std::format("{}:{}", std::string(ip), ntohs(v4->sin_port));
    }
    if (address->sa_family == AF_INET6) {
        auto* v6 = reinterpret_cast<const sockaddr_in6*>(address);
        inet_ntop(AF_INET6, &v6->sin6_addr, ip, sizeof(ip));
        return std::format("[{}]:{}", std::string(ip), ntohs(v6->sin6_port));
    }
    return "";
}

}
