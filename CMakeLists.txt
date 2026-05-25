cmake_minimum_required(VERSION 3.20)
project(connect4 CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(OpenMP REQUIRED)

# Main executable
add_executable(connect4
    src/main.cpp
    src/board.cpp
    src/solver.cpp
)
target_include_directories(connect4 PRIVATE src)
target_compile_options(connect4 PRIVATE -O3 -march=native -Wall -Wextra)
target_link_libraries(connect4 PRIVATE OpenMP::OpenMP_CXX)

# Tests
enable_testing()

add_executable(test_board tests/test_board.cpp src/board.cpp)
target_include_directories(test_board PRIVATE src)
target_compile_options(test_board PRIVATE -O2 -Wall -Wextra)

add_executable(test_solver tests/test_solver.cpp src/board.cpp src/solver.cpp)
target_include_directories(test_solver PRIVATE src)
target_compile_options(test_solver PRIVATE -O3 -march=native)
target_link_libraries(test_solver PRIVATE OpenMP::OpenMP_CXX)

add_test(NAME board  COMMAND test_board)
add_test(NAME solver COMMAND test_solver)
