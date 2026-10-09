#include <iostream>  
#include <string>
#include <cstring>
#include <thread>    
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <ctime>
#include "../include/Commands.hpp"
#include "../include/Protocol.hpp"

// Turns the server's /history reply lines into something readable;
// every other line is printed as-is.
std::string displayLine(const std::string& line) {
    if (line.starts_with("~history-begin ")) {
        // "~history-begin <more> <room>"
        std::string rest = line.substr(15);
        bool more = rest.starts_with("1");
        std::string room = rest.size() > 2 ? rest.substr(2) : "";
        return "── #" + room + (more ? " (most recent messages)" : "") + " ──";
    }
    if (line.starts_with("~history-end "))
        return "── end of history ──";
    if (line.starts_with("~history ")) {
        // "~history <unix seconds> <sender>: <body>"
        std::string rest = line.substr(9);
        auto space = rest.find(' ');
        if (space != std::string::npos) {
            std::time_t when = std::strtoll(rest.substr(0, space).c_str(), nullptr, 10);
            char stamp[32];
            std::strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M", std::localtime(&when));
            return std::string("[") + stamp + "] " + rest.substr(space + 1);
        }
    }
    return line;
}

void receive_messages(int clientSocket) {
    char buffer[1024];
    Protocol::LineBuffer lines;
    while (true) {
        int bytes = recv(clientSocket, buffer, sizeof(buffer), 0);

        if (bytes > 0) {
            lines.append(buffer, bytes);
            while (auto line = lines.nextLine())
                std::cout << "\r" << displayLine(*line) << "\n";
            std::cout << "> " << std::flush;
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
    std::string serverIP;
    sockaddr_in serverAddress{};
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_port = htons(8080);
    std::cout << "enter the IP address of the chat-server: ";
    std::cin >> serverIP;



    inet_pton(AF_INET, serverIP.c_str(), &serverAddress.sin_addr);

    if (connect(sock, (struct sockaddr*)&serverAddress, sizeof(serverAddress)) == -1) {
        perror("Connect failed");
        return 1;
    }

    std::cout << "--- Connected to Chat Server ---" << std::endl;
    std::cout << "Commands: /help to list all commands, /nick <name>, /join <room>, /rooms to list active rooms, /history to show this room's recent messages, or /quit to exit." << std::endl;

    std::thread receiveThread(receive_messages, sock);
    receiveThread.detach();

    std::string input;
    while (true) {
        std::cout << "> " << std::flush;
        if (!std::getline(std::cin, input) || input == "/quit") {
            break;
        }

        if (input.empty()) continue;
        // The server reads newline-terminated lines.
        input += '\n';
        if (send(sock, input.c_str(), input.length(), 0) == -1) {
            std::cerr << "Failed to send message." << std::endl;
            break;
        }
    }

    close(sock);
    return 0;
}