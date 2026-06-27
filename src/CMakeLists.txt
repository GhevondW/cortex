add_library(cortex STATIC)

if (EMSCRIPTEN)
    set(CORTEX_COROUTINE_SRC detail/coroutine_emscripten_impl.cpp)
else ()
    set(CORTEX_COROUTINE_SRC detail/coroutine_native_impl.cpp)
endif()

# tiny_fiber sources
set(CORTEX_TINY_FIBER_SRC
    tiny_fiber/fiber.cpp
    tiny_fiber/scheduler.cpp
    tiny_fiber/yield.cpp
    tiny_fiber/mutex.cpp
    tiny_fiber/condition_variable.cpp
)

# async sources (native only — requires OS threads)
if(NOT EMSCRIPTEN)
    set(CORTEX_EXEC_SRC
        exec/single_thread_executor.cpp
    )

    set(CORTEX_ASYNC_SRC
        async/runtime.cpp
        async/executor.cpp
        async/future.cpp
        async/spawn.cpp
        async/sync/mutex.cpp
        async/sync/shared_mutex.cpp
        async/sync/condition_variable.cpp
        async/sync/baton.cpp
        async/sync/semaphore.cpp
        async/sync/wait_group.cpp
        async/sync/event.cpp
    )
endif()

target_sources(cortex
        PRIVATE
        base_coroutine.cpp
        coroutine.cpp
        memory_resource.cpp
        ${CORTEX_COROUTINE_SRC}
        ${CORTEX_TINY_FIBER_SRC}
        ${CORTEX_EXEC_SRC}
        ${CORTEX_ASYNC_SRC}
)

# Includes
target_include_directories(cortex PUBLIC
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_include_directories(cortex PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(cortex PUBLIC function2::function2)

# Dependencies
if(NOT EMSCRIPTEN)
    target_link_libraries(cortex PUBLIC Boost::context)
endif()

# Apply Modular Configuration
cortex_apply_warnings(cortex)
cortex_apply_sanitizers(cortex)
cortex_configure_platform(cortex)

# Alias
add_library(cortex::cortex ALIAS cortex)
