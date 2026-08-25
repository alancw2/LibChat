#include <atomic>
#include <iostream>
#include <limits>
#include <string>
#include <thread>
#include <cstring>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>

namespace {

void receive_messages(int clientSocket, std::atomic<bool>& listening) {
    char buffer[1024];
    while (listening.load()) {
        memset(buffer, 0, sizeof(buffer));
        int bytes = recv(clientSocket, buffer, sizeof(buffer) - 1, 0);

        if (bytes > 0) {
            std::cout << "\r" << std::string(buffer, bytes) << "> " << std::flush;
        } else {
            listening = false;
            std::cout << "\n[Server closed connection]" << std::endl;
            break;
        }
    }
}

bool connect_to_server(const std::string& serverIP, int& sock) {
    sockaddr_in serverAddress{};
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_port = htons(8080);

    if (inet_pton(AF_INET, serverIP.c_str(), &serverAddress.sin_addr) != 1) {
        std::cerr << "Invalid IP address." << std::endl;
        return false;
    }

    int newSock = socket(AF_INET, SOCK_STREAM, 0);
    if (newSock == -1) {
        perror("Socket creation failed");
        return false;
    }

    if (connect(newSock, (struct sockaddr*)&serverAddress, sizeof(serverAddress)) == -1) {
        perror("Connect failed");
        close(newSock);
        return false;
    }

    sock = newSock;
    std::cout << "--- Connected to Chat Server at " << serverIP << " ---" << std::endl;
    std::cout << "Commands: /help to list all commands, /nick <name>, /join <room>, /rooms to list active rooms,"
                 " /disconnect to drop the current server, /connect <ip> to switch servers, or /quit to exit." << std::endl;
    return true;
}

void stop_connection(int& sock, std::thread& receiveThread, std::atomic<bool>& listening) {
    if (sock != -1) {
        listening = false;
        shutdown(sock, SHUT_RDWR);
        close(sock);
        sock = -1;
    }
    if (receiveThread.joinable()) {
        receiveThread.join();
    }
}

void start_receiver(int sock, std::thread& receiveThread, std::atomic<bool>& listening) {
    listening = true;
    if (receiveThread.joinable()) {
        receiveThread.join();
    }
    receiveThread = std::thread(receive_messages, sock, std::ref(listening));
}

}

int main() {
    int sock = -1;
    std::thread receiveThread;
    std::atomic<bool> listening{false};

    std::string serverIP;
    std::cout << "enter the IP address of the chat-server: ";
    std::cin >> serverIP;
    std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');

    if (!connect_to_server(serverIP, sock)) {
        return 1;
    }

    start_receiver(sock, receiveThread, listening);

    std::string input;
    while (true) {
        std::cout << "> " << std::flush;
        if (!std::getline(std::cin, input)) {
            break;
        }

        if (input == "/quit") {
            break;
        }

        if (input.rfind("/connect ", 0) == 0) {
            std::string newIP = input.substr(9);
            if (newIP.empty()) {
                std::cout << "Usage: /connect <ip>" << std::endl;
                continue;
            }

            stop_connection(sock, receiveThread, listening);
            if (connect_to_server(newIP, sock)) {
                start_receiver(sock, receiveThread, listening);
            } else {
                std::cout << "[Reconnect failed. You are disconnected.]" << std::endl;
            }
            continue;
        }

        if (input == "/disconnect") {
            stop_connection(sock, receiveThread, listening);
            std::cout << "[Disconnected. Use /connect <ip> to reconnect or /quit to exit]" << std::endl;
            continue;
        }

        if (sock == -1) {
            std::cout << "[Not connected. Use /connect <ip> to join a server]" << std::endl;
            continue;
        }

        if (input.empty()) {
            continue;
        }

        if (send(sock, input.c_str(), input.length(), 0) == -1) {
            std::cerr << "Failed to send message." << std::endl;
            stop_connection(sock, receiveThread, listening);
            std::cout << "[Connection lost. Use /connect <ip> to reconnect]" << std::endl;
        }
    }

    stop_connection(sock, receiveThread, listening);
    return 0;
}
