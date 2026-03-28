add_executable(cortex_coroutine_test coroutine_test.cpp)
target_link_libraries(cortex_coroutine_test PRIVATE cortex::cortex GTest::gtest_main)
cortex_apply_warnings(cortex_coroutine_test)
cortex_apply_sanitizers(cortex_coroutine_test)

add_executable(cortex_memory_resource_test memory_resource_test.cpp)
target_link_libraries(cortex_memory_resource_test PRIVATE cortex::cortex GTest::gtest_main)
cortex_apply_warnings(cortex_memory_resource_test)
cortex_apply_sanitizers(cortex_memory_resource_test)

add_executable(cortex_base_coroutine_test base_coroutine_test.cpp)
target_link_libraries(cortex_base_coroutine_test PRIVATE cortex::cortex GTest::gtest_main)
cortex_apply_warnings(cortex_base_coroutine_test)
cortex_apply_sanitizers(cortex_base_coroutine_test)

add_executable(cortex_generator_test generator_test.cpp)
target_link_libraries(cortex_generator_test PRIVATE cortex::cortex GTest::gtest_main)
cortex_apply_warnings(cortex_generator_test)
cortex_apply_sanitizers(cortex_generator_test)

add_executable(cortex_tiny_fiber_test tiny_fiber_test.cpp)
target_link_libraries(cortex_tiny_fiber_test PRIVATE cortex::cortex GTest::gtest_main)
cortex_apply_warnings(cortex_tiny_fiber_test)
cortex_apply_sanitizers(cortex_tiny_fiber_test)

if(NOT EMSCRIPTEN)
    add_executable(cortex_fiber_test fiber_test.cpp)
    target_link_libraries(cortex_fiber_test PRIVATE cortex::cortex GTest::gtest_main)
    cortex_apply_warnings(cortex_fiber_test)
    cortex_apply_sanitizers(cortex_fiber_test)
endif()

if(EMSCRIPTEN)
    # Configure all executables for WASM
    set_target_properties(cortex_coroutine_test PROPERTIES SUFFIX ".js")
    set_target_properties(cortex_memory_resource_test PROPERTIES SUFFIX ".js")
    set_target_properties(cortex_base_coroutine_test PROPERTIES SUFFIX ".js")
    set_target_properties(cortex_generator_test PROPERTIES SUFFIX ".js")
    set_target_properties(cortex_tiny_fiber_test PROPERTIES SUFFIX ".js")

    add_test(NAME cortex_wasm_coroutine_test COMMAND node cortex_coroutine_test.js)
    add_test(NAME cortex_wasm_memory_resource_test COMMAND node cortex_memory_resource_test.js)
    add_test(NAME cortex_wasm_base_coroutine_test COMMAND node cortex_base_coroutine_test.js)
    add_test(NAME cortex_wasm_generator_test COMMAND node cortex_generator_test.js)
    add_test(NAME cortex_wasm_tiny_fiber_test COMMAND node cortex_tiny_fiber_test.js)

    set(WASM_TEST_LINK_OPTIONS
        "-sEXIT_RUNTIME=1"
        "-sALLOW_MEMORY_GROWTH=1"
        "-sNODERAWFS=1"
        "-fexceptions"
        "-sASYNCIFY_STACK_SIZE=1048576"
        "-sSTACK_SIZE=1048576"
    )

    target_link_options(cortex_coroutine_test PRIVATE ${WASM_TEST_LINK_OPTIONS})
    target_link_options(cortex_memory_resource_test PRIVATE ${WASM_TEST_LINK_OPTIONS})
    target_link_options(cortex_base_coroutine_test PRIVATE ${WASM_TEST_LINK_OPTIONS})
    target_link_options(cortex_generator_test PRIVATE ${WASM_TEST_LINK_OPTIONS})
    target_link_options(cortex_tiny_fiber_test PRIVATE ${WASM_TEST_LINK_OPTIONS})

else()
    include(GoogleTest)
    gtest_discover_tests(cortex_coroutine_test)
    gtest_discover_tests(cortex_memory_resource_test)
    gtest_discover_tests(cortex_base_coroutine_test)
    gtest_discover_tests(cortex_generator_test)
    gtest_discover_tests(cortex_tiny_fiber_test)
    gtest_discover_tests(cortex_fiber_test)
endif()
