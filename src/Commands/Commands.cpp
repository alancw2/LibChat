#include "../../include/Commands.hpp"
#include "../../include/ConnectionHandler.hpp"
#include <iostream>
#include <regex>

void Commands::joinNewRoom(std::shared_ptr<ConnectionHandler> conn, std::string& newRoom) {
    newRoom.erase(std::remove(newRoom.begin(), newRoom.end(), '\n'), newRoom.end());
    newRoom.erase(std::remove(newRoom.begin(), newRoom.end(), '\r'), newRoom.end());
    newRoom.erase(newRoom.find_last_not_of(" ") + 1);

    if (!newRoom.empty()) {
        conn->changeRoom(newRoom);
    }
}
void Commands::setNewNickname(std::shared_ptr<ConnectionHandler> conn, std::string& newNick) {
	if (std::regex_match(newNick, Config::APP_NICKNAME_PATTERN)) {
		conn->setClientLabel(newNick);
	} else {
		std::cerr << "1 - nickname change fail (bad nickname)" << std::endl;
		conn->sendMessage("1 - nickname change fail (bad nickname)");
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
