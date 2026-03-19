#include "../../include/Commands.hpp"
#include "../../include/ConnectionHandler.hpp"


void Commands::joinNewRoom(std::shared_ptr<ConnectionHandler> conn, std::string& newRoom) {
    newRoom.erase(std::remove(newRoom.begin(), newRoom.end(), '\n'), newRoom.end());
    newRoom.erase(std::remove(newRoom.begin(), newRoom.end(), '\r'), newRoom.end());
    newRoom.erase(newRoom.find_last_not_of(" ") + 1);

    if (!newRoom.empty()) {
        conn->changeRoom(newRoom);
    }
}
void Commands::setNewNickname(std::shared_ptr<ConnectionHandler> conn, std::string& newNick) {
    newNick.erase(std::remove(newNick.begin(), newNick.end(), '\n'), newNick.end());
    newNick.erase(std::remove(newNick.begin(), newNick.end(), '\r'), newNick.end());
    newNick.erase(newNick.find_last_not_of(" ") + 1);
    if (!newNick.empty()) {
        conn->setClientLabel(newNick);
    }
} 
std::string Commands::getUsersInRoom(std::shared_ptr<ConnectionHandler> conn, std::vector<std::shared_ptr<ConnectionHandler>>& connections) {
    std::string users = "";
    for (const auto& connection : connections) {
        if (connection->getCurrentRoom() == conn->getCurrentRoom()) {
            users += connection->getClientLabel();
            users += '\n';
        }
    }
    return users;
    
}