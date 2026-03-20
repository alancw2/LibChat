#include "../../include/Commands.hpp"
#include "../../include/ConnectionHandler.hpp"
#include "../../include/Config.hpp"
#include <iostream>
#include <format>
#include <regex>
#include <set>

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
bool Commands::nickAvailable(std::vector<std::shared_ptr<ConnectionHandler>>& connections, const std::string& newNick) {
    for (const auto& connection : connections) {
        if (connection->getClientLabel() == newNick) {
            std::cerr << "2 - nickname change fail (taken nickname)" << std::endl;
            return false;
        }
    }
    return true;
}
void Commands::showRooms(std::shared_ptr<ConnectionHandler> conn, std::vector<std::shared_ptr<ConnectionHandler>>& connections) {
	std::set<std::string> rooms;
	for (const auto& connection: connections) {
		rooms.insert(connection->getCurrentRoom() + "\n");
	}
	conn->sendMessage("active rooms: \n");
	for (std::string room : rooms) {
		conn->sendMessage(room);
	}
}

