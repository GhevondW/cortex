# Consumes an installed cortex through its CMake package config.
cmake_minimum_required(VERSION 3.28)
project(cortex_installed_consumer CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(cortex CONFIG REQUIRED)

add_executable(consumer ../main.cpp)
target_link_libraries(consumer PRIVATE cortex::cortex)
