# Consumes an installed cortex through its CMake package config.
cmake_minimum_required(VERSION 3.28)
project(cortex_installed_consumer CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(cortex CONFIG REQUIRED)

add_executable(consumer ../main.cpp)
target_link_libraries(consumer PRIVATE cortex::cortex)

# The browser driver ships with the package.
if(NOT EXISTS "${cortex_JS_DRIVER}")
    message(FATAL_ERROR "cortex_JS_DRIVER does not name the browser driver: '${cortex_JS_DRIVER}'")
endif()
