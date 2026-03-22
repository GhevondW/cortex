if(NOT EMSCRIPTEN)
    message(STATUS "ImageEditor: Skipping (requires Emscripten)")
    return()
endif()

message(STATUS "ImageEditor: Configuring MVP app")

if(CMAKE_BUILD_TYPE STREQUAL "Release")
    set(IMAGE_EDITOR_OPT_FLAGS "-O3")
    set(IMAGE_EDITOR_ASSERT_FLAG "-sASSERTIONS=0")
else()
    set(IMAGE_EDITOR_OPT_FLAGS "-O0")
    set(IMAGE_EDITOR_ASSERT_FLAG "-sASSERTIONS=1")
endif()

add_executable(image_editor
    engine/main.cpp
    engine/editor_session.cpp
    engine/image_ops.cpp
)

target_include_directories(image_editor PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/engine
)

target_link_libraries(image_editor PRIVATE cortex::cortex)

set_target_properties(image_editor PROPERTIES
    SUFFIX ".js"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)

target_link_options(image_editor PRIVATE
    "-sEXPORTED_FUNCTIONS=[\
'_main',\
'_editor_init',\
'_editor_destroy',\
'_editor_set_source_rgba',\
'_editor_set_grayscale_amount',\
'_editor_set_blur_radius',\
'_editor_pump',\
'_editor_needs_pump',\
'_editor_get_status',\
'_editor_get_output_width',\
'_editor_get_output_height',\
'_editor_get_output_ptr',\
'_editor_get_output_size',\
'_editor_copy_output_rgba',\
'_editor_get_output_revision',\
'_editor_get_error_message',\
'_malloc',\
'_free'\
]"
    "-sEXPORTED_RUNTIME_METHODS=['ccall','cwrap']"
    "-sALLOW_MEMORY_GROWTH=1"
    "-sEXIT_RUNTIME=0"
    "-sASYNCIFY_STACK_SIZE=1048576"
    "${IMAGE_EDITOR_ASSERT_FLAG}"
    "${IMAGE_EDITOR_OPT_FLAGS}"
)

add_custom_command(TARGET image_editor POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/web
            ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "ImageEditor: Copying web app to build directory"
)

cortex_apply_warnings(image_editor)
cortex_apply_sanitizers(image_editor)
