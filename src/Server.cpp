#include "../include/Server.hpp"
#include "../include/ChatHistory.hpp"
#include "../include/Commands.hpp"
#include "../include/ConnectionHandler.hpp"
#include "../include/Protocol.hpp"
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <format>
#include <iostream>
#include <memory>
#include <mutex>
#include <poll.h>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace {

using Connections = std::vector<std::shared_ptr<ConnectionHandler>>;

// Sends server events to ServerOptions::log, or to the terminal if none is set.
struct Logger {
    std::function<void(const std::string&)> sink;

    void event(const std::string& message) const {
        if (sink) sink(message);
        else std::cout << message << std::endl;
    }
    void error(const std::string& message) const {
        if (sink) sink("error: " + message);
        else std::cerr << message << std::endl;
    }
    // Every line from every client; only reported to a custom log so the
    // terminal server stays quiet.
    void traffic(const std::string& message) const {
        if (sink) sink(message);
    }
};

// Every write to a connection's label or room happens while holding
// connectionsMutex, so other threads can safely read them under the same lock.

// Sends a line to everyone in conn's room. The caller must hold connectionsMutex.
void broadcastLocked(const std::shared_ptr<ConnectionHandler>& conn, Connections& connections,
                     const std::string& message) {
    for (const auto& connection : connections) {
        if (connection->getCurrentRoom() == conn->getCurrentRoom())
            connection->sendMessage(std::format("{}\n", message));
    }
}

void broadcastMessage(std::shared_ptr<ConnectionHandler> conn, Connections& connections,
                      std::mutex& connectionsMutex, const std::string& message) {
    std::scoped_lock lock(connectionsMutex);
    broadcastLocked(conn, connections, message);
}

std::string trim(const std::string& s) {
    auto first = s.find_first_not_of(' ');
    if (first == std::string::npos)
        return "";
    return s.substr(first, s.find_last_not_of(' ') - first + 1);
}

// Handles one complete line from a client. Returns false if the client quit.
bool handleLine(std::shared_ptr<ConnectionHandler> conn, const std::string& line,
                Connections& connections, std::mutex& connectionsMutex,
                ChatHistory* history, const Logger& log) {
    if (line.empty())
        return true;

    if (line == "/quit")
        return false;

    if (line.starts_with("/nick ")) {
        std::string newNick = trim(line.substr(6));
        std::string oldNick = conn->getClientLabel();
        bool taken = false;
        bool changed = false;
        {
            // Check and set under one lock so two clients can't claim the same nick.
            std::scoped_lock lock(connectionsMutex);
            taken = !Commands::nickAvailable(connections, newNick);
            if (!taken && !newNick.empty())
                changed = Commands::setNewNickname(conn, newNick);
        }
        if (taken)
            conn->sendMessage("Nickname is taken.\n");
        else if (!changed)
            conn->sendMessage("Nickname change failed: invalid nickname.\n");
        else
            broadcastMessage(conn, connections, connectionsMutex,
                             std::format("{} has changed their nickname to {}", oldNick, newNick));
        return true;
    }

    if (line.starts_with("/join ")) {
        std::string newRoom = trim(line.substr(6));
        if (newRoom.empty())
            return true;
        broadcastMessage(conn, connections, connectionsMutex,
                         std::format("{} has moved to room {}", conn->getClientLabel(), newRoom));
        {
            std::scoped_lock lock(connectionsMutex);
            Commands::joinNewRoom(conn, newRoom);
        }
        if (history && !history->addRoom(newRoom))
            log.error(std::format("could not save room {}", newRoom));
        broadcastMessage(conn, connections, connectionsMutex,
                         std::format("{} has entered room {}", conn->getClientLabel(), newRoom));
        return true;
    }

    if (line.starts_with("/who")) {
        std::string users;
        {
            std::scoped_lock lock(connectionsMutex);
            users = Commands::getUsersInRoom(conn, connections);
        }
        conn->sendMessage(users);
        return true;
    }

    if (line.starts_with("/rooms")) {
        std::scoped_lock lock(connectionsMutex);
        Commands::showRooms(conn, connections);
        return true;
    }

    if (line.starts_with("/help")) {
        Commands::showCommands(conn);
        return true;
    }

    // Reading history and broadcasting both hold connectionsMutex, so a message
    // is either in the history a client receives or arrives after it, never both.
    if (line == "/history") {
        std::scoped_lock lock(connectionsMutex);
        std::string room = conn->getCurrentRoom();
        bool more = false;
        std::vector<HistoryEntry> entries;
        if (history)
            entries = history->recent(room, Protocol::HISTORY_LIMIT, &more);
        conn->sendMessage(Protocol::formatHistory(room, entries, more));
        return true;
    }

    {
        std::scoped_lock lock(connectionsMutex);
        std::string sender = conn->getClientLabel();
        if (history && !history->record(conn->getCurrentRoom(), sender, line))
            log.error(std::format("could not save message from {} to history", sender));
        broadcastLocked(conn, connections, sender + ": " + line);
    }
    return true;
}

