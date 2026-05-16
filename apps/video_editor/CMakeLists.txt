message(STATUS "VideoEditor: Configuring engine library")

# --- Engine static library (built both natively and under Emscripten) ---
add_library(video_editor_engine STATIC
    engine/src/frame_buffer.cpp
    engine/src/procedural_source.cpp
    engine/src/filters/brightness.cpp
    engine/src/filters/contrast.cpp
    engine/src/filters/saturation.cpp
    engine/src/filters/gaussian_blur.cpp
    engine/src/filter_chain.cpp
    engine/src/pipeline.cpp
    engine/src/blocking_runner.cpp
    engine/src/cooperative_runner.cpp
    engine/src/editor.cpp
)

target_include_directories(video_editor_engine PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/engine/include
)

target_link_libraries(video_editor_engine PUBLIC cortex::cortex)

cortex_apply_warnings(video_editor_engine)
cortex_apply_sanitizers(video_editor_engine)

# --- WASM executable + web assets (Emscripten only) ---
if(EMSCRIPTEN)
    message(STATUS "VideoEditor: Configuring WASM bridge + web bundle")

    cortex_add_wasm_app_runtime(
        video_editor_runtime
        "video_editor"
        "[\
'_main',\
'_editor_init',\
'_editor_shutdown',\
'_editor_get_width',\
'_editor_get_height',\
'_editor_get_frame_count',\
'_editor_get_source_frame',\
'_editor_get_output_frame',\
'_editor_set_brightness',\
'_editor_set_contrast',\
'_editor_set_saturation',\
'_editor_set_blur_radius',\
'_editor_render_preview',\
'_editor_start_apply_cooperative',\
'_editor_apply_blocking',\
'_editor_step',\
'_editor_get_progress',\
'_editor_cancel'\
]"
        engine/bridge/wasm_bridge.cpp
    )
    target_link_libraries(video_editor_runtime PRIVATE video_editor_engine)

    add_custom_target(video_editor_assets
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                ${CMAKE_CURRENT_SOURCE_DIR}/web
                ${CMAKE_CURRENT_BINARY_DIR}
        COMMENT "VideoEditor: Copying web app to build directory"
    )
    add_dependencies(video_editor_assets video_editor_runtime)

    add_custom_target(video_editor DEPENDS video_editor_runtime video_editor_assets)
endif()

# --- Native unit tests ---
if(CORTEX_BUILD_TESTS AND NOT EMSCRIPTEN)
    message(STATUS "VideoEditor: Configuring native engine tests")

    add_executable(video_editor_engine_test
        engine/tests/filters_test.cpp
        engine/tests/filter_chain_test.cpp
        engine/tests/pipeline_test.cpp
        engine/tests/runners_test.cpp
    )
    target_link_libraries(video_editor_engine_test PRIVATE
        video_editor_engine
        GTest::gtest_main
    )
    cortex_apply_warnings(video_editor_engine_test)
    cortex_apply_sanitizers(video_editor_engine_test)

    include(GoogleTest)
    gtest_discover_tests(video_editor_engine_test)
endif()
