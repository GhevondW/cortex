cmake_minimum_required(VERSION 3.15...3.23)

project(CmakeConfigPackageTests LANGUAGES CXX)

# ---- Test as standalone project the exported config package ----

if(PROJECT_IS_TOP_LEVEL OR TEST_INSTALLED_VERSION)
  enable_testing()

  find_package(cortex CONFIG REQUIRED) # for intro, project_options, ...

  if(NOT TARGET cortex_options)
    message(FATAL_ERROR "Requiered config package not found!")
    return() # be strictly paranoid for Template Janitor github action! CK
  endif()
endif()

function(add_cortex_test target_name source_file)
  add_executable(${target_name} ${source_file})
  target_link_libraries(
    ${target_name}
    PRIVATE cortex::cortex_options
            Boost::unit_test_framework
            cortex::lib)

  if(WIN32 AND BUILD_SHARED_LIBS)
    add_custom_command(
      TARGET ${target_name}
      PRE_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_RUNTIME_DLLS:${target_name}> $<TARGET_FILE_DIR:${target_name}>
      COMMAND_EXPAND_LISTS)
  endif()

  add_test(NAME ${target_name} COMMAND ${target_name})
endfunction()

add_cortex_test(just_works_test just_works_test.cpp)
add_cortex_test(memory_leak_test memory_leak_test.cpp)
add_cortex_test(nested_execution_test nested_execution_test.cpp)
add_cortex_test(stack_allocator_test stack_allocator_test.cpp)
