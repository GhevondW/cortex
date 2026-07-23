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

            # Boost.Context sanitizer support (ucontext backend +
            # BOOST_USE_UCONTEXT + BOOST_USE_ASAN) is configured on the
            # boost_context target in cmake/Dependencies.cmake and propagates
            # to every consumer through Boost::context's PUBLIC definitions.
            # (Defining BOOST_USE_ASAN only here was a no-op: without
            # BOOST_USE_UCONTEXT the fcontext backend is used, which contains
            # no sanitizer annotations at all, and BOOST_USE_UBSAN is not
            # referenced anywhere in Boost.Context.)
        endif()
    endif()
endfunction()
