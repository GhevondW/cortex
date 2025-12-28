function(cortex_configure_platform TARGET_NAME)
    if(EMSCRIPTEN)
        message(STATUS "[${TARGET_NAME}] Configuring for WebAssembly")
        
        # Asyncify is often needed for coroutine-like behavior in JS
        target_link_options(${TARGET_NAME} PUBLIC "-sASYNCIFY")
        
        # WASM platform macro
        target_compile_definitions(${TARGET_NAME} PUBLIC CORTEX_PLATFORM_WASM)
    else()
        message(STATUS "[${TARGET_NAME}] Configuring for Native (Linux/Clang)")
        
        # Static libraries usually need PIC
        set_property(TARGET ${TARGET_NAME} PROPERTY POSITION_INDEPENDENT_CODE ON)
        
        target_compile_definitions(${TARGET_NAME} PUBLIC CORTEX_PLATFORM_NATIVE)
    endif()
endfunction()
