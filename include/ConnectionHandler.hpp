#ifndef CONNECTIONHANDLER_HPP
#define CONNECTIONHANDLER_HPP

#include <string>

class ConnectionHandler {
public:
    ConnectionHandler(int clientSocket, const std::string& clientLabel);
    void sendMessage(const std::string& message);
    std::string getClientLabel() const;
    int getSocket() const;
    void setClientLabel(std::string newNick);
    std::string getCurrentRoom();
    void changeRoom(std::string& new_room);

private:
    int clientSocket;
    std::string clientLabel;
    std::string currentRoom;
};

#endif