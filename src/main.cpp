#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <cstring>
#include <csignal>
#include "../include/Server.hpp"
#include "../include/Database.hpp"
#include "../include/ChatHistory.hpp"
#include <exception>
#include <memory>

// Usage: ./server [database path]   (default: libchat.db)
int main(int argc, char** argv) {
    // Never let a write to a disconnected client kill the whole server.
    std::signal(SIGPIPE, SIG_IGN);

    //creating the socket and server address/port
    int serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSocket == -1) {
        std::cerr << "server socket creation failed" << std::endl;
        return 1;
    }
    struct sockaddr_in serverAddress;
    memset(&serverAddress, 0, sizeof(serverAddress));
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_port = htons(8080);
    serverAddress.sin_addr.s_addr = INADDR_ANY;

    int optval = 1;
    if (setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) < 0) {
        std::cerr << "setsockopt(SO_REUSEADDR) failed" << std::endl;
        return 1;
    }

    //binding the socket to address and port.
    if (bind(serverSocket, (struct sockaddr*)&serverAddress, sizeof(serverAddress)) == -1) {
        perror("bind");
        std::cerr << "socket binding failed" << std::endl;
        return 1;
    }

    //listening for connections on the socket once its all binded
    if (listen(serverSocket, 5) == -1) {
        std::cerr << "socket listening failed" << std::endl;
        return 1;
    }

    // Chat history lives in SQLite; without it the server still runs, it just
    // doesn't remember messages.
    const char* dbPath = argc > 1 ? argv[1] : "libchat.db";
    std::unique_ptr<Database> database;
    std::unique_ptr<ChatHistory> history;
    try {
        database = std::make_unique<Database>(dbPath);
        history = std::make_unique<ChatHistory>(*database);
        std::cout << "saving chat history to " << dbPath << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "chat history disabled: " << e.what() << std::endl;
    }

    ServerOptions options;
    options.history = history.get();
    runServer(serverSocket, options);
    close(serverSocket);
    return 0;
}
