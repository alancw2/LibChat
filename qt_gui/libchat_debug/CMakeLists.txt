cmake_minimum_required(VERSION 3.16)
project(LibChatDebug VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Network)
find_package(SQLite3 REQUIRED)

# The real LibChat server code, compiled straight into the debug app.
set(LIBCHAT_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..)

qt_add_executable(LibChatDebug
    src/main.cpp
    src/DebugWindow.cpp
    src/DebugWindow.h
    src/ServerController.cpp
    src/ServerController.h
    src/ClientPanel.cpp
    src/ClientPanel.h
    ${LIBCHAT_ROOT}/src/Server.cpp
    ${LIBCHAT_ROOT}/src/Protocol.cpp
    ${LIBCHAT_ROOT}/src/ConnectionHandler.cpp
    ${LIBCHAT_ROOT}/src/Commands/Commands.cpp
    ${LIBCHAT_ROOT}/src/Database.cpp
    ${LIBCHAT_ROOT}/src/ChatHistory.cpp
)

target_link_libraries(LibChatDebug PRIVATE Qt6::Widgets Qt6::Network SQLite::SQLite3)
target_include_directories(LibChatDebug PRIVATE src ${LIBCHAT_ROOT}/include)
