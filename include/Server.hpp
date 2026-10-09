#ifndef SERVER_HPP
#define SERVER_HPP

#include <atomic>
#include <functional>
#include <string>

class ChatHistory;

struct ServerOptions {
    // Receives every server event, including each line a client sends. Called
    // from server threads. If empty, connects and disconnects go to stdout.
    std::function<void(const std::string&)> log;
    // When set to true, runServer disconnects every client and returns.
    const std::atomic<bool>* stop = nullptr;
    // Where chat messages are saved and /history reads from. If null, nothing
    // is saved and /history returns an empty history.
    ChatHistory* history = nullptr;
};

// Accepts clients on an already bound and listening socket and serves each one
// on its own thread. Returns if accept() fails or options.stop becomes true.
void runServer(int serverSocket, const ServerOptions& options = {});

#endif
