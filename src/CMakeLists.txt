add_library(cortex STATIC)

if (EMSCRIPTEN)
    set(CORTEX_COROUTINE_SRC detail/coroutine_emscripten_impl.cpp)
else ()
    set(CORTEX_COROUTINE_SRC detail/coroutine_native_impl.cpp)
endif()

target_sources(cortex
        PRIVATE
        base_coroutine.cpp
        core.cpp
        coroutine.cpp
        memory_resource.cpp
        ${CORTEX_COROUTINE_SRC}
)

# Includes
target_include_directories(cortex PUBLIC
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_include_directories(cortex PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(cortex PUBLIC function2::function2)

# Dependencies
if(NOT EMSCRIPTEN)
    target_link_libraries(cortex PUBLIC Boost::context)
endif()

# Apply Modular Configuration
cortex_apply_warnings(cortex)
cortex_apply_sanitizers(cortex)
cortex_configure_platform(cortex)

# Alias
add_library(cortex::cortex ALIAS cortex)
