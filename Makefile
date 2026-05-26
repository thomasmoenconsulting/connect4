CXX      := g++
CXXFLAGS := -std=c++20 -O3 -march=native -Wall -Wextra
LDFLAGS  := -fopenmp

SRC      := src/board.cpp src/solver.cpp
MAIN     := src/main.cpp
MONITOR  := src/monitor.cpp

.PHONY: all test clean

all: connect4 monitor

connect4: $(MAIN) $(SRC) src/board.hpp src/solver.hpp src/transposition.hpp src/evaluator.hpp
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(MAIN) $(SRC) -o $@

monitor: $(MONITOR) $(SRC) src/board.hpp src/solver.hpp src/transposition.hpp src/evaluator.hpp
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(MONITOR) $(SRC) -o $@

test: test_board test_solver
	./test_board
	./test_solver

test_board: tests/test_board.cpp $(SRC)
	$(CXX) $(CXXFLAGS) tests/test_board.cpp $(SRC) -o $@

test_solver: tests/test_solver.cpp $(SRC)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) tests/test_solver.cpp $(SRC) -o $@

clean:
	rm -f connect4 monitor test_board test_solver
