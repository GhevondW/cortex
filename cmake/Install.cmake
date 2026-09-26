# Install rules and CMake package config for find_package(cortex).
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

set(CORTEX_INSTALL_CMAKEDIR "${CMAKE_INSTALL_LIBDIR}/cmake/cortex")

install(TARGETS cortex
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
# directory so it cannot clash with a separately installed function2; the
# exported cortex target adds that directory to its include path.
install(DIRECTORY "${function2_SOURCE_DIR}/include/function2"
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/cortex/third_party"
)

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
