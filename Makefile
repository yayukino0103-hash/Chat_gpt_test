CXX := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -Wpedantic -O2
INCLUDES := -Iinclude
TARGET := gomoku_server

SRC := main.cpp src/gomoku_server.cpp src/simple_json.cpp

all: $(TARGET)

$(TARGET): $(SRC)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(SRC) -o $(TARGET)

clean:
	rm -f $(TARGET)

.PHONY: all clean
