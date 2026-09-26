# Consumes cortex the way FetchContent/CPM users do: add_subdirectory().
cmake_minimum_required(VERSION 3.28)
project(cortex_subdir_consumer CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_subdirectory(${CORTEX_SOURCE_DIR} cortex)

add_executable(consumer ../main.cpp)
target_link_libraries(consumer PRIVATE cortex::cortex)
