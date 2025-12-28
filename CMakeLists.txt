cmake_minimum_required(VERSION 3.28.3)

project(cortex
    VERSION 1.0.0
    LANGUAGES CXX
    DESCRIPTION "Stackful coroutine library with WASM support"
)

# --- Options ---
option(CORTEX_BUILD_TESTS "Build unit tests" ON)
option(CORTEX_BUILD_EXAMPLES "Build examples" OFF)

# --- Module Path ---
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# --- Load Modules ---
include(Dependencies)
include(CompilerConfig)
include(WasmConfig)

# --- Subdirectories ---
add_subdirectory(src)

if(CORTEX_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(CORTEX_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()
