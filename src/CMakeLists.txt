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
    tiny_fiber/wait_queue.cpp
    tiny_fiber/future_state.cpp
)

# exec sources (native only — requires OS threads)
if(NOT EMSCRIPTEN)
    set(CORTEX_EXEC_SRC
        exec/single_thread_executor.cpp
    )
endif()

target_sources(cortex
        PRIVATE
        base_coroutine.cpp
        coroutine.cpp
        memory_resource.cpp
        pooled_memory_resource.cpp
        guarded_stack_resource.cpp
        ${CORTEX_COROUTINE_SRC}
        ${CORTEX_TINY_FIBER_SRC}
        ${CORTEX_EXEC_SRC}
)

# Includes
target_include_directories(cortex PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_include_directories(cortex PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# Consumers only need C++20 to use the public headers.
target_compile_features(cortex PUBLIC cxx_std_20)

# function2 is header-only. Inside the build it comes from its CMake target;
# an installed cortex ships the header under include/cortex/third_party (see
# cmake/Install.cmake), so exported consumers need no function2 package.
target_link_libraries(cortex PUBLIC $<BUILD_INTERFACE:function2::function2>)
target_include_directories(cortex PUBLIC $<INSTALL_INTERFACE:include/cortex/third_party>)

# Dependencies
if(NOT EMSCRIPTEN)
    target_link_libraries(cortex PUBLIC Boost::context)
else()
    target_compile_definitions(cortex PRIVATE CORTEX_WASM_ASYNCIFY_STACK_SIZE=${CORTEX_WASM_ASYNCIFY_STACK_SIZE})
endif()

# Apply Modular Configuration
cortex_apply_warnings(cortex)
cortex_apply_sanitizers(cortex)
cortex_apply_lto(cortex)
cortex_configure_platform(cortex)

# Alias
add_library(cortex::cortex ALIAS cortex)

# cortex::web — awaiting JavaScript promises from fibers (WASM only; embind).
if(EMSCRIPTEN)
    add_library(cortex_web STATIC web/await.cpp)
    target_link_libraries(cortex_web PUBLIC cortex)
    target_link_options(cortex_web PUBLIC "-lembind")
    cortex_apply_warnings(cortex_web)
    add_library(cortex::web ALIAS cortex_web)
endif()
