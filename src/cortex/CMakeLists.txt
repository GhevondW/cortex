add_library(cortex_lib
            include/cortex/api/disabler.hpp
            include/cortex/api/flow.hpp
            include/cortex/basic_flow.hpp
            include/cortex/error.hpp
            include/cortex/execution.hpp
            include/cortex/machine_context.hpp
            include/cortex/stack_allocator.hpp
            include/cortex/stack.hpp
            src/basic_flow.cpp
            src/execution.cpp
            src/machine_context.cpp
            src/stack_allocator.cpp)

add_library(cortex::lib ALIAS cortex_lib)

target_link_libraries(cortex_lib PRIVATE cortex_options cortex_warnings)
target_link_system_libraries(cortex_lib PUBLIC Boost::context)

target_include_directories(cortex_lib ${WARNING_GUARD} PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/src/cortex/include>)

target_compile_features(cortex_lib PUBLIC cxx_std_20)

set_target_properties(
        cortex_lib
        PROPERTIES VERSION ${PROJECT_VERSION}
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN YES)
