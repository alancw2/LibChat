#ifndef COMMANDS_HPP
#define COMMANDS_HPP
#include <vector>
#include <string>
#include <algorithm>
#include <string>
#include <memory>

class ConnectionHandler;

namespace Commands {
    void joinNewRoom(std::shared_ptr<ConnectionHandler> conn, std::string& newRoom);
    void setNewNickname(std::shared_ptr<ConnectionHandler> conn, std::string& newNick);
    std::string getUsersInRoom(std::shared_ptr<ConnectionHandler> conn, std::vector<std::shared_ptr<ConnectionHandler>>& connections);
    bool nickAvailable(std::vector<std::shared_ptr<ConnectionHandler>>& connections, const std::string& newNick);
    void showRooms(std::shared_ptr<ConnectionHandler> conn, std::vector<std::shared_ptr<ConnectionHandler>>& connections);
}


#endif