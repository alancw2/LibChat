#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <thread>
#include <format>
void handleClient(int clientSocket) {
    while (true) {
        char buffer[1024] = {0};
        int bytes = recv(clientSocket, buffer, sizeof(buffer), 0);

        if (bytes == -1) {
            std::cerr << std::format("receiving data with recv failed from {}", clientSocket) << std::endl;
            break;
            }
        if (bytes == 0) {
            std::cout << std::format("client {} has disconnected", clientSocket) << std::endl;
            break;
            }
        
        //Process data and send a response
        if (send(clientSocket, buffer, bytes, 0) == -1) {
            std::cerr << std::format("send failed from {}", clientSocket) << std::endl;
            break;
            }
        }
        close(clientSocket);
    }
std::string get_peer_ip_address(int client_socket_fd) {
    struct sockaddr_storage addr;
    socklen_t len = sizeof(addr);
    char ipstr[INET6_ADDRSTRLEN];
    int port;
    if (getpeername(client_socket_fd, (struct sockaddr*)&addr, &len) == -1) {
        perror("getpeername");
        return "";
    }
    if (addr.ss_family == AF_INET) {
        struct sockaddr_in *s = (struct sockaddr_in *)&addr;
        port = ntohs(s->sin_port);
        inet_ntop(AF_INET, &s->sin_addr, ipstr, sizeof(ipstr));
    }
    //return ipstr;
    return std::format("{}:{}", ipstr, std::to_string(port));
}
int main() {
    //creating the socket and server address/port
    int serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSocket == -1) {
        std::cerr << "socket creation failed" << std::endl;
        return 1;
    }
    struct sockaddr_in serverAddress;
    memset(&serverAddress, 0, sizeof(serverAddress));
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_port = htons(8080);
    inet_pton(AF_INET, "127.0.0.1", &serverAddress.sin_addr);
    int optval = 1;
    if (setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) < 0) {
        std::cerr << "setsockopt(SO_REUSEADDR) failed" << std::endl;
        return 1;
    }

    //binding the socket to address and port.
    if (bind(serverSocket, (struct sockaddr*)&serverAddress, sizeof(serverAddress)) == -1) {
        std::cerr << "socket binding failed" << std::endl;
        return 1;
    }

    //listening for connections on the socket once its all binded
    if (listen(serverSocket, 5) == -1) {
        std::cerr << "socked listening failed" << std::endl;
        return 1;
    }
    while (true) {
    //accept client connection
    sockaddr_in clientAddress;
    socklen_t clientSize = sizeof(clientAddress);
    int clientSocket = accept(serverSocket, (struct sockaddr*)&clientAddress, &clientSize);
    if (clientSocket > 0) {
        std::string peer_ipaddr = get_peer_ip_address(clientSocket);
        std::cout << std::format("client {} connected from {}", std::to_string(clientSocket), peer_ipaddr) << std::endl;
    }
    if (clientSocket == -1) {
        std::cerr << "accept failed" << std::endl;
        break;
    }
    //client loop, detached
    std::thread client_thread(handleClient, clientSocket);
    if (client_thread.joinable()) {
        client_thread.detach();
    }
    //handleClient(clientSocket);
    }
    close(serverSocket);   
    return 0;
}