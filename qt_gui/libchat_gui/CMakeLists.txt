cmake_minimum_required(VERSION 3.16)
project(LibChatGUI VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Network)

qt_add_executable(LibChatGUI
    src/main.cpp
    src/ChatClient.cpp
    src/ChatClient.h
    src/MainWindow.cpp
    src/MainWindow.h
    src/MessageBubble.cpp
    src/MessageBubble.h
    src/SettingsDialog.cpp
    src/SettingsDialog.h
    resources/resources.qrc
)

target_link_libraries(LibChatGUI PRIVATE Qt6::Widgets Qt6::Network)
target_include_directories(LibChatGUI PRIVATE src)

install(TARGETS LibChatGUI
    RUNTIME DESTINATION bin
)
