cmake_minimum_required(VERSION 3.25)
project(clessy LANGUAGES CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS
    ON
    CACHE BOOL "Generate compile_commands.json" FORCE)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(SOURCES
    src/main.cpp
    src/uci.cpp
    src/engine.cpp
    src/position.cpp
    src/move_gen.cpp
    src/eval.cpp
    src/tt.cpp
    src/attacks.cpp
    src/uci_utils.cpp
    src/logger.cpp)

find_package(Threads REQUIRED)

add_executable(clessy ${SOURCES})
target_include_directories(clessy PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_compile_options(clessy PRIVATE -O3 -march=native)
target_link_libraries(clessy PRIVATE Threads::Threads)