void handleClient(std::shared_ptr<ConnectionHandler> conn, Connections& connections,
                  std::mutex& connectionsMutex, ChatHistory* history, Logger log) {
    int clientSocket = conn->getSocket();
    Protocol::LineBuffer lines;
    char buffer[1024];
    bool connected = true;
    while (connected) {
        ssize_t bytes = recv(clientSocket, buffer, sizeof(buffer), 0);
        if (bytes == -1) {
            if (errno == EINTR)
                continue;
            log.event(std::format("client {} disconnected (recv failed: {})", clientSocket, std::strerror(errno)));
            break;
        }
        if (bytes == 0) {
            log.event(std::format("client {} has disconnected", clientSocket));
            break;
        }

        lines.append(buffer, static_cast<size_t>(bytes));
        while (auto line = lines.nextLine()) {
            log.traffic(std::format("client {} ({}) > {}", clientSocket, conn->getClientLabel(), *line));
            if (!handleLine(conn, *line, connections, connectionsMutex, history, log)) {
                connected = false;
                break;
            }
        }
    }

    {
        std::scoped_lock lock(connectionsMutex);
        connections.erase(std::remove(connections.begin(), connections.end(), conn), connections.end());
    }
    close(clientSocket);
}

} // namespace

void runServer(int serverSocket, const ServerOptions& options) {
    Logger log{options.log};
    // Detached client threads keep references to these, so they are
    // deliberately never freed: they must outlive runServer.
    auto& connections = *new Connections;
    auto& connectionsMutex = *new std::mutex;

    // Expired messages are deleted when the server starts and then about once a
    // minute, from this loop (it already wakes every 200 ms). /history never
    // returns them even in the minute before they're deleted.
    using Clock = std::chrono::steady_clock;
    constexpr auto PRUNE_INTERVAL = std::chrono::seconds(60);
    auto lastPrune = Clock::now() - PRUNE_INTERVAL;
    auto pruneIfDue = [&] {
        if (!options.history || Clock::now() - lastPrune < PRUNE_INTERVAL)
            return;
        lastPrune = Clock::now();
        int deleted = options.history->pruneExpired();
        if (deleted < 0)
            log.error("could not delete expired messages");
        else if (deleted > 0)
            log.event(std::format("deleted {} expired message(s)", deleted));
    };

    while (!options.stop || !options.stop->load()) {
        pruneIfDue();
        // Wake up regularly so a stop request is noticed even with no new clients.
        pollfd listener{serverSocket, POLLIN, 0};
        int ready = poll(&listener, 1, 200);
        if (ready == 0 || (ready == -1 && errno == EINTR))
            continue;
        if (ready == -1) {
            log.error("poll failed");
            break;
        }

        sockaddr_storage clientAddress{};
        socklen_t clientSize = sizeof(clientAddress);
        int clientSocket = accept(serverSocket, reinterpret_cast<sockaddr*>(&clientAddress), &clientSize);
        if (clientSocket == -1) {
            if (errno == EINTR || errno == ECONNABORTED)
                continue;
            log.error("accept failed");
            break;
        }

        std::string peerAddress = Protocol::formatPeerAddress(reinterpret_cast<sockaddr*>(&clientAddress));
        log.event(std::format("client {} connected from {}", clientSocket, peerAddress));
        auto conn = std::make_shared<ConnectionHandler>(clientSocket, peerAddress);
        {
            std::scoped_lock lock(connectionsMutex);
            connections.push_back(conn);
        }

        std::thread(handleClient, conn, std::ref(connections), std::ref(connectionsMutex),
                    options.history, log).detach();
    }

    // Wake every client thread; each one sees end-of-stream and cleans up.
    std::scoped_lock lock(connectionsMutex);
    for (const auto& connection : connections)
        shutdown(connection->getSocket(), SHUT_RDWR);
}
