#ifndef COMMANDS_HPP
#define COMMANDS_HPP

#include <string>
#include <memory>

class ConnectionHandler;

namespace Commands {
    void joinNewRoom(std::shared_ptr<ConnectionHandler> conn, std::string& newRoom);
    void setNewNickname(std::shared_ptr<ConnectionHandler> conn, std::string& newNick);
 
}


#endif