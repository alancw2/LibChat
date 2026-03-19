#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <thread>
#include <format>
#include <memory>
#include <mutex>
#include "../include/ConnectionHandler.hpp"
#include "../include/Commands.hpp"
#include <algorithm>
#include <vector>


void broadcastMessage(std::shared_ptr<ConnectionHandler> conn, std::vector<std::shared_ptr<ConnectionHandler>>& connections, std::mutex& connectionsMutex, const std::string& message) {
    {
            std::scoped_lock lock(connectionsMutex);
            for (const auto& connection : connections) {
                if (connection->getCurrentRoom() == conn->getCurrentRoom())
                connection->sendMessage(std::format("{}\n", message));
            }
    }
}
void handleClient(std::shared_ptr<ConnectionHandler> conn,
                  std::vector<std::shared_ptr<ConnectionHandler>>& connections,
                  std::mutex& connectionsMutex) {
    int clientSocket = conn->getSocket();
    char buffer[1024] = {0};
    while (true) {
        //char buffer[1024] = {0};
        int bytes = recv(clientSocket, buffer, sizeof(buffer), 0);
        std::string rawContent(buffer, bytes);
        if (rawContent.starts_with("/nick ")) {
            std::string newNick = rawContent.substr(6);
            const std::string& message = std::format("{} has changed their nickname to {}", conn->getClientLabel(), newNick);
            //std::string& m = message;
            broadcastMessage(conn, connections, connectionsMutex, message);
            Commands::setNewNickname(conn, newNick);
            //std::string& message = std::ref(std::format(""))
            continue;
        }
        if (rawContent.starts_with("/join ")) {
            std::string newRoom = rawContent.substr(6);
            const std::string& move_message = std::format("{} has moved to room {}", conn->getClientLabel(), newRoom);
            broadcastMessage(conn, connections, connectionsMutex, move_message);
            Commands::joinNewRoom(conn, newRoom);
            const std::string& enter_message = std::format("{} has entered room {}", conn->getClientLabel(), newRoom);
            broadcastMessage(conn, connections, connectionsMutex, enter_message);
            continue;
        }
        if (rawContent.starts_with("/who")) {
            broadcastMessage(conn, connections, connectionsMutex, Commands::getUsersInRoom(conn, connections));
            continue;
        }
        if (bytes == -1) {
            std::cerr << std::format("receiving data with recv failed from {}", clientSocket) << std::endl;
            break;
        }

        if (bytes == 0) {
            std::cout << std::format("client {} has disconnected", clientSocket) << std::endl;
            break;
        }

        std::string message = conn->getClientLabel() + ": " + std::string(buffer, bytes);

        broadcastMessage(conn, connections, connectionsMutex, message);
    }

    {
        std::scoped_lock lock(connectionsMutex);
        connections.erase(
            std::remove(connections.begin(), connections.end(), conn),
            connections.end()
        );
    }

    close(clientSocket);
}
std::string get_peer_ip_address(int client_socket_fd) {
    struct sockaddr_storage addr;
    socklen_t len = sizeof(addr);
    char ipstr[INET6_ADDRSTRLEN];
    int port;
    if (getpeername(client_socket_fd, (struct sockaddr*)&addr, &len) == -1) {
        perror("getpeername");
        return "";
    }
    if (addr.ss_family == AF_INET) {
        struct sockaddr_in *s = (struct sockaddr_in *)&addr;
        port = ntohs(s->sin_port);
        inet_ntop(AF_INET, &s->sin_addr, ipstr, sizeof(ipstr));
    }
    //return ipstr;
    return std::format("{}:{}", ipstr, std::to_string(port));
}
int main() {
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
    inet_pton(AF_INET, "127.0.0.1", &serverAddress.sin_addr);
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

    //
    std::vector<std::shared_ptr<ConnectionHandler>> connections;
    std::mutex connectionsMutex;


    //listening for connections on the socket once its all binded
    if (listen(serverSocket, 5) == -1) {
        std::cerr << "socket listening failed" << std::endl;
        return 1;
    }
    while (true) {

    //accept client connection
    sockaddr_in clientAddress;
    socklen_t clientSize = sizeof(clientAddress);
    int clientSocket = accept(serverSocket, (struct sockaddr*)&clientAddress, &clientSize);
    //accept fail check
    if (clientSocket == -1) {
        std::cerr << "accept failed" << std::endl;
        break;
    }

    
    std::string peer_ipaddr = get_peer_ip_address(clientSocket);
    std::cout << std::format("client {} connected from {}", std::to_string(clientSocket), peer_ipaddr) << std::endl;
    auto conn = std::make_shared<ConnectionHandler>(clientSocket, peer_ipaddr);
    {
        std::scoped_lock lock(connectionsMutex);
        connections.push_back(conn);
    }
    
    
    //client loop, detached
    std::thread client_thread(handleClient, conn, std::ref(connections), std::ref(connectionsMutex));
    if (client_thread.joinable()) {
        client_thread.detach();
    }
    //handleClient(clientSocket);
    }
    close(serverSocket);   
    return 0;
}