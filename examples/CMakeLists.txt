if(EMSCRIPTEN)
    # WASM Example
    add_executable(wasm_example wasm/main.cpp)
    target_link_libraries(wasm_example PRIVATE cortex::cortex)
    
    # Set output to .js for Node.js and browser compatibility
    set_target_properties(wasm_example PROPERTIES SUFFIX ".js")
    
    # Required linker flags for the example
    target_link_options(wasm_example PRIVATE 
        "-sEXPORTED_FUNCTIONS=['_main','_cortex_add']"
        "-sEXPORTED_RUNTIME_METHODS=['ccall','cwrap']"
        "-sALLOW_MEMORY_GROWTH=1"
        "-sEXIT_RUNTIME=1"
    )
    
    # Copy HTML file to build directory
    add_custom_command(TARGET wasm_example POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy
                ${CMAKE_CURRENT_SOURCE_DIR}/wasm/index.html
                ${CMAKE_CURRENT_BINARY_DIR}/index.html
        COMMENT "Copying index.html to build directory"
    )
    
    cortex_apply_warnings(wasm_example)
else()
    # Native Example
    add_executable(native_example linux/main.cpp)
    target_link_libraries(native_example PRIVATE cortex::cortex)
    cortex_apply_warnings(native_example)
endif()
