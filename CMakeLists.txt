cmake_minimum_required(VERSION 3.20)
project(hpla VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(HPLA_BUILD_TESTS "Build unit and integration tests" ON)
option(HPLA_BUILD_BENCHMARKS "Build benchmark and log generator" ON)
option(HPLA_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)

find_package(Threads REQUIRED)

add_library(hpla_core STATIC
    src/core/timestamp.cpp
    src/core/ip.cpp
    src/core/format.cpp
    src/parser/log_parser.cpp
    src/io/file_line_reader.cpp
    src/io/mapped_file.cpp
    src/filter/filter.cpp
    src/stats/hyperloglog.cpp
    src/stats/accumulator.cpp
    src/engine/engine.cpp
    src/export/exporter.cpp
    src/report/report.cpp
    src/cli/cli.cpp
)

target_include_directories(hpla_core PUBLIC src)
target_link_libraries(hpla_core PUBLIC Threads::Threads)

if(MSVC)
    target_compile_options(hpla_core PRIVATE /W4 /permissive- /Zc:__cplusplus)
else()
    target_compile_options(hpla_core PRIVATE
        -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion
        -Wold-style-cast -Wnon-virtual-dtor
    )
endif()

if(HPLA_WARNINGS_AS_ERRORS)
    if(MSVC)
        target_compile_options(hpla_core PRIVATE /WX)
    else()
        target_compile_options(hpla_core PRIVATE -Werror)
    endif()
endif()

add_executable(hpla src/main.cpp)
target_link_libraries(hpla PRIVATE hpla_core)

if(HPLA_BUILD_BENCHMARKS)
    add_executable(hpla_genlog tools/hpla_genlog.cpp)
    target_link_libraries(hpla_genlog PRIVATE hpla_core)

    add_executable(hpla_bench bench/bench.cpp)
    target_link_libraries(hpla_bench PRIVATE hpla_core)
endif()

if(HPLA_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# Put all binaries next to each other so the executables are easy to find.
set_target_properties(hpla PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
if(HPLA_BUILD_BENCHMARKS)
    set_target_properties(hpla_genlog hpla_bench PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
endif()
