include(GenerateExportHeader)

add_library(cortex_lib
            include/cortex/cortex.hpp
            src/cortex.cpp)

add_library(cortex::lib ALIAS cortex_lib)

target_link_libraries(cortex_lib PRIVATE cortex_options cortex_warnings)

target_include_directories(cortex_lib ${WARNING_GUARD} PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/src/cortex/include>
        $<BUILD_INTERFACE:${PROJECT_BINARY_DIR}/include>)

target_compile_features(cortex_lib PUBLIC cxx_std_20)

set_target_properties(
        cortex_lib
        PROPERTIES VERSION ${PROJECT_VERSION}
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN YES)

generate_export_header(cortex_lib EXPORT_FILE_NAME ${PROJECT_BINARY_DIR}/include/cortex/cortex_lib_export.hpp)

if(NOT BUILD_SHARED_LIBS)
    target_compile_definitions(cortex_lib PUBLIC CORTEX_LIB_STATIC_DEFINE)
endif()
