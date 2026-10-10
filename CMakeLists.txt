cmake_minimum_required(VERSION 3.20)

project(cgrep
    VERSION 0.1.0
    DESCRIPTION "A parallel recursive search tool"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ---------------------------------------------------------------- dependencies
include(FetchContent)

FetchContent_Declare(fmt
    GIT_REPOSITORY https://github.com/fmtlib/fmt.git
    GIT_TAG        11.0.2
    GIT_SHALLOW    TRUE)

FetchContent_Declare(doctest
    GIT_REPOSITORY https://github.com/doctest/doctest.git
    GIT_TAG        v2.5.3
    GIT_SHALLOW    TRUE)

FetchContent_MakeAvailable(fmt doctest)

# ---------------------------------------------------------------- warning flags
# An INTERFACE target carries no code -- only requirements it passes to whoever
# links it. This is how you share flags without a global variable.
add_library(cgrep_warnings INTERFACE)
target_compile_options(cgrep_warnings INTERFACE
    -Wall -Wextra -Wpedantic -Wshadow -Wnon-virtual-dtor -Wold-style-cast)

# ---------------------------------------------------------------- generated header
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/src/version.hpp.in"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/cgrep/version.hpp"
    @ONLY)

# ---------------------------------------------------------------- core library
add_library(cgrep_core
    src/format_util.cpp
    src/walk.cpp)

target_include_directories(cgrep_core
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${CMAKE_CURRENT_BINARY_DIR}/generated")

target_link_libraries(cgrep_core
    PUBLIC  fmt::fmt
    PRIVATE cgrep_warnings)

# ---------------------------------------------------------------- application
add_executable(cgrep src/main.cpp)
target_link_libraries(cgrep PRIVATE cgrep_core cgrep_warnings)

# ---------------------------------------------------------------- tests
enable_testing()

add_executable(cgrep_tests
    tests/test_format_util.cpp
    tests/test_version.cpp
    tests/test_walk.cpp)
target_link_libraries(cgrep_tests PRIVATE cgrep_core doctest::doctest cgrep_warnings)

include("${doctest_SOURCE_DIR}/scripts/cmake/doctest.cmake")
doctest_discover_tests(cgrep_tests)
