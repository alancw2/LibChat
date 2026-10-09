// Helpers for tests that run a real server on a loopback port.
#ifndef LIBCHAT_SERVER_HARNESS_HPP
#define LIBCHAT_SERVER_HARNESS_HPP

#include <functional>
#include <string>

struct ServerOptions;

namespace Harness {

// Runs fn in a separate, freshly started process (this test binary, running
// only the current test). Fails the test if fn throws, crashes (e.g. SIGPIPE
// or an uncaught exception in a server thread) or takes longer than
// timeoutSeconds. The child's stdout/stderr (server logging) is discarded.
// Call it once per test, wrapping the whole test body.
void isolated(const std::function<void()>& fn, unsigned timeoutSeconds = 20);

// Opens a listening socket on 127.0.0.1 with a free port. Stores the port in
// *port and returns the socket.
int listenOnLoopback(int* port);

// Starts runServer() on 127.0.0.1 with a free port and returns the port.
int startServer();
int startServer(const ServerOptions& options);

// A raw TCP client that records everything the server sends it.
class Client {
public:
    explicit Client(int port);
    ~Client();
    Client(const Client&) = delete;
    Client& operator=(const Client&) = delete;

    void send(const std::string& data);
    // Waits until `needle` has been received. Returns false on timeout.
    bool waitFor(const std::string& needle, int timeoutMs = 3000);
    // Collects whatever arrives during the next `ms` milliseconds.
    void drain(int ms = 200);
    // True once the server has closed the connection.
    bool waitForClose(int timeoutMs = 3000);
    // Closes with a TCP reset instead of a normal shutdown.
    void resetAbruptly();
    // This client's "ip:port" as the server sees it.
    std::string localAddress() const;
    // Sets a nickname and waits for the server to confirm it.
    bool nick(const std::string& name);

    std::string received;

private:
    bool readSome(int timeoutMs);
    int fd = -1;
    bool closed = false;
};

}

#endif
