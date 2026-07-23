include(cmake/CPM.cmake)

# --- Boost (Native Only) ---
if(NOT EMSCRIPTEN)
    message(STATUS "Native build detected: Fetching Boost.Context via CPM")
    
    set(BOOST_OPTIONS 
        "BOOST_ENABLE_CMAKE ON" 
        "BOOST_SKIP_INSTALL_RULES ON"
        "BUILD_SHARED_LIBS OFF" 
        "BOOST_INCLUDE_LIBRARIES context\\\;asio"
    )

    if(CORTEX_USE_SANITIZERS)
        # Boost.Context's default fcontext backend switches stacks in raw
        # assembly with no ASan fiber annotations, so ASan keeps stale
        # per-thread stack bounds ("ASan is ignoring requested
        # __asan_handle_no_return", github.com/google/sanitizers/issues/189)
        # and SEGVs once a fiber parked on one thread is resumed on another.
        # Only the ucontext backend carries the
        # __sanitizer_{start,finish}_switch_fiber annotations, so sanitizer
        # builds must use it. Slower than fcontext, but sanitizer-builds-only:
        # regular builds keep the default fcontext backend.
        list(APPEND BOOST_OPTIONS "BOOST_CONTEXT_IMPLEMENTATION ucontext")
    endif()

    CPMAddPackage(
        NAME Boost
        VERSION 1.86.0
        URL https://github.com/boostorg/boost/releases/download/boost-1.86.0/boost-1.86.0-cmake.tar.xz
        URL_HASH SHA256=2c5ec5edcdff47ff55e27ed9560b0a0b94b07bd07ed9928b476150e16b0efc57
        OPTIONS ${BOOST_OPTIONS}
    )

    if(CORTEX_USE_SANITIZERS)
        # BOOST_USE_ASAN both enables the __sanitizer_*_switch_fiber calls and
        # adds data members to the activation records that are compiled into
        # libboost_context (src/fiber.cpp, src/continuation.cpp in ucontext
        # mode), so it must be defined identically for boost_context's own
        # sources and for every consumer of its headers — PUBLIC on the boost
        # target covers both. BOOST_USE_UCONTEXT is already exported PUBLIC by
        # boost_context's CMake when BOOST_CONTEXT_IMPLEMENTATION=ucontext.
        target_compile_definitions(boost_context PUBLIC BOOST_USE_ASAN)
    endif()
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
