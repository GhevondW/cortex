cmake_minimum_required(VERSION 3.28.3)

project(cortex
    VERSION 1.0.0
    LANGUAGES CXX
    DESCRIPTION "Stackful coroutine library with WASM support"
)

# Coroutine switching and stack recycling are allocation- and call-heavy;
# an unoptimized build is several times slower. Default to Release when the
# user hasn't chosen a build type.
if(PROJECT_IS_TOP_LEVEL AND NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    message(STATUS "No CMAKE_BUILD_TYPE set: defaulting to Release")
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release RelWithDebInfo MinSizeRel)
endif()

# --- Options ---
# Tests default ON only when cortex is the top-level project, so FetchContent /
# add_subdirectory consumers don't build (or fetch GoogleTest for) our tests.
option(CORTEX_BUILD_TESTS "Build unit tests" ${PROJECT_IS_TOP_LEVEL})
option(CORTEX_BUILD_BENCHMARKS "Build micro-benchmarks" OFF)
option(CORTEX_BUILD_EXAMPLES "Build examples" OFF)
option(CORTEX_BUILD_APPS "Build applications (AlgoViz, etc.)" OFF)
option(CORTEX_USE_SANITIZERS "Enable address and undefined behavior sanitizers" OFF)
option(CORTEX_ENABLE_LTO "Enable link-time optimization for the cortex library" OFF)
option(CORTEX_USE_SYSTEM_BOOST "Use an installed Boost.Context instead of fetching it" OFF)

# Installing needs every dependency to be exportable. A CPM-fetched Boost is
# not, so on native builds install rules default ON only with system Boost.
if(PROJECT_IS_TOP_LEVEL AND (EMSCRIPTEN OR CORTEX_USE_SYSTEM_BOOST))
    set(CORTEX_INSTALL_DEFAULT ON)
else()
    set(CORTEX_INSTALL_DEFAULT OFF)
endif()
option(CORTEX_INSTALL "Generate install rules and the cortex CMake package" ${CORTEX_INSTALL_DEFAULT})
if(CORTEX_INSTALL AND NOT EMSCRIPTEN AND NOT CORTEX_USE_SYSTEM_BOOST)
    message(FATAL_ERROR
        "CORTEX_INSTALL=ON requires CORTEX_USE_SYSTEM_BOOST=ON on native builds: "
        "a Boost fetched by CPM cannot be exported with the cortex package.")
endif()

# --- Module Path ---
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# --- Load Modules ---
include(Dependencies)
include(CompilerConfig)
include(WasmConfig)
include(AppRuntime)

# --- Subdirectories ---
add_subdirectory(src)

if(CORTEX_INSTALL)
    include(Install)
endif()

if(CORTEX_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(CORTEX_BUILD_BENCHMARKS)
    add_subdirectory(benchmarks)
endif()

if(CORTEX_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

# Add apps/ when explicitly requested OR when building our own tests — some apps
# (currently apps/video_editor) ship a native engine library that has its own
# unit tests, and we want those tests to be part of the regular `ctest` run.
# Never pull apps into a consumer's build implicitly. Each app's CMakeLists is
# responsible for gating its WASM-only bits behind `if(EMSCRIPTEN)`.
if(CORTEX_BUILD_APPS OR (CORTEX_BUILD_TESTS AND PROJECT_IS_TOP_LEVEL))
    add_subdirectory(apps)
endif()
