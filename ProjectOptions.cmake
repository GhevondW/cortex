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
  option(CORTEX_ENABLE_COVERAGE "Enable coverage reporting" OFF)

  cortex_supports_sanitizers()

  if(PROJECT_IS_TOP_LEVEL)
    option(CORTEX_WARNINGS_AS_ERRORS "Treat Warnings As Errors" ON)
    option(CORTEX_ENABLE_SANITIZER_ADDRESS "Enable address sanitizer" ${SUPPORTS_ASAN})
    option(CORTEX_ENABLE_SANITIZER_LEAK "Enable leak sanitizer" ON)
    option(CORTEX_ENABLE_SANITIZER_UNDEFINED "Enable undefined sanitizer" ${SUPPORTS_UBSAN})
    option(CORTEX_ENABLE_SANITIZER_THREAD "Enable thread sanitizer" OFF)
    option(CORTEX_ENABLE_SANITIZER_MEMORY "Enable memory sanitizer" OFF)
    option(CORTEX_ENABLE_CLANG_TIDY "Enable clang-tidy" ON)
    option(CORTEX_ENABLE_CPPCHECK "Enable cpp-check analysis" ON)
    option(CORTEX_ENABLE_PCH "Enable precompiled headers" OFF)
    option(CORTEX_ENABLE_CACHE "Enable ccache" ON)
    option(CORTEX_BUILD_TESTING "Enable testing" ON)
  else()
    option(CORTEX_WARNINGS_AS_ERRORS "Treat Warnings As Errors" OFF)
    option(CORTEX_ENABLE_SANITIZER_ADDRESS "Enable address sanitizer" OFF)
    option(CORTEX_ENABLE_SANITIZER_LEAK "Enable leak sanitizer" OFF)
    option(CORTEX_ENABLE_SANITIZER_UNDEFINED "Enable undefined sanitizer" OFF)
    option(CORTEX_ENABLE_SANITIZER_THREAD "Enable thread sanitizer" OFF)
    option(CORTEX_ENABLE_SANITIZER_MEMORY "Enable memory sanitizer" OFF)
    option(CORTEX_ENABLE_CLANG_TIDY "Enable clang-tidy" OFF)
    option(CORTEX_ENABLE_CPPCHECK "Enable cpp-check analysis" OFF)
    option(CORTEX_ENABLE_PCH "Enable precompiled headers" OFF)
    option(CORTEX_ENABLE_CACHE "Enable ccache" OFF)
  endif()

  message(STATUS "CORTEX_WARNINGS_AS_ERRORS: ${CORTEX_WARNINGS_AS_ERRORS}")
  message(STATUS "CORTEX_ENABLE_SANITIZER_ADDRESS: ${CORTEX_ENABLE_SANITIZER_ADDRESS}")
  message(STATUS "CORTEX_ENABLE_SANITIZER_LEAK: ${CORTEX_ENABLE_SANITIZER_LEAK}")
  message(STATUS "CORTEX_ENABLE_SANITIZER_UNDEFINED: ${CORTEX_ENABLE_SANITIZER_UNDEFINED}")
  message(STATUS "CORTEX_ENABLE_SANITIZER_THREAD: ${CORTEX_ENABLE_SANITIZER_THREAD}")
  message(STATUS "CORTEX_ENABLE_SANITIZER_MEMORY: ${CORTEX_ENABLE_SANITIZER_MEMORY}")
  message(STATUS "CORTEX_ENABLE_CLANG_TIDY: ${CORTEX_ENABLE_CLANG_TIDY}")
  message(STATUS "CORTEX_ENABLE_CPPCHECK: ${CORTEX_ENABLE_CPPCHECK}")
  message(STATUS "CORTEX_ENABLE_PCH: ${CORTEX_ENABLE_PCH}")
  message(STATUS "CORTEX_ENABLE_CACHE: ${CORTEX_ENABLE_CACHE}")

  if(NOT PROJECT_IS_TOP_LEVEL)
    mark_as_advanced(
      CORTEX_WARNINGS_AS_ERRORS
      CORTEX_ENABLE_USER_LINKER
      CORTEX_ENABLE_SANITIZER_ADDRESS
      CORTEX_ENABLE_SANITIZER_LEAK
      CORTEX_ENABLE_SANITIZER_UNDEFINED
      CORTEX_ENABLE_SANITIZER_THREAD
      CORTEX_ENABLE_SANITIZER_MEMORY
      CORTEX_ENABLE_CLANG_TIDY
      CORTEX_ENABLE_CPPCHECK
      CORTEX_ENABLE_COVERAGE
      CORTEX_ENABLE_PCH
      CORTEX_ENABLE_CACHE)
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

  include(cmake/Sanitizers.cmake)
  cortex_enable_sanitizers(
    cortex_options
    ${CORTEX_ENABLE_SANITIZER_ADDRESS}
    ${CORTEX_ENABLE_SANITIZER_LEAK}
    ${CORTEX_ENABLE_SANITIZER_UNDEFINED}
    ${CORTEX_ENABLE_SANITIZER_THREAD}
    ${CORTEX_ENABLE_SANITIZER_MEMORY})

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

endmacro()
