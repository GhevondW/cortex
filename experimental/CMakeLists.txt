# Unfinished modules. Not part of the cortex library, not installed, and not
# built unless CORTEX_BUILD_EXPERIMENTAL=ON.

if(NOT EMSCRIPTEN)
    # cortex::async — multithreaded runtime API design. Every function is still
    # a stub that throws "Not implemented yet".
    add_library(cortex_async_experimental STATIC
        async/src/runtime.cpp
        async/src/executor.cpp
        async/src/future.cpp
        async/src/spawn.cpp
        async/src/sync/mutex.cpp
        async/src/sync/shared_mutex.cpp
        async/src/sync/condition_variable.cpp
        async/src/sync/baton.cpp
        async/src/sync/semaphore.cpp
        async/src/sync/wait_group.cpp
        async/src/sync/event.cpp
    )
    target_include_directories(cortex_async_experimental PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/async/include)
    target_link_libraries(cortex_async_experimental PUBLIC cortex::cortex)
    cortex_apply_warnings(cortex_async_experimental)

    if(CORTEX_BUILD_TESTS)
        add_executable(cortex_async_experimental_test async/tests/async_test.cpp)
        target_link_libraries(cortex_async_experimental_test PRIVATE cortex_async_experimental GTest::gtest_main)
        cortex_apply_warnings(cortex_async_experimental_test)
        include(GoogleTest)
        gtest_discover_tests(cortex_async_experimental_test)
    endif()
endif()
