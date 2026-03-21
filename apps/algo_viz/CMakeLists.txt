if(NOT EMSCRIPTEN)
    message(STATUS "AlgoViz: Skipping (requires Emscripten)")
    return()
endif()

message(STATUS "AlgoViz: Configuring visualizers")

# --- Optimization flags based on build type ---
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    set(ALGOVIZ_OPT_FLAGS "-O3")
    set(ALGOVIZ_ASSERT_FLAG "-sASSERTIONS=0")
else()
    set(ALGOVIZ_OPT_FLAGS "-O0")
    set(ALGOVIZ_ASSERT_FLAG "-sASSERTIONS=1")
endif()

# --- Target ---
add_executable(algo_viz engine/main.cpp)

target_include_directories(algo_viz PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/engine
)

target_link_libraries(algo_viz PRIVATE cortex::cortex)

set_target_properties(algo_viz PROPERTIES
    SUFFIX ".js"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)

# --- WASM link options ---
target_link_options(algo_viz PRIVATE
    "-sEXPORTED_FUNCTIONS=[\
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
'_get_node_parent',\
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
    "-sEXPORTED_RUNTIME_METHODS=['ccall','cwrap']"
    "-sALLOW_MEMORY_GROWTH=1"
    "-sEXIT_RUNTIME=0"
    "-sASYNCIFY_STACK_SIZE=1048576"
    "${ALGOVIZ_ASSERT_FLAG}"
    "${ALGOVIZ_OPT_FLAGS}"
)

# --- Copy complete web app to build directory ---
add_custom_command(TARGET algo_viz POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/web
            ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "AlgoViz: Copying web app to build directory"
)

# --- Apply project-wide settings ---
cortex_apply_warnings(algo_viz)
cortex_apply_sanitizers(algo_viz)
