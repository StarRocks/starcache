### install the starcache output

# include CMAKE_INSTALL_INCLUDEDIR definitions
include(GNUInstallDirs)

## install include files
install(
    DIRECTORY ${STARCACHE_SRC_DIR}/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/starcache
    FILES_MATCHING PATTERN "*.h*"
)

# install libraries
install(
    TARGETS ${PROJECT_NAME}
        starcache
    DESTINATION ${CMAKE_INSTALL_LIBDIR}
    EXPORT ${PROJECT_NAME}
)

include(CMakePackageConfigHelpers)
write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/starcacheConfigVersion.cmake
    VERSION 0.0.1
    COMPATIBILITY SameMajorVersion
)

set(STARCACHE_CMAKE_DIR ${CMAKE_INSTALL_LIBDIR}/cmake/starcache)

configure_package_config_file(
    ${PROJECT_SOURCE_DIR}/cmake/starcacheConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/starcacheConfig.cmake
    INSTALL_DESTINATION ${STARCACHE_CMAKE_DIR}
    PATH_VARS CMAKE_INSTALL_INCLUDEDIR CMAKE_INSTALL_LIBDIR
)

install(FILES ${CMAKE_CURRENT_BINARY_DIR}/starcacheConfig.cmake
              ${CMAKE_CURRENT_BINARY_DIR}/starcacheConfigVersion.cmake
              DESTINATION ${STARCACHE_CMAKE_DIR}
)
