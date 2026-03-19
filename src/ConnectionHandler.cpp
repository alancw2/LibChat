#include "../include/ConnectionHandler.hpp"
#include <string>
#include <sys/socket.h>
#include <unistd.h>
#include <iostream>

ConnectionHandler::ConnectionHandler(int clientSocket, const std::string& clientLabel)
    : clientSocket(clientSocket), clientLabel(clientLabel), currentRoom("lobby") {}

void ConnectionHandler::sendMessage(const std::string& message) {
    const char* data = message.c_str();
    size_t totalSent = 0;
    size_t messageLength = message.size();
    while (totalSent < messageLength) {
        ssize_t bytesSent = send(clientSocket, data + totalSent, messageLength - totalSent, 0);
        if (bytesSent == -1) {
            std::cerr << "send failed for " << clientLabel << std::endl;
            return;
        }
        totalSent += bytesSent;
    }
}
void ConnectionHandler::setClientLabel(std::string newNick) {
    clientLabel = newNick;
}
void ConnectionHandler::changeRoom(std::string& new_room) {
    currentRoom = new_room;
}
std::string ConnectionHandler::getCurrentRoom() {
    return currentRoom;
}
int ConnectionHandler::getSocket() const {
    return clientSocket;
}

std::string ConnectionHandler::getClientLabel() const {
    return clientLabel;
}