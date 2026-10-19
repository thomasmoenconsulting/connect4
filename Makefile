CXX      := g++
CXXFLAGS := -std=c++20 -O3 -march=native -Wall -Wextra
LDFLAGS  := -fopenmp

SRC      := src/board.cpp src/solver.cpp src/opening_book.cpp
MAIN     := src/main.cpp
MONITOR  := src/monitor.cpp
HEADERS  := src/board.hpp src/solver.hpp src/transposition.hpp src/evaluator.hpp src/opening_book.hpp

# Pons' solver and 14-ply book, used by import_book to generate data/connect4.book.
PONS_DIR  ?= /tmp/pons-c4
PONS_BIN  ?= $(PONS_DIR)/c4solver
PONS_BOOK ?= $(PONS_DIR)/7x6.book

.PHONY: all test clean book

all: connect4 monitor

connect4: $(MAIN) $(SRC) $(HEADERS)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(MAIN) $(SRC) -o $@

monitor: $(MONITOR) $(SRC) $(HEADERS)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(MONITOR) $(SRC) -o $@

test: test_book test_board test_solver
	./test_book
	./test_board
	./test_solver

test_book: tests/test_book.cpp $(SRC) $(HEADERS)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) tests/test_book.cpp $(SRC) -o $@

test_board: tests/test_board.cpp $(SRC) $(HEADERS)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) tests/test_board.cpp $(SRC) -o $@

test_solver: tests/test_solver.cpp $(SRC) $(HEADERS)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) tests/test_solver.cpp $(SRC) -o $@

import_book: tools/import_book.cpp $(SRC) $(HEADERS)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) tools/import_book.cpp $(SRC) -o $@

# Generate the 14-ply book by enumerating positions and querying Pons' solver.
# Requires Pons' c4solver binary and 7x6.book to exist (override PONS_DIR to taste).
book data/connect4.book: import_book
	@mkdir -p data
	./import_book $(PONS_BIN) $(PONS_BOOK) data/connect4.book

clean:
	rm -f connect4 monitor test_board test_solver test_book import_book
