include(cmake/CPM.cmake)

# --- Boost (Native Only) ---
if(NOT EMSCRIPTEN)
    message(STATUS "Native build detected: Fetching Boost.Context via CPM")
    CPMAddPackage(
        NAME Boost
        VERSION 1.86.0 # Versions less than 1.85.0 may need patches for installation targets.
        URL https://github.com/boostorg/boost/releases/download/boost-1.86.0/boost-1.86.0-cmake.tar.xz
        URL_HASH SHA256=2c5ec5edcdff47ff55e27ed9560b0a0b94b07bd07ed9928b476150e16b0efc57
        OPTIONS "BOOST_ENABLE_CMAKE ON" "BOOST_SKIP_INSTALL_RULES ON" # Set `OFF` for installation
        "BUILD_SHARED_LIBS OFF" "BOOST_INCLUDE_LIBRARIES context\\\;asio" # Note the escapes!
    )
else()
    message(STATUS "WASM build detected: Skipping Boost (Using Emscripten built-ins)")
endif()

CPMAddPackage(
    NAME function2
    VERSION 4.2.5 # Use the appropriate version of function2 that you need
    GITHUB_REPOSITORY Naios/function2
    GIT_TAG 4.2.5 # This should match the version you want to use
)

# --- GoogleTest (Always needed for tests) ---
if(CORTEX_BUILD_TESTS)
    CPMAddPackage(
        NAME GTest
        GITHUB_REPOSITORY google/googletest
        VERSION 1.14.0
    )
endif()
