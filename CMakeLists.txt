cmake_minimum_required(VERSION 4.0)
project(MimoMemory CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_library(Mimo.Memory INTERFACE)
target_include_directories(Mimo.Memory INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(Mimo.Memory INTERFACE cxx_std_23)

include(CTest)
if(BUILD_TESTING)
    find_package(GTest CONFIG QUIET)
    if(NOT GTest_FOUND)
        include(FetchContent)
        FetchContent_Declare(googletest
            URL https://github.com/google/googletest/archive/refs/tags/v1.17.0.tar.gz
            URL_HASH SHA256=65fab701d9829d38cb77c14acdc431d2108bfdbf8979e40eb8ae567edf10b27c
            EXCLUDE_FROM_ALL)
        set(gtest_force_shared_crt ON CACHE BOOL "Use the shared CRT for GoogleTest")
        FetchContent_MakeAvailable(googletest)
    endif()

    add_executable(BitMapArenaTests
        tests/BitMapArenaTests.cpp
        tests/HierarchicalBitMapArenaTests.cpp)
    target_link_libraries(BitMapArenaTests PRIVATE Mimo.Memory GTest::gtest_main)
    include(GoogleTest)
    gtest_discover_tests(BitMapArenaTests PROPERTIES TIMEOUT 30)
endif()

option(MIMO_BUILD_BENCHMARKS "Build standalone memory allocation benchmarks" OFF)
if(MIMO_BUILD_BENCHMARKS)
    add_executable(Benchmarks benchmarks/Benchmark.cpp)
    set_target_properties(Benchmarks PROPERTIES OUTPUT_NAME benchmark)
    target_link_libraries(Benchmarks PRIVATE Mimo.Memory)
endif()
