add_library(cortex STATIC)

# Sources
target_sources(cortex PRIVATE
    core.cpp
)

# Includes
target_include_directories(cortex PUBLIC
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

# Dependencies
if(NOT EMSCRIPTEN)
    target_link_libraries(cortex PUBLIC Boost::context)
endif()

# Apply Modular Configuration
cortex_apply_warnings(cortex)
cortex_configure_platform(cortex)

# Alias
add_library(cortex::cortex ALIAS cortex)
