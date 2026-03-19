#include <iostream>  
#include <string>
#include <cstring>
#include <thread>    
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include "../include/Commands.hpp"

void receive_messages(int clientSocket) {
    char buffer[1024];
    while (true) {
        memset(buffer, 0, sizeof(buffer));
        int bytes = recv(clientSocket, buffer, sizeof(buffer) - 1, 0);
        
        if (bytes > 0) {
            std::cout << "\r" << std::string(buffer, bytes) << "> " << std::flush;
        } 
        else if (bytes == 0) {
            std::cout << "\n[Server closed connection]" << std::endl;
            break;
        } 
        else {
            break; 
        }
    }
}

int main() {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == -1) {
        perror("Socket creation failed");
        return 1;
    }

    sockaddr_in serverAddress;
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_port = htons(8080);
    inet_pton(AF_INET, "127.0.0.1", &serverAddress.sin_addr);

    if (connect(sock, (struct sockaddr*)&serverAddress, sizeof(serverAddress)) == -1) {
        perror("Connect failed");
        return 1;
    }

    std::cout << "--- Connected to Chat Server ---" << std::endl;
    std::cout << "Commands: /nick <name>, /join <room>, or /quit to exit." << std::endl;

    // Fixed: The function name here must match 'receive_messages' above
    std::thread receiveThread(receive_messages, sock);
    receiveThread.detach();

    std::string input;
    while (true) {
        std::cout << "> " << std::flush;
        if (!std::getline(std::cin, input) || input == "/quit") {
            break;
        }
        if (input == "/who") {

        }

        if (input.empty()) continue;
        if (send(sock, input.c_str(), input.length(), 0) == -1) {
            std::cerr << "Failed to send message." << std::endl;
            break;
        }
    }

    close(sock);
    return 0;
}