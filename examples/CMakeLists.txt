if(EMSCRIPTEN)
    # WASM Example
    add_executable(wasm_example wasm/main.cpp)
    target_link_libraries(wasm_example PRIVATE cortex::cortex)
    
    # Set output to .js for Node.js and browser compatibility
    set_target_properties(wasm_example PROPERTIES SUFFIX ".js")
    
    target_link_options(wasm_example PRIVATE 
        "-sEXPORTED_FUNCTIONS=['_main','_start_coroutine_example','_resume_coroutine_example','_is_coroutine_done']"
        "-sEXPORTED_RUNTIME_METHODS=['ccall','cwrap']"
        "-sALLOW_MEMORY_GROWTH=1"
        "-sEXIT_RUNTIME=0"
        "-sASYNCIFY_STACK_SIZE=1048576"
        "-sASSERTIONS=1"
    )
    
    # Copy HTML file to build directory
    add_custom_command(TARGET wasm_example POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy
                ${CMAKE_CURRENT_SOURCE_DIR}/wasm/index.html
                ${CMAKE_CURRENT_BINARY_DIR}/index.html
        COMMENT "Copying index.html to build directory"
    )
    
    cortex_apply_warnings(wasm_example)
    cortex_apply_sanitizers(wasm_example)
else()
    # Native Examples
    add_executable(native_example linux/main.cpp)
    target_link_libraries(native_example PRIVATE cortex::cortex)
    cortex_apply_warnings(native_example)
    cortex_apply_sanitizers(native_example)

    add_executable(binary_tree_example linux/binary_tree.cpp)
    target_link_libraries(binary_tree_example PRIVATE cortex::cortex)
    cortex_apply_warnings(binary_tree_example)
    cortex_apply_sanitizers(binary_tree_example)
endif()
