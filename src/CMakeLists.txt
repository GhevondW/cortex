add_library(cortex STATIC)

set(CORTEX_COROUTINE_SRC coroutine_native.cpp)

if (EMSCRIPTEN)
    set(CORTEX_COROUTINE_SRC coroutine_emscripten.cpp)
endif()

target_sources(cortex
        PRIVATE
        core.cpp
        ${CORTEX_COROUTINE_SRC}
)

# Includes
target_include_directories(cortex PUBLIC
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(cortex PUBLIC function2::function2)

# Dependencies
if(NOT EMSCRIPTEN)
    target_link_libraries(cortex PUBLIC Boost::context)
endif()

# Apply Modular Configuration
cortex_apply_warnings(cortex)
cortex_configure_platform(cortex)

# Alias
add_library(cortex::cortex ALIAS cortex)
