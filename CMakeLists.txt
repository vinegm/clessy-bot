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

# NOTE: inlining across translation units matters here, the hot path runs through
# search, position, move_gen and nnue, which all live in separate files
include(CheckIPOSupported)
check_ipo_supported(RESULT IPO_SUPPORTED)
if(IPO_SUPPORTED)
  set_property(TARGET clessy PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
endif()

# Profile-guided optimization, driven by `./build.sh --pgo`
set(CLESSY_PGO
    "off"
    CACHE STRING "Profile-guided optimization stage: off, generate or use")
set_property(CACHE CLESSY_PGO PROPERTY STRINGS off generate use)
set(CLESSY_PGO_DIR
    "${PROJECT_BINARY_DIR}/pgo"
    CACHE PATH "Where the profile is written and read")

if(CLESSY_PGO STREQUAL "generate")
  # NOTE: atomic counters, the UCI search runs on its own thread
  target_compile_options(clessy PRIVATE -fprofile-generate=${CLESSY_PGO_DIR}
                                        -fprofile-update=prefer-atomic)
  target_link_options(clessy PRIVATE -fprofile-generate=${CLESSY_PGO_DIR})
elseif(CLESSY_PGO STREQUAL "use")
  # NOTE: partial training keeps code the workload never ran optimized for speed
  # instead of size, it would otherwise punish UCI paths datagen does not touch
  target_compile_options(clessy PRIVATE -fprofile-use=${CLESSY_PGO_DIR}
                                        -fprofile-partial-training -Wno-missing-profile)
  target_link_options(clessy PRIVATE -fprofile-use=${CLESSY_PGO_DIR})
elseif(NOT CLESSY_PGO STREQUAL "off")
  message(FATAL_ERROR "CLESSY_PGO must be off, generate or use, not '${CLESSY_PGO}'")
endif()

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
