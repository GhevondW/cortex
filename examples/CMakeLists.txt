if(EMSCRIPTEN)
    # Optimization flags based on build type
    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        set(WASM_OPTIMIZATION_FLAGS "-O3")
        set(WASM_ASSERTIONS_FLAG "-sASSERTIONS=0")
    else()
        set(WASM_OPTIMIZATION_FLAGS "-O0")
        set(WASM_ASSERTIONS_FLAG "-sASSERTIONS=1")
    endif()

    # WASM Example
    add_executable(wasm_example wasm/main.cpp)
    target_link_libraries(wasm_example PRIVATE cortex::cortex)
    
    # Set output to .js for Node.js and browser compatibility
    set_target_properties(wasm_example PROPERTIES SUFFIX ".js")
    
    target_link_options(wasm_example PRIVATE 
        "-sEXPORTED_FUNCTIONS=['_main','_start_coroutine_example','_resume_coroutine_example','_is_coroutine_done']"
        "-sEXPORTED_RUNTIME_METHODS=['ccall','cwrap']"
        "-sALLOW_MEMORY_GROWTH=1"
        "-sEXIT_RUNTIME=0"
        "-sASYNCIFY_STACK_SIZE=1048576"
        "${WASM_ASSERTIONS_FLAG}"
        "${WASM_OPTIMIZATION_FLAGS}"
    )
    
    # Copy HTML file to build directory
    add_custom_command(TARGET wasm_example POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy
                ${CMAKE_CURRENT_SOURCE_DIR}/wasm/index.html
                ${CMAKE_CURRENT_BINARY_DIR}/index.html
        COMMENT "Copying index.html to build directory"
    )
    
    cortex_apply_warnings(wasm_example)
    cortex_apply_sanitizers(wasm_example)

    # Particle Simulation Demo - demonstrates coroutine power
    add_executable(particle_simulation wasm/particle_simulation.cpp)
    target_link_libraries(particle_simulation PRIVATE cortex::cortex)
    
    set_target_properties(particle_simulation PROPERTIES SUFFIX ".js")
    
    target_link_options(particle_simulation PRIVATE 
        "-sEXPORTED_FUNCTIONS=['_main','_start_computation','_resume_computation','_is_computation_done','_get_current_iteration','_get_total_iterations']"
        "-sEXPORTED_RUNTIME_METHODS=['ccall','cwrap']"
        "-sALLOW_MEMORY_GROWTH=1"
        "-sEXIT_RUNTIME=0"
        "-sASYNCIFY_STACK_SIZE=1048576"
        "${WASM_ASSERTIONS_FLAG}"
        "${WASM_OPTIMIZATION_FLAGS}"
    )
    
    # Copy HTML file to build directory
    add_custom_command(TARGET particle_simulation POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy
                ${CMAKE_CURRENT_SOURCE_DIR}/wasm/particle_demo.html
                ${CMAKE_CURRENT_BINARY_DIR}/particle_demo.html
        COMMAND ${CMAKE_COMMAND} -E copy
                ${CMAKE_CURRENT_SOURCE_DIR}/wasm/examples_index.html
                ${CMAKE_CURRENT_BINARY_DIR}/examples_index.html
        COMMENT "Copying HTML files to build directory"
    )
    
    cortex_apply_warnings(particle_simulation)
    cortex_apply_sanitizers(particle_simulation)

    # Sudoku Solver Demo - demonstrates recursive backtracking with coroutines
    add_executable(sudoku_solver wasm/sudoku_solver.cpp)
    target_link_libraries(sudoku_solver PRIVATE cortex::cortex)
    
    set_target_properties(sudoku_solver PROPERTIES SUFFIX ".js")
    
    target_link_options(sudoku_solver PRIVATE 
        "-sEXPORTED_FUNCTIONS=['_main','_set_sudoku_board','_start_solving','_resume_solving','_is_solving_done','_get_cell_value','_is_cell_original','_reset_solver']"
        "-sEXPORTED_RUNTIME_METHODS=['ccall','cwrap']"
        "-sALLOW_MEMORY_GROWTH=1"
        "-sEXIT_RUNTIME=0"
        "-sASYNCIFY_STACK_SIZE=1048576"
        "${WASM_ASSERTIONS_FLAG}"
        "${WASM_OPTIMIZATION_FLAGS}"
    )
    
    # Copy HTML files to build directory
    add_custom_command(TARGET sudoku_solver POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy
                ${CMAKE_CURRENT_SOURCE_DIR}/wasm/sudoku_demo.html
                ${CMAKE_CURRENT_BINARY_DIR}/sudoku_demo.html
        COMMAND ${CMAKE_COMMAND} -E copy
                ${CMAKE_CURRENT_SOURCE_DIR}/wasm/examples_index.html
                ${CMAKE_CURRENT_BINARY_DIR}/examples_index.html
        COMMENT "Copying HTML files to build directory"
    )
    
    cortex_apply_warnings(sudoku_solver)
    cortex_apply_sanitizers(sudoku_solver)

    # Fiber Workflow Demo - demonstrates tiny_fiber cooperative multitasking
    add_executable(fiber_workflow wasm/fiber_workflow.cpp)
    target_link_libraries(fiber_workflow PRIVATE cortex::cortex)
    
    set_target_properties(fiber_workflow PROPERTIES SUFFIX ".js")
    
    target_link_options(fiber_workflow PRIVATE 
        "-sEXPORTED_FUNCTIONS=['_main','_start_workflow','_step_workflow','_is_workflow_done']"
        "-sEXPORTED_RUNTIME_METHODS=['ccall','cwrap']"
        "-sALLOW_MEMORY_GROWTH=1"
        "-sEXIT_RUNTIME=0"
        "-sASYNCIFY_STACK_SIZE=1048576"
        "${WASM_ASSERTIONS_FLAG}"
        "${WASM_OPTIMIZATION_FLAGS}"
    )
    
    # Copy HTML files to build directory
    add_custom_command(TARGET fiber_workflow POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy
                ${CMAKE_CURRENT_SOURCE_DIR}/wasm/fiber_demo.html
                ${CMAKE_CURRENT_BINARY_DIR}/fiber_demo.html
        COMMAND ${CMAKE_COMMAND} -E copy
                ${CMAKE_CURRENT_SOURCE_DIR}/wasm/examples_index.html
                ${CMAKE_CURRENT_BINARY_DIR}/examples_index.html
        COMMENT "Copying HTML files to build directory"
    )
    
    cortex_apply_warnings(fiber_workflow)
    cortex_apply_sanitizers(fiber_workflow)
else()
    add_executable(binary_tree_example linux/binary_tree.cpp)
    target_link_libraries(binary_tree_example PRIVATE cortex::cortex)
    cortex_apply_warnings(binary_tree_example)
    cortex_apply_sanitizers(binary_tree_example)
endif()
