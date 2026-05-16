cmake_minimum_required(VERSION 3.28.3)

project(cortex
    VERSION 1.0.0
    LANGUAGES CXX
    DESCRIPTION "Stackful coroutine library with WASM support"
)

# --- Options ---
option(CORTEX_BUILD_TESTS "Build unit tests" ON)
option(CORTEX_BUILD_EXAMPLES "Build examples" OFF)
option(CORTEX_BUILD_APPS "Build applications (AlgoViz, etc.)" OFF)
option(CORTEX_USE_SANITIZERS "Enable address and undefined behavior sanitizers" OFF)

# --- Module Path ---
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# --- Load Modules ---
include(Dependencies)
include(CompilerConfig)
include(WasmConfig)
include(AppRuntime)

# --- Subdirectories ---
add_subdirectory(src)

if(CORTEX_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(CORTEX_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

# Add apps/ when explicitly requested OR when building tests — some apps
# (currently apps/video_editor) ship a native engine library that has its own
# unit tests, and we want those tests to be part of the regular `ctest` run.
# Each app's CMakeLists is responsible for gating its WASM-only bits behind
# `if(EMSCRIPTEN)`.
if(CORTEX_BUILD_APPS OR CORTEX_BUILD_TESTS)
    add_subdirectory(apps)
endif()
