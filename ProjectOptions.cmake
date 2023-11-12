include(cmake/SystemLink.cmake)
include(cmake/StaticAnalyzers.cmake)
include(CMakeDependentOption)
include(CheckCXXCompilerFlag)


macro(cortex_supports_sanitizers)
  if((CMAKE_CXX_COMPILER_ID MATCHES ".*Clang.*" OR CMAKE_CXX_COMPILER_ID MATCHES ".*GNU.*") AND NOT WIN32)
    set(SUPPORTS_UBSAN ON)
  else()
    set(SUPPORTS_UBSAN OFF)
  endif()

  if((CMAKE_CXX_COMPILER_ID MATCHES ".*Clang.*" OR CMAKE_CXX_COMPILER_ID MATCHES ".*GNU.*") AND WIN32)
    set(SUPPORTS_ASAN OFF)
  else()
    set(SUPPORTS_ASAN ON)
  endif()
endmacro()

macro(cortex_setup_options)
  option(CORTEX_ENABLE_HARDENING "Enable hardening" ON)
  option(CORTEX_ENABLE_COVERAGE "Enable coverage reporting" OFF)
  cmake_dependent_option(
    CORTEX_ENABLE_GLOBAL_HARDENING
    "Attempt to push hardening options to built dependencies"
    ON
    CORTEX_ENABLE_HARDENING
    OFF)

  cortex_supports_sanitizers()

  if(NOT PROJECT_IS_TOP_LEVEL OR CORTEX_PACKAGING_MAINTAINER_MODE)
    option(CORTEX_ENABLE_IPO "Enable IPO/LTO" OFF)
    option(CORTEX_WARNINGS_AS_ERRORS "Treat Warnings As Errors" OFF)
    option(CORTEX_ENABLE_USER_LINKER "Enable user-selected linker" OFF)
    option(CORTEX_ENABLE_SANITIZER_ADDRESS "Enable address sanitizer" OFF)
    option(CORTEX_ENABLE_SANITIZER_LEAK "Enable leak sanitizer" OFF)
    option(CORTEX_ENABLE_SANITIZER_UNDEFINED "Enable undefined sanitizer" OFF)
    option(CORTEX_ENABLE_SANITIZER_THREAD "Enable thread sanitizer" OFF)
    option(CORTEX_ENABLE_SANITIZER_MEMORY "Enable memory sanitizer" OFF)
    option(CORTEX_ENABLE_UNITY_BUILD "Enable unity builds" OFF)
    option(CORTEX_ENABLE_CLANG_TIDY "Enable clang-tidy" OFF)
    option(CORTEX_ENABLE_CPPCHECK "Enable cpp-check analysis" OFF)
    option(CORTEX_ENABLE_PCH "Enable precompiled headers" OFF)
    option(CORTEX_ENABLE_CACHE "Enable ccache" OFF)
  else()
    option(CORTEX_ENABLE_IPO "Enable IPO/LTO" OFF)
    option(CORTEX_WARNINGS_AS_ERRORS "Treat Warnings As Errors" ON)
    option(CORTEX_ENABLE_USER_LINKER "Enable user-selected linker" OFF)
    option(CORTEX_ENABLE_SANITIZER_ADDRESS "Enable address sanitizer" ${SUPPORTS_ASAN})
    option(CORTEX_ENABLE_SANITIZER_LEAK "Enable leak sanitizer" OFF)
    option(CORTEX_ENABLE_SANITIZER_UNDEFINED "Enable undefined sanitizer" ${SUPPORTS_UBSAN})
    option(CORTEX_ENABLE_SANITIZER_THREAD "Enable thread sanitizer" OFF)
    option(CORTEX_ENABLE_SANITIZER_MEMORY "Enable memory sanitizer" OFF)
    option(CORTEX_ENABLE_UNITY_BUILD "Enable unity builds" OFF)
    option(CORTEX_ENABLE_CLANG_TIDY "Enable clang-tidy" OFF)
    option(CORTEX_ENABLE_CPPCHECK "Enable cpp-check analysis" ON)
    option(CORTEX_ENABLE_PCH "Enable precompiled headers" OFF)
    option(CORTEX_ENABLE_CACHE "Enable ccache" ON)
  endif()

  if(NOT PROJECT_IS_TOP_LEVEL)
    mark_as_advanced(
      CORTEX_ENABLE_IPO
      CORTEX_WARNINGS_AS_ERRORS
      CORTEX_ENABLE_USER_LINKER
      CORTEX_ENABLE_SANITIZER_ADDRESS
      CORTEX_ENABLE_SANITIZER_LEAK
      CORTEX_ENABLE_SANITIZER_UNDEFINED
      CORTEX_ENABLE_SANITIZER_THREAD
      CORTEX_ENABLE_SANITIZER_MEMORY
      CORTEX_ENABLE_UNITY_BUILD
      CORTEX_ENABLE_CLANG_TIDY
      CORTEX_ENABLE_CPPCHECK
      CORTEX_ENABLE_COVERAGE
      CORTEX_ENABLE_PCH
      CORTEX_ENABLE_CACHE)
  endif()

