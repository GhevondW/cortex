add_executable(cortex_tests unit_test.cpp)

target_link_libraries(cortex_tests PRIVATE 
    cortex::cortex 
    GTest::gtest_main
)

cortex_apply_warnings(cortex_tests)

if(EMSCRIPTEN)
    # 1. Set output to .js so Node can run it
    set_target_properties(cortex_tests PROPERTIES SUFFIX ".js")

    # 2. Add the test manually for CTest
    # Format: add_test(NAME <name> COMMAND <emulator> <binary>)
    add_test(NAME cortex_wasm_unit_tests 
             COMMAND node cortex_tests.js)
             
    # 3. Required Linker Flags
    # -sEXIT_RUNTIME=1: Ensures node process ends when C++ main ends
    # -sALLOW_MEMORY_GROWTH=1: Common for GTest's memory usage
    # -sNODERAWFS=1: Enables filesystem access in Node.js
    target_link_options(cortex_tests PRIVATE 
        "-sEXIT_RUNTIME=1"
        "-sALLOW_MEMORY_GROWTH=1"
        "-sNODERAWFS=1"
    )
else()
    # Native discovery still works fine
    include(GoogleTest)
    gtest_discover_tests(cortex_tests)
endif()
