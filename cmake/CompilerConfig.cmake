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

# Link-time optimization (IPO) — lets the compiler inline across the
# library/application boundary, which matters for the small hot functions
# on the coroutine switch path.
function(cortex_apply_lto TARGET_NAME)
    if(CORTEX_ENABLE_LTO)
        include(CheckIPOSupported)
        check_ipo_supported(RESULT ipo_supported OUTPUT ipo_error)
        if(ipo_supported)
            message(STATUS "[${TARGET_NAME}] Enabling LTO")
            set_property(TARGET ${TARGET_NAME} PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
        else()
            message(WARNING "[${TARGET_NAME}] LTO requested but not supported: ${ipo_error}")
        endif()
    endif()
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

            # Boost.Context requires these macros to be defined when using sanitizers
            # to properly notify the sanitizer about stack switches.
            target_compile_definitions(${TARGET_NAME} PUBLIC 
                BOOST_USE_ASAN
                BOOST_USE_UBSAN
            )
        endif()
    endif()
endfunction()
