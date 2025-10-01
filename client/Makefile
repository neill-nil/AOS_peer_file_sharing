CXX = g++
CXXFLAGS = -Wall -std=c++17 -Iinclude
LDFLAGS = -lssl -lcrypto -lpthread

SRC = $(wildcard src/*.cpp)
OBJ = $(SRC:.cpp=.o)
TARGET = client_app

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(OBJ) $(TARGET)
