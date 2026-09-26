include(cmake/CPM.cmake)

# --- Boost (Native Only) ---
# Only Boost.Context is needed. By default it is fetched with CPM; set
# CORTEX_USE_SYSTEM_BOOST=ON to use an installed Boost instead (required for
# `cmake --install`, since a fetched Boost cannot be exported).
set(CORTEX_BOOST_MIN_VERSION 1.74)
if(NOT EMSCRIPTEN)
    if(CORTEX_USE_SYSTEM_BOOST)
        message(STATUS "Native build detected: using system Boost.Context")
        find_package(Boost ${CORTEX_BOOST_MIN_VERSION} CONFIG REQUIRED COMPONENTS context)
    else()
        message(STATUS "Native build detected: Fetching Boost.Context via CPM")

        set(BOOST_OPTIONS
            "BOOST_ENABLE_CMAKE ON"
            "BOOST_SKIP_INSTALL_RULES ON"
            "BUILD_SHARED_LIBS OFF"
            "BOOST_INCLUDE_LIBRARIES context"
        )

        # Only Boost.Context's ucontext backend tells ASan about stack
        # switches; with the default fcontext backend an exception thrown on a
        # fiber stack makes ASan report false stack-buffer-underflows.
        if(CORTEX_USE_SANITIZERS)
            list(APPEND BOOST_OPTIONS "BOOST_CONTEXT_IMPLEMENTATION ucontext")
        endif()

        CPMAddPackage(
            NAME Boost
            VERSION 1.86.0
            URL https://github.com/boostorg/boost/releases/download/boost-1.86.0/boost-1.86.0-cmake.tar.xz
            URL_HASH SHA256=2c5ec5edcdff47ff55e27ed9560b0a0b94b07bd07ed9928b476150e16b0efc57
            OPTIONS ${BOOST_OPTIONS}
        )

        # Macros that change the layout of Boost.Context's activation record
        # must be identical in Boost's compiled sources and in every user of
        # its headers, so they travel with the target (PUBLIC).
        if(CORTEX_USE_SANITIZERS)
            # ASan stack-switch annotations (ucontext backend only).
            target_compile_definitions(boost_context PUBLIC BOOST_USE_ASAN BOOST_USE_UBSAN)
            if(APPLE)
                # On macOS, ucontext_t only has room for the machine context
                # when _XOPEN_SOURCE is defined before the first system header;
                # otherwise getcontext() writes past the struct, off the top of
                # the fiber stack. _DARWIN_C_SOURCE keeps the rest of the macOS
                # API visible.
                target_compile_definitions(boost_context PUBLIC _XOPEN_SOURCE=600 _DARWIN_C_SOURCE)
            endif()
        endif()
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
