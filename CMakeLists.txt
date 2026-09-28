cmake_minimum_required(VERSION 3.20)
project(tile_solver LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(tile_core src/game.cpp src/solver.cpp)
target_include_directories(tile_core PUBLIC include)

add_executable(solver src/cli.cpp)
target_link_libraries(solver PRIVATE tile_core)

add_executable(solver-bench src/benchmark.cpp)
target_link_libraries(solver-bench PRIVATE tile_core)

enable_testing()
add_executable(solver-tests tests/test_solver.cpp)
target_link_libraries(solver-tests PRIVATE tile_core)
add_test(NAME solver-tests COMMAND solver-tests)
