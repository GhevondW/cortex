cmake_minimum_required(VERSION 3.21)

# This template attempts to be "fetch_content"-able
# so that it works well with tools like CPM or other
# manual dependency management

# Only set the cxx_standard if it is not set by someone else
if (NOT DEFINED CMAKE_CXX_STANDARD)
  set(CMAKE_CXX_STANDARD 20)
endif()

# strongly encouraged to enable this globally to avoid conflicts between
# -Wpedantic being enabled and -std=c++20 and -std=gnu++20 for example
# when compiling with PCH enabled
set(CMAKE_CXX_EXTENSIONS OFF)

# Set the project name and language
project(
  cortex
  VERSION 0.0.1
  DESCRIPTION ""
  LANGUAGES CXX C)

include(cmake/PreventInSourceBuilds.cmake)
include(ProjectOptions.cmake)

cortex_setup_options()

include(Dependencies.cmake)
cortex_setup_dependencies()

cortex_local_options()

target_compile_features(cortex_options INTERFACE cxx_std_${CMAKE_CXX_STANDARD})

add_library(cortex::options ALIAS cortex_options)
add_library(cortex::warnings ALIAS cortex_warnings)

#add_library(cortex::cortex_options INTERFACE IMPORTED)
#add_library(cortex::cortex_warnings INTERFACE IMPORTED)

# configure files based on CMake configuration options
add_subdirectory(configured_files)

# Adding the src:
add_subdirectory(src)

# Don't even look at tests if we're not top level
if(NOT PROJECT_IS_TOP_LEVEL)
  return()
endif()

# Adding the tests:
include(CTest)

if(CORTEX_BUILD_TESTING)
  add_subdirectory(test)
endif()
