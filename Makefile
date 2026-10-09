CXX = g++
CXXFLAGS = -std=c++20 -Wall -Wextra -pthread

SERVER_TARGET = server
CLIENT_TARGET = client
TEST_TARGET = tests

SRC_DIR = src
INC_DIR = include
TEST_DIR = test

# Server-specific files (everything except main.o is shared with the tests)
SERVER_LIB_OBJS = $(SRC_DIR)/Server.o $(SRC_DIR)/Protocol.o $(SRC_DIR)/ConnectionHandler.o $(SRC_DIR)/Commands.o $(SRC_DIR)/Database.o $(SRC_DIR)/ChatHistory.o
SERVER_OBJS = $(SRC_DIR)/main.o $(SERVER_LIB_OBJS)
SERVER_LIBS = -lsqlite3
# Client-specific files
CLIENT_OBJS = $(SRC_DIR)/Client.o $(SRC_DIR)/Protocol.o
# Test files: add new test .cpp files here
TEST_SRCS = $(TEST_DIR)/main.cpp $(TEST_DIR)/ServerHarness.cpp $(TEST_DIR)/DatabaseTest.cpp $(TEST_DIR)/BugFixTest.cpp $(TEST_DIR)/ServerTest.cpp $(TEST_DIR)/HistoryTest.cpp
TEST_OBJS = $(TEST_SRCS:.cpp=.o)

.PHONY: all clean tests test run_server run_client gui debug_gui run_gui run_debug_gui

all: $(SERVER_TARGET) $(CLIENT_TARGET)

# Link the Server
$(SERVER_TARGET): $(SERVER_OBJS)
	$(CXX) $(CXXFLAGS) $(SERVER_OBJS) $(SERVER_LIBS) -o $(SERVER_TARGET)

# Link the Client
$(CLIENT_TARGET): $(CLIENT_OBJS)
	$(CXX) $(CXXFLAGS) $(CLIENT_OBJS) -o $(CLIENT_TARGET)

# Compile Server Objects
$(SRC_DIR)/main.o: $(SRC_DIR)/main.cpp $(INC_DIR)/Server.hpp $(INC_DIR)/Database.hpp $(INC_DIR)/ChatHistory.hpp
	$(CXX) $(CXXFLAGS) -c $(SRC_DIR)/main.cpp -o $(SRC_DIR)/main.o

$(SRC_DIR)/Server.o: $(SRC_DIR)/Server.cpp $(INC_DIR)/Server.hpp $(INC_DIR)/ChatHistory.hpp $(INC_DIR)/Protocol.hpp $(INC_DIR)/ConnectionHandler.hpp $(INC_DIR)/Commands.hpp
	$(CXX) $(CXXFLAGS) -c $(SRC_DIR)/Server.cpp -o $(SRC_DIR)/Server.o

$(SRC_DIR)/Protocol.o: $(SRC_DIR)/Protocol.cpp $(INC_DIR)/Protocol.hpp $(INC_DIR)/ChatHistory.hpp
	$(CXX) $(CXXFLAGS) -c $(SRC_DIR)/Protocol.cpp -o $(SRC_DIR)/Protocol.o

$(SRC_DIR)/ConnectionHandler.o: $(SRC_DIR)/ConnectionHandler.cpp $(INC_DIR)/ConnectionHandler.hpp
	$(CXX) $(CXXFLAGS) -c $(SRC_DIR)/ConnectionHandler.cpp -o $(SRC_DIR)/ConnectionHandler.o

$(SRC_DIR)/Commands.o: $(SRC_DIR)/Commands/Commands.cpp $(INC_DIR)/Commands.hpp $(INC_DIR)/ConnectionHandler.hpp
	$(CXX) $(CXXFLAGS) -c $(SRC_DIR)/Commands/Commands.cpp -o $(SRC_DIR)/Commands.o

$(SRC_DIR)/ChatHistory.o: $(SRC_DIR)/ChatHistory.cpp $(INC_DIR)/ChatHistory.hpp $(INC_DIR)/Database.hpp
	$(CXX) $(CXXFLAGS) -c $(SRC_DIR)/ChatHistory.cpp -o $(SRC_DIR)/ChatHistory.o

$(SRC_DIR)/Database.o: $(SRC_DIR)/Database.cpp $(INC_DIR)/Database.hpp
	$(CXX) $(CXXFLAGS) -c $(SRC_DIR)/Database.cpp -o $(SRC_DIR)/Database.o

$(SRC_DIR)/Client.o: $(SRC_DIR)/Client.cpp $(INC_DIR)/Protocol.hpp
	$(CXX) $(CXXFLAGS) -c $(SRC_DIR)/Client.cpp -o $(SRC_DIR)/Client.o

# Compile test objects
$(TEST_DIR)/%.o: $(TEST_DIR)/%.cpp $(TEST_DIR)/Test.hpp $(TEST_DIR)/ServerHarness.hpp $(wildcard $(INC_DIR)/*.hpp)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Build ./tests and run every test
tests: $(TEST_OBJS) $(SERVER_LIB_OBJS)
	$(CXX) $(CXXFLAGS) $(TEST_OBJS) $(SERVER_LIB_OBJS) $(SERVER_LIBS) -o $(TEST_TARGET)
	./$(TEST_TARGET)

test: tests

# ── Qt GUIs (need Qt 6: brew install qtbase) ─────────────────────────
QT_PREFIX ?= $(shell brew --prefix qtbase 2>/dev/null)
GUI_DIR = qt_gui/libchat_gui
DEBUG_GUI_DIR = qt_gui/libchat_debug

gui:
	cmake -S $(GUI_DIR) -B $(GUI_DIR)/build -DCMAKE_PREFIX_PATH=$(QT_PREFIX) -DCMAKE_BUILD_TYPE=Release
	cmake --build $(GUI_DIR)/build -j 8

# Debug GUI: a LibChat server plus any number of test clients in one window
debug_gui:
	cmake -S $(DEBUG_GUI_DIR) -B $(DEBUG_GUI_DIR)/build -DCMAKE_PREFIX_PATH=$(QT_PREFIX) -DCMAKE_BUILD_TYPE=Release
	cmake --build $(DEBUG_GUI_DIR)/build -j 8

run_gui: gui
	./$(GUI_DIR)/build/LibChatGUI

run_debug_gui: debug_gui
	./$(DEBUG_GUI_DIR)/build/LibChatDebug

clean:
	rm -f $(SRC_DIR)/*.o $(TEST_DIR)/*.o $(SERVER_TARGET) $(CLIENT_TARGET) $(TEST_TARGET)
	rm -f *.o main
	rm -rf $(GUI_DIR)/build $(DEBUG_GUI_DIR)/build
run_server: $(SERVER_TARGET)
	./$(SERVER_TARGET)

run_client: $(CLIENT_TARGET)
	./$(CLIENT_TARGET)
