cmake_minimum_required(VERSION 4.0)
project(MimoMemory CXX)

set(CMAKE_CXX_STANDARD_REQUIRED 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_library(
    Mimo.Memory
        INTERFACE
            ./include)

add_executable(Main ./src/main.cpp)
target_link_libraries(Main PRIVATE Mimo.Memory)