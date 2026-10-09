#include "ServerHarness.hpp"
#include "Test.hpp"
#include "../include/Protocol.hpp"
#include "../include/Server.hpp"
#include <arpa/inet.h>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <netinet/in.h>
#include <poll.h>
#include <spawn.h>
#include <stdexcept>
#include <sys/socket.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>

extern char** environ;

#ifdef MSG_NOSIGNAL
static constexpr int SEND_FLAGS = MSG_NOSIGNAL;
#else
static constexpr int SEND_FLAGS = 0;
#endif

namespace Harness {

// Runs fn in a brand-new process: this test binary started again with
// "--child <test name>". A plain fork() isn't safe here: on macOS, system
// libraries the parent already used (SQLite's logging, for one) can crash in a
// forked child that doesn't exec.
void isolated(const std::function<void()>& fn, unsigned timeoutSeconds) {
    if (Test::isChild) {
        // We are that new process: run the test here. Failures are reported by
        // main()'s runChild.
        alarm(timeoutSeconds);
        fn();
        return;
    }

    int pipeFds[2];
    if (pipe(pipeFds) == -1)
        throw std::runtime_error("pipe failed");
    std::cout.flush();
    std::cerr.flush();

    // The child's stdout/stderr (server logging) is discarded; its failure
    // message arrives on CHILD_RESULT_FD.
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_addclose(&actions, pipeFds[0]);
    posix_spawn_file_actions_adddup2(&actions, pipeFds[1], Test::CHILD_RESULT_FD);
    posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, "/dev/null", O_WRONLY, 0);
    posix_spawn_file_actions_adddup2(&actions, STDOUT_FILENO, STDERR_FILENO);

    std::string child = "--child";
    std::vector<char*> args = {Test::executablePath.data(), child.data(), Test::currentTest.data(), nullptr};
    pid_t pid = 0;
    int spawned = posix_spawn(&pid, Test::executablePath.c_str(), &actions, nullptr, args.data(), environ);
    posix_spawn_file_actions_destroy(&actions);
    close(pipeFds[1]);
    if (spawned != 0) {
        close(pipeFds[0]);
        throw std::runtime_error(std::string("could not start test process: ") + strerror(spawned));
    }

    std::string error;
    char buf[512];
    ssize_t n;
    while ((n = read(pipeFds[0], buf, sizeof(buf))) > 0)
        error.append(buf, n);
    close(pipeFds[0]);

    int status = 0;
    waitpid(pid, &status, 0);
    if (WIFSIGNALED(status)) {
        int sig = WTERMSIG(status);
        if (sig == SIGALRM)
            throw std::runtime_error("timed out after " + std::to_string(timeoutSeconds) + "s");
        throw std::runtime_error(std::string("test process crashed: ") + strsignal(sig));
    }
    if (WEXITSTATUS(status) != 0)
        throw std::runtime_error(error.empty() ? "test process failed" : error);
}

int listenOnLoopback(int* port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    int on = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    if (bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == -1 || listen(fd, 64) == -1)
        throw std::runtime_error("could not start test server");
    socklen_t len = sizeof(addr);
    getsockname(fd, reinterpret_cast<sockaddr*>(&addr), &len);
    *port = ntohs(addr.sin_port);
    return fd;
}

int startServer() {
    return startServer(ServerOptions{});
}

int startServer(const ServerOptions& options) {
    int port = 0;
    int fd = listenOnLoopback(&port);
    std::thread([fd, options] { runServer(fd, options); }).detach();
    return port;
}

Client::Client(int port) {
    fd = socket(AF_INET, SOCK_STREAM, 0);
#ifdef SO_NOSIGPIPE
    int on = 1;
    setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof(on));
#endif
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(port);
    if (connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == -1)
        throw std::runtime_error("could not connect to test server");
}

Client::~Client() {
    if (fd >= 0)
        close(fd);
}

void Client::send(const std::string& data) {
    size_t sent = 0;
    while (sent < data.size()) {
        ssize_t n = ::send(fd, data.data() + sent, data.size() - sent, SEND_FLAGS);
        if (n <= 0)
            throw std::runtime_error("send to server failed");
        sent += n;
    }
}

bool Client::readSome(int timeoutMs) {
    pollfd p{fd, POLLIN, 0};
    if (poll(&p, 1, timeoutMs) <= 0)
        return false;
    char buf[4096];
    ssize_t n = recv(fd, buf, sizeof(buf), 0);
    if (n <= 0) {
        closed = true;
        return false;
    }
    received.append(buf, n);
    return true;
}

bool Client::waitFor(const std::string& needle, int timeoutMs) {
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (received.find(needle) == std::string::npos) {
        auto left = std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - std::chrono::steady_clock::now()).count();
        if (left <= 0 || closed)
            return false;
        readSome(static_cast<int>(left));
    }
    return true;
}

void Client::drain(int ms) {
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (!closed) {
        auto left = std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - std::chrono::steady_clock::now()).count();
        if (left <= 0)
            break;
        readSome(static_cast<int>(left));
    }
}

bool Client::waitForClose(int timeoutMs) {
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (!closed && std::chrono::steady_clock::now() < deadline)
        readSome(50);
    return closed;
}

void Client::resetAbruptly() {
    linger l{1, 0};
    setsockopt(fd, SOL_SOCKET, SO_LINGER, &l, sizeof(l));
    close(fd);
    fd = -1;
    closed = true;
}

std::string Client::localAddress() const {
    sockaddr_storage addr{};
    socklen_t len = sizeof(addr);
    getsockname(fd, reinterpret_cast<sockaddr*>(&addr), &len);
    return Protocol::formatPeerAddress(reinterpret_cast<sockaddr*>(&addr));
}

bool Client::nick(const std::string& name) {
    send("/nick " + name + "\n");
    return waitFor("has changed their nickname to " + name + "\n");
}

}
