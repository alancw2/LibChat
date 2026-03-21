cmake_minimum_required(VERSION 3.11)
project(chat_tui)

set(CMAKE_CXX_STANDARD 20)

include(FetchContent)

FetchContent_Declare(
  ftxui
  GIT_REPOSITORY https://github.com/ArthurSonzogni/FTXUI.git
  GIT_TAG v5.0.0
)

FetchContent_MakeAvailable(ftxui)

add_executable(client_tui src/client_tui/client_tui.cpp)

target_link_libraries(client_tui
  PRIVATE ftxui::screen
          ftxui::dom
          ftxui::component
)