endmacro()

macro(cortex_global_options)
  if(CORTEX_ENABLE_IPO)
    include(cmake/InterproceduralOptimization.cmake)
    cortex_enable_ipo()
  endif()

  cortex_supports_sanitizers()

  if(CORTEX_ENABLE_HARDENING AND CORTEX_ENABLE_GLOBAL_HARDENING)
    include(cmake/Hardening.cmake)
    if(NOT SUPPORTS_UBSAN 
       OR CORTEX_ENABLE_SANITIZER_UNDEFINED
       OR CORTEX_ENABLE_SANITIZER_ADDRESS
       OR CORTEX_ENABLE_SANITIZER_THREAD
       OR CORTEX_ENABLE_SANITIZER_LEAK)
      set(ENABLE_UBSAN_MINIMAL_RUNTIME FALSE)
    else()
      set(ENABLE_UBSAN_MINIMAL_RUNTIME TRUE)
    endif()
    message("${CORTEX_ENABLE_HARDENING} ${ENABLE_UBSAN_MINIMAL_RUNTIME} ${CORTEX_ENABLE_SANITIZER_UNDEFINED}")
    cortex_enable_hardening(cortex_options ON ${ENABLE_UBSAN_MINIMAL_RUNTIME})
  endif()
endmacro()

macro(cortex_local_options)
  if(PROJECT_IS_TOP_LEVEL)
    include(cmake/StandardProjectSettings.cmake)
  endif()

  add_library(cortex_warnings INTERFACE)
  add_library(cortex_options INTERFACE)

  cortex_enable_cppcheck(cortex_options ${CORTEX_WARNINGS_AS_ERRORS} "")

  include(cmake/CompilerWarnings.cmake)
  cortex_set_project_warnings(
    cortex_warnings
    ${CORTEX_WARNINGS_AS_ERRORS}
    ""
    ""
    ""
    "")

  if(CORTEX_ENABLE_USER_LINKER)
    include(cmake/Linker.cmake)
    configure_linker(cortex_options)
  endif()

  include(cmake/Sanitizers.cmake)
  cortex_enable_sanitizers(
    cortex_options
    ${CORTEX_ENABLE_SANITIZER_ADDRESS}
    ${CORTEX_ENABLE_SANITIZER_LEAK}
    ${CORTEX_ENABLE_SANITIZER_UNDEFINED}
    ${CORTEX_ENABLE_SANITIZER_THREAD}
    ${CORTEX_ENABLE_SANITIZER_MEMORY})

  set_target_properties(cortex_options PROPERTIES UNITY_BUILD ${CORTEX_ENABLE_UNITY_BUILD})

  if(CORTEX_ENABLE_PCH)
    target_precompile_headers(
      cortex_options
      INTERFACE
      <vector>
      <string>
      <utility>)
  endif()

  if(CORTEX_ENABLE_CACHE)
    include(cmake/Cache.cmake)
    cortex_enable_cache()
  endif()

  include(cmake/StaticAnalyzers.cmake)
  if(CORTEX_ENABLE_CLANG_TIDY)
    cortex_enable_clang_tidy(cortex_options ${CORTEX_WARNINGS_AS_ERRORS})
  endif()

  if(CORTEX_ENABLE_CPPCHECK)
    cortex_enable_cppcheck(${CORTEX_WARNINGS_AS_ERRORS} "" # override cppcheck options
    )
  endif()

  if(CORTEX_ENABLE_COVERAGE)
    include(cmake/Tests.cmake)
    cortex_enable_coverage(cortex_options)
  endif()

  if(CORTEX_WARNINGS_AS_ERRORS)
    check_cxx_compiler_flag("-Wl,--fatal-warnings" LINKER_FATAL_WARNINGS)
    if(LINKER_FATAL_WARNINGS)
      # This is not working consistently, so disabling for now
      # target_link_options(cortex_options INTERFACE -Wl,--fatal-warnings)
    endif()
  endif()

  if(CORTEX_ENABLE_HARDENING AND NOT CORTEX_ENABLE_GLOBAL_HARDENING)
    include(cmake/Hardening.cmake)
    if(NOT SUPPORTS_UBSAN 
       OR CORTEX_ENABLE_SANITIZER_UNDEFINED
       OR CORTEX_ENABLE_SANITIZER_ADDRESS
       OR CORTEX_ENABLE_SANITIZER_THREAD
       OR CORTEX_ENABLE_SANITIZER_LEAK)
      set(ENABLE_UBSAN_MINIMAL_RUNTIME FALSE)
    else()
      set(ENABLE_UBSAN_MINIMAL_RUNTIME TRUE)
    endif()
    cortex_enable_hardening(cortex_options OFF ${ENABLE_UBSAN_MINIMAL_RUNTIME})
  endif()

endmacro()
