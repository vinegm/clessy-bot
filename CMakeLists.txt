cmake_minimum_required(VERSION 3.25)
project(cless-uci LANGUAGES CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS
    ON
    CACHE BOOL "Generate compile_commands.json" FORCE)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(SOURCES src/main.cpp src/uci.cpp src/engine.cpp src/position.cpp
            src/move_gen.cpp src/attacks.cpp)

add_executable(cless-uci ${SOURCES})
target_include_directories(cless-uci PRIVATE ${PROJECT_SOURCE_DIR}/include)

enable_testing()

find_library(CRITERION_LIB criterion)
include_directories(/usr/include)

set(TEST_SOURCES src/engine.cpp src/position.cpp src/move_gen.cpp
                 tests/perft_tests.cpp tests/unique_moves.cpp src/attacks.cpp)

add_executable(tests ${TEST_SOURCES})
target_link_libraries(tests ${CRITERION_LIB})
target_include_directories(tests PRIVATE ${PROJECT_SOURCE_DIR}/include)

add_test(NAME engine_tests COMMAND tests)
