if(NOT EMSCRIPTEN)
    message(STATUS "AlgoViz: Skipping (requires Emscripten)")
    return()
endif()

message(STATUS "AlgoViz: Configuring visualizers")

# --- Optimization flags based on build type ---
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    set(ALGOVIZ_OPT_FLAGS "-Os")
    set(ALGOVIZ_ASSERT_FLAG "-sASSERTIONS=0")
else()
    set(ALGOVIZ_OPT_FLAGS "-O0")
    set(ALGOVIZ_ASSERT_FLAG "-sASSERTIONS=1")
endif()

set(ALGOVIZ_COMMON_LINK_FLAGS
    "-sALLOW_MEMORY_GROWTH=1"
    "-sEXIT_RUNTIME=0"
    "-sASYNCIFY_STACK_SIZE=65536"
    "-sENVIRONMENT=web"
    "-sINCOMING_MODULE_JS_API=['locateFile','onAbort','onRuntimeInitialized']"
    "${ALGOVIZ_ASSERT_FLAG}"
    "${ALGOVIZ_OPT_FLAGS}"
)

function(cortex_add_algoviz_runtime TARGET_NAME OUTPUT_NAME EXPORTED_FUNCTIONS)
    add_executable(${TARGET_NAME} engine/main.cpp)

    target_include_directories(${TARGET_NAME} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/engine
    )

    target_link_libraries(${TARGET_NAME} PRIVATE cortex::cortex)

    set_target_properties(${TARGET_NAME} PROPERTIES
        SUFFIX ".js"
        OUTPUT_NAME "${OUTPUT_NAME}"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    )

    target_link_options(${TARGET_NAME} PRIVATE
        "-sEXPORTED_FUNCTIONS=${EXPORTED_FUNCTIONS}"
        ${ALGOVIZ_COMMON_LINK_FLAGS}
    )

    cortex_apply_warnings(${TARGET_NAME})
    cortex_apply_sanitizers(${TARGET_NAME})
endfunction()

cortex_add_algoviz_runtime(
    algoviz_bst
    "algoviz_bst"
    "[\
'_main',\
'_build_preset_tree',\
'_add_node',\
'_clear_tree',\
'_set_operation_value',\
'_start_algorithm',\
'_step_algorithm',\
'_is_algorithm_done',\
'_reset_algorithm',\
'_get_root_id',\
'_get_node_count',\
'_get_node_value',\
'_get_node_left',\
'_get_node_right',\
'_get_node_parent'\
]"
)

cortex_add_algoviz_runtime(
    algoviz_union_find
    "algoviz_union_find"
    "[\
'_main',\
'_uf_init',\
'_uf_count',\
'_uf_component_count',\
'_uf_get_parent',\
'_uf_get_rank',\
'_uf_get_set_size',\
'_uf_find',\
'_uf_union',\
'_uf_connected'\
]"
)

add_custom_target(algo_viz_assets
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/web
            ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "AlgoViz: Copying web app to build directory"
)

add_dependencies(algo_viz_assets algoviz_bst algoviz_union_find)
add_custom_target(algo_viz DEPENDS algoviz_bst algoviz_union_find algo_viz_assets)
