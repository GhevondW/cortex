if(NOT EMSCRIPTEN)
    message(STATUS "AlgoViz: Skipping (requires Emscripten)")
    return()
endif()

message(STATUS "AlgoViz: Configuring visualizers")

# Build flags are shared across apps; cortex_add_wasm_app_runtime lives in
# cmake/AppRuntime.cmake (included from the root CMakeLists).

cortex_add_wasm_app_runtime(
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
    engine/main.cpp
)
target_include_directories(algoviz_bst PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/engine)
target_link_libraries(algoviz_bst PRIVATE cortex::cortex)

cortex_add_wasm_app_runtime(
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
    engine/main.cpp
)
target_include_directories(algoviz_union_find PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/engine)
target_link_libraries(algoviz_union_find PRIVATE cortex::cortex)

add_custom_target(algo_viz_assets
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/web
            ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "AlgoViz: Copying web app to build directory"
)

add_dependencies(algo_viz_assets algoviz_bst algoviz_union_find)
add_custom_target(algo_viz DEPENDS algoviz_bst algoviz_union_find algo_viz_assets)
