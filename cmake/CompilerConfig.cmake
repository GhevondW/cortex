# Force C++ Standard
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Compiler Warnings (Clang/GCC friendly)
function(cortex_apply_warnings TARGET_NAME)
    target_compile_options(${TARGET_NAME} PRIVATE
        -Wall
        -Wextra
        -Wshadow
        -Wnon-virtual-dtor
        -Wold-style-cast
        -Wcast-align
        -Wunused
        -Woverloaded-virtual
        -Wpedantic
        -Wconversion
        -Wsign-conversion
    )
endfunction()

# Sanitizers (ASan, UBSan)
function(cortex_apply_sanitizers TARGET_NAME)
    if(CORTEX_USE_SANITIZERS)
        if(EMSCRIPTEN)
            message(STATUS "[${TARGET_NAME}] Enabling WASM Sanitizers")
            target_compile_options(${TARGET_NAME} PUBLIC -fsanitize=address -fsanitize=undefined)
            target_link_options(${TARGET_NAME} PUBLIC -fsanitize=address -fsanitize=undefined)
        else()
            message(STATUS "[${TARGET_NAME}] Enabling Native Sanitizers")
            target_compile_options(${TARGET_NAME} PUBLIC 
                -fsanitize=address 
                -fsanitize=undefined 
                -fno-omit-frame-pointer
            )
            target_link_options(${TARGET_NAME} PUBLIC 
                -fsanitize=address 
                -fsanitize=undefined
            )
        endif()
    endif()
endfunction()
