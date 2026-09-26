# Install rules and CMake package config for find_package(cortex).
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

set(CORTEX_INSTALL_CMAKEDIR "${CMAKE_INSTALL_LIBDIR}/cmake/cortex")

set(CORTEX_INSTALL_TARGETS cortex)
if(TARGET cortex_web)
    list(APPEND CORTEX_INSTALL_TARGETS cortex_web)
    set_target_properties(cortex_web PROPERTIES EXPORT_NAME web)
endif()

install(TARGETS ${CORTEX_INSTALL_TARGETS}
    EXPORT cortexTargets
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    INCLUDES DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
)

install(DIRECTORY "${PROJECT_SOURCE_DIR}/include/cortex"
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
)

# function2 is header-only and, when fetched by CPM, an imported target that
# cannot be exported. Ship its header next to ours, under a cortex-owned
# directory that the exported cortex target adds to its include path. (A
# consumer that uses its own function2 as well gets whichever copy comes first
# on its include path.)
set(_cortex_function2_dirs "")
if(function2_SOURCE_DIR)
    list(APPEND _cortex_function2_dirs "${function2_SOURCE_DIR}/include")
endif()
get_target_property(_cortex_function2_includes function2::function2 INTERFACE_INCLUDE_DIRECTORIES)
if(_cortex_function2_includes)
    list(APPEND _cortex_function2_dirs ${_cortex_function2_includes})
endif()
set(_cortex_function2_header_dir "")
foreach(_dir IN LISTS _cortex_function2_dirs)
    if(EXISTS "${_dir}/function2/function2.hpp")
        set(_cortex_function2_header_dir "${_dir}/function2")
        break()
    endif()
endforeach()
if(NOT _cortex_function2_header_dir)
    message(FATAL_ERROR "cortex: cannot find function2/function2.hpp to install it")
endif()
install(DIRECTORY "${_cortex_function2_header_dir}"
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/cortex/third_party"
)

# The browser driver (js/cortex.mjs); find_package() sets cortex_JS_DRIVER.
set(CORTEX_INSTALL_JSDIR "${CMAKE_INSTALL_DATADIR}/cortex")
install(FILES "${PROJECT_SOURCE_DIR}/js/cortex.mjs" DESTINATION "${CORTEX_INSTALL_JSDIR}")

install(EXPORT cortexTargets
    NAMESPACE cortex::
    DESTINATION "${CORTEX_INSTALL_CMAKEDIR}"
)

if(EMSCRIPTEN)
    set(CORTEX_CONFIG_NEEDS_BOOST OFF)
else()
    set(CORTEX_CONFIG_NEEDS_BOOST ON)
endif()

configure_package_config_file(
    "${CMAKE_CURRENT_LIST_DIR}/cortexConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/cortexConfig.cmake"
    INSTALL_DESTINATION "${CORTEX_INSTALL_CMAKEDIR}"
    PATH_VARS CORTEX_INSTALL_JSDIR
)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/cortexConfigVersion.cmake"
    VERSION "${PROJECT_VERSION}"
    COMPATIBILITY SameMajorVersion
)
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/cortexConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/cortexConfigVersion.cmake"
    DESTINATION "${CORTEX_INSTALL_CMAKEDIR}"
)
