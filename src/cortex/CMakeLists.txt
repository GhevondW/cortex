add_library(cortex_lib
            include/cortex/api/suspendable.hpp
            include/cortex/api/flow.hpp
            include/cortex/basic_flow.hpp
            include/cortex/coroutine.hpp
            include/cortex/error.hpp
            include/cortex/execution.hpp
            include/cortex/machine_context.hpp
            include/cortex/naive_coroutine.hpp
            include/cortex/stack_allocator.hpp
            include/cortex/stack.hpp
            src/basic_flow.cpp
            src/coroutine.cpp
            src/execution.cpp
            src/machine_context.cpp
            src/naive_coroutine.cpp
            src/stack_allocator.cpp)

add_library(cortex::lib ALIAS cortex_lib)

target_link_libraries(cortex_lib PRIVATE cortex::options cortex::warnings)
target_link_system_libraries(cortex_lib PUBLIC Boost::context)
if (CORTEX_ENABLE_SANITIZER_ADDRESS)
    target_compile_definitions(cortex_lib PRIVATE BOOST_USE_ASAN)
endif ()

target_include_directories(cortex_lib ${WARNING_GUARD} PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/src/cortex/include>)

target_compile_features(cortex_lib PUBLIC cxx_std_20)

set_target_properties(
        cortex_lib
        PROPERTIES VERSION ${PROJECT_VERSION}
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN YES)
