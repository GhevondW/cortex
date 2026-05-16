# Shared helper for building WASM applications under apps/.
#
# Each app builds a single Emscripten executable that exposes a small C ABI
# to JavaScript and ships a static web bundle alongside. The build flags are
# common across apps, so we encapsulate them here.

if(NOT EMSCRIPTEN)
    # Native builds skip apps entirely; the helper is a no-op so callers
    # don't need to wrap their cortex_add_wasm_app_runtime() calls.
    function(cortex_add_wasm_app_runtime)
    endfunction()
    return()
endif()

if(CMAKE_BUILD_TYPE STREQUAL "Release")
    set(_CORTEX_APP_OPT_FLAGS "-Os")
    set(_CORTEX_APP_ASSERT_FLAG "-sASSERTIONS=0")
else()
    set(_CORTEX_APP_OPT_FLAGS "-O0")
    set(_CORTEX_APP_ASSERT_FLAG "-sASSERTIONS=1")
endif()

set(CORTEX_WASM_APP_LINK_FLAGS
    "-sALLOW_MEMORY_GROWTH=1"
    "-sEXIT_RUNTIME=0"
    "-sASYNCIFY_STACK_SIZE=65536"
    "-sENVIRONMENT=web"
    "-sINCOMING_MODULE_JS_API=['locateFile','onAbort','onRuntimeInitialized']"
    "-sEXPORTED_RUNTIME_METHODS=['UTF8ToString']"
    "${_CORTEX_APP_ASSERT_FLAG}"
    "${_CORTEX_APP_OPT_FLAGS}"
)

# cortex_add_wasm_app_runtime(TARGET OUTPUT_NAME EXPORTED_FUNCTIONS SOURCES...)
#
# TARGET             — CMake target name for the executable.
# OUTPUT_NAME        — Basename of the produced .js / .wasm files.
# EXPORTED_FUNCTIONS — String value for -sEXPORTED_FUNCTIONS=[...]
#                     (e.g. "['_main','_my_func']"). Function names need a
#                     leading underscore.
# SOURCES            — One or more .cpp files; passed to add_executable.
function(cortex_add_wasm_app_runtime TARGET OUTPUT_NAME EXPORTED_FUNCTIONS)
    set(_sources ${ARGN})
    if(NOT _sources)
        message(FATAL_ERROR
            "cortex_add_wasm_app_runtime(${TARGET}): no source files provided")
    endif()

    add_executable(${TARGET} ${_sources})

    set_target_properties(${TARGET} PROPERTIES
        SUFFIX ".js"
        OUTPUT_NAME "${OUTPUT_NAME}"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    )

    target_link_options(${TARGET} PRIVATE
        "-sEXPORTED_FUNCTIONS=${EXPORTED_FUNCTIONS}"
        ${CORTEX_WASM_APP_LINK_FLAGS}
    )

    cortex_apply_warnings(${TARGET})
    cortex_apply_sanitizers(${TARGET})
endfunction()
