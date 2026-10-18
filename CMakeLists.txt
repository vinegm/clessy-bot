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
    src/datagen.cpp
    src/binpack.cpp
    src/engine.cpp
    src/position.cpp
    src/move_gen.cpp
    src/nnue.cpp
    src/eval.cpp
    src/search.cpp
    src/transposition_table.cpp
    src/attacks.cpp
    src/notation.cpp
    src/uci_parser.cpp
    src/uci_formatter.cpp
    src/logger.cpp)

find_package(Threads REQUIRED)

include(FetchContent)
# NOTE: SYSTEM keeps clang-tidy and warnings out of CLI11's own headers
FetchContent_Declare(
  cli11
  URL https://github.com/CLIUtils/CLI11/archive/refs/tags/v2.5.0.tar.gz
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
  SYSTEM)
FetchContent_MakeAvailable(cli11)

add_executable(clessy ${SOURCES})
target_include_directories(clessy PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_compile_options(clessy PRIVATE -O3 -march=native)
target_link_libraries(clessy PRIVATE Threads::Threads CLI11::CLI11)

# Dev tooling
file(GLOB_RECURSE
    FORMAT_FILES
    CONFIGURE_DEPENDS
    ${PROJECT_SOURCE_DIR}/src/*.cpp
    ${PROJECT_SOURCE_DIR}/include/*.hpp)
list(TRANSFORM
    SOURCES
    PREPEND
    ${PROJECT_SOURCE_DIR}/
    OUTPUT_VARIABLE
    LINT_FILES)

find_program(CLANG_FORMAT clang-format)
find_program(CLANG_TIDY clang-tidy)

if(CLANG_FORMAT)
  add_custom_target(
    fmt
    COMMAND ${CLANG_FORMAT} -i ${FORMAT_FILES}
    COMMENT "Formatting sources")

  add_custom_target(
    fmt-check
    COMMAND ${CLANG_FORMAT} --dry-run --Werror ${FORMAT_FILES}
    COMMENT "Checking formatting")
endif()

if(CLANG_TIDY)
  add_custom_target(
    lint
    COMMAND ${CLANG_TIDY} -p ${PROJECT_BINARY_DIR} --quiet
            ${LINT_FILES}
    COMMENT "Running clang-tidy")
endif()

if(CLANG_FORMAT AND CLANG_TIDY)
  add_custom_target(ci DEPENDS fmt-check lint)
endif()
