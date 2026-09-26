# cortex_add_test(<name> <source> [NATIVE_ONLY])
#
# Builds one GoogleTest executable. Natively its tests are registered with
# gtest_discover_tests; under Emscripten it is linked to a .js file and run
# with node. NATIVE_ONLY skips the test on WASM (e.g. it needs OS threads).
set(CORTEX_WASM_TEST_LINK_OPTIONS
    "-sEXIT_RUNTIME=1"
    "-sALLOW_MEMORY_GROWTH=1"
    "-sNODERAWFS=1"
    "-fexceptions"
    "-sASYNCIFY_STACK_SIZE=1048576"
    "-sSTACK_SIZE=1048576"
)

if(NOT EMSCRIPTEN)
    include(GoogleTest)
endif()

function(cortex_add_test NAME SOURCE)
    cmake_parse_arguments(ARG "NATIVE_ONLY" "" "" ${ARGN})
    if(EMSCRIPTEN AND ARG_NATIVE_ONLY)
        return()
    endif()

    add_executable(${NAME} ${SOURCE})
    target_link_libraries(${NAME} PRIVATE cortex::cortex GTest::gtest_main)
    cortex_apply_warnings(${NAME})
    cortex_apply_sanitizers(${NAME})

    if(EMSCRIPTEN)
        set_target_properties(${NAME} PROPERTIES SUFFIX ".js")
        target_link_options(${NAME} PRIVATE ${CORTEX_WASM_TEST_LINK_OPTIONS})
        add_test(NAME ${NAME} COMMAND node ${NAME}.js)
    else()
        gtest_discover_tests(${NAME})
    endif()
endfunction()

cortex_add_test(cortex_coroutine_test coroutine_test.cpp)
cortex_add_test(cortex_memory_resource_test memory_resource_test.cpp)
cortex_add_test(cortex_pooled_memory_resource_test pooled_memory_resource_test.cpp)
cortex_add_test(cortex_base_coroutine_test base_coroutine_test.cpp)
# Exercises only the header-only Executor base, so it builds on both targets.
cortex_add_test(cortex_exec_executor_api_test executor_api_test.cpp)
cortex_add_test(cortex_generator_test generator_test.cpp)
cortex_add_test(cortex_tiny_fiber_test tiny_fiber_test.cpp)
cortex_add_test(cortex_fiber_teardown_test fiber_teardown_test.cpp)
cortex_add_test(cortex_tiny_fiber_nested_test tiny_fiber_nested_test.cpp)

# SingleThreadExecutor relies on OS threads, so its source is native-only
# (see src/CMakeLists.txt).
cortex_add_test(cortex_exec_single_thread_executor_test single_thread_executor_test.cpp NATIVE_ONLY)
cortex_add_test(cortex_tiny_fiber_future_test tiny_fiber_future_test.cpp)
cortex_add_test(cortex_tiny_fiber_errors_test tiny_fiber_errors_test.cpp)
cortex_add_test(cortex_guarded_stack_test guarded_stack_test.cpp NATIVE_ONLY)
