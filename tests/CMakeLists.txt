cmake_minimum_required(VERSION 3.20)

include(FetchContent)
set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.17.0.zip
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(googletest)

add_executable(hpla_tests
    test_timestamp.cpp
    test_ip.cpp
    test_parser.cpp
    test_line_reader.cpp
    test_filters.cpp
    test_stats.cpp
    test_engine.cpp
    test_cli.cpp
    test_export.cpp
)

target_link_libraries(hpla_tests PRIVATE hpla_core GTest::gtest_main)
target_include_directories(hpla_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

if(MSVC)
    target_compile_options(hpla_tests PRIVATE /W4)
else()
    target_compile_options(hpla_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

include(GoogleTest)
gtest_discover_tests(hpla_tests)
