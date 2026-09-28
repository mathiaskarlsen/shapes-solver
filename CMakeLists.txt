cmake_minimum_required(VERSION 3.20)
project(tile_solver LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Threads REQUIRED)
add_library(tile_core src/game.cpp src/solver.cpp src/bfs.cpp src/sat.cpp)
target_include_directories(tile_core PUBLIC include)
target_link_libraries(tile_core PUBLIC Threads::Threads)

add_executable(solver src/cli.cpp)
target_link_libraries(solver PRIVATE tile_core)
if(WIN32)
    target_link_libraries(solver PRIVATE Psapi)
endif()

add_executable(solver-bench src/benchmark.cpp)
target_link_libraries(solver-bench PRIVATE tile_core)

add_executable(solver-bfs src/bfs_cli.cpp)
target_link_libraries(solver-bfs PRIVATE tile_core)

add_executable(solver-sat src/sat_cli.cpp)
target_link_libraries(solver-sat PRIVATE tile_core)

enable_testing()
add_executable(solver-tests tests/test_solver.cpp)
target_link_libraries(solver-tests PRIVATE tile_core)
add_test(NAME solver-tests COMMAND solver-tests)
