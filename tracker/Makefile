CXX = g++
CXXFLAGS = -Wall -std=c++17 -I./include
SRC = $(wildcard src/*.cpp)
OUT = tracker_app

all: $(OUT)

$(OUT): $(SRC)
	$(CXX) $(CXXFLAGS) $(SRC) -o $(OUT)

clean:
	rm -f $(OUT)
