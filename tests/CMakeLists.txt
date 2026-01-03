# Existing test executable
add_executable(cortex_tests unit_test.cpp)
target_link_libraries(cortex_tests PRIVATE cortex::cortex GTest::gtest_main)
cortex_apply_warnings(cortex_tests)
cortex_apply_sanitizers(cortex_tests)

# New test executable
add_executable(cortex_coroutine_test coroutine_test.cpp)
target_link_libraries(cortex_coroutine_test PRIVATE cortex::cortex GTest::gtest_main)
cortex_apply_warnings(cortex_coroutine_test)
cortex_apply_sanitizers(cortex_coroutine_test)

# Memory Resource test executable
add_executable(cortex_memory_resource_test memory_resource_test.cpp)
target_link_libraries(cortex_memory_resource_test PRIVATE cortex::cortex GTest::gtest_main)
cortex_apply_warnings(cortex_memory_resource_test)
cortex_apply_sanitizers(cortex_memory_resource_test)

if(EMSCRIPTEN)
    # Configure all executables for WASM
    set_target_properties(cortex_tests PROPERTIES SUFFIX ".js")
    set_target_properties(cortex_coroutine_test PROPERTIES SUFFIX ".js")
    set_target_properties(cortex_memory_resource_test PROPERTIES SUFFIX ".js")

    add_test(NAME cortex_wasm_unit_tests COMMAND node cortex_tests.js)
    add_test(NAME cortex_wasm_coroutine_test COMMAND node cortex_coroutine_test.js)
    add_test(NAME cortex_wasm_memory_resource_test COMMAND node cortex_memory_resource_test.js)

    target_link_options(cortex_tests PRIVATE
            "-sEXIT_RUNTIME=1" "-sALLOW_MEMORY_GROWTH=1" "-sNODERAWFS=1" "-fexceptions")
    target_link_options(cortex_coroutine_test PRIVATE
            "-sEXIT_RUNTIME=1" "-sALLOW_MEMORY_GROWTH=1" "-sNODERAWFS=1" "-fexceptions")
    target_link_options(cortex_memory_resource_test PRIVATE
            "-sEXIT_RUNTIME=1" "-sALLOW_MEMORY_GROWTH=1" "-sNODERAWFS=1" "-fexceptions")
else()
    include(GoogleTest)
    gtest_discover_tests(cortex_tests)
    gtest_discover_tests(cortex_coroutine_test)
    gtest_discover_tests(cortex_memory_resource_test)
endif()
