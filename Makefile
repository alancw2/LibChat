CXX = g++
CXXFLAGS = -std=c++20 -Wall -Wextra -pthread

TARGET = main

SRC_DIR = src
INC_DIR = include

SRCS = $(SRC_DIR)/main.cpp $(SRC_DIR)/ConnectionHandler.cpp
OBJS = $(SRC_DIR)/main.o $(SRC_DIR)/ConnectionHandler.o

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) $(OBJS) -o $(TARGET)

$(SRC_DIR)/main.o: $(SRC_DIR)/main.cpp $(INC_DIR)/ConnectionHandler.hpp
	$(CXX) $(CXXFLAGS) -c $(SRC_DIR)/main.cpp -o $(SRC_DIR)/main.o

$(SRC_DIR)/ConnectionHandler.o: $(SRC_DIR)/ConnectionHandler.cpp $(INC_DIR)/ConnectionHandler.hpp
	$(CXX) $(CXXFLAGS) -c $(SRC_DIR)/ConnectionHandler.cpp -o $(SRC_DIR)/ConnectionHandler.o

clean:
	rm -f $(OBJS) $(TARGET)

run: $(TARGET)
	./$(TARGET)