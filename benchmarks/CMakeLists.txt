add_executable(cortex_bench bench_main.cpp)

target_link_libraries(cortex_bench PRIVATE cortex::cortex)

cortex_apply_warnings(cortex_bench)
