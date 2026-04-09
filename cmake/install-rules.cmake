if(PROJECT_IS_TOP_LEVEL)
  set(
      CMAKE_INSTALL_INCLUDEDIR "include/pgnlib-${PROJECT_VERSION}"
      CACHE STRING ""
  )
  set_property(CACHE CMAKE_INSTALL_INCLUDEDIR PROPERTY TYPE PATH)
endif()

include(CMakePackageConfigHelpers)
include(GNUInstallDirs)

# find_package(<package>) call for consumers to find this project
# should match the name of variable set in the install-config.cmake script
set(package pgnlib)

install(
    DIRECTORY
    include/
    "${PROJECT_BINARY_DIR}/export/"
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
    COMPONENT pgnlib_Development
)

install(
    TARGETS pgnlib_pgnlib
    EXPORT pgnlibTargets
    RUNTIME #
    COMPONENT pgnlib_Runtime
    LIBRARY #
    COMPONENT pgnlib_Runtime
    NAMELINK_COMPONENT pgnlib_Development
    ARCHIVE #
    COMPONENT pgnlib_Development
    INCLUDES #
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
)

write_basic_package_version_file(
    "${package}ConfigVersion.cmake"
    COMPATIBILITY SameMajorVersion
)

# Allow package maintainers to freely override the path for the configs
set(
    pgnlib_INSTALL_CMAKEDIR "${CMAKE_INSTALL_LIBDIR}/cmake/${package}"
    CACHE STRING "CMake package config location relative to the install prefix"
)
set_property(CACHE pgnlib_INSTALL_CMAKEDIR PROPERTY TYPE PATH)
mark_as_advanced(pgnlib_INSTALL_CMAKEDIR)

install(
    FILES cmake/install-config.cmake
    DESTINATION "${pgnlib_INSTALL_CMAKEDIR}"
    RENAME "${package}Config.cmake"
    COMPONENT pgnlib_Development
)

install(
    FILES "${PROJECT_BINARY_DIR}/${package}ConfigVersion.cmake"
    DESTINATION "${pgnlib_INSTALL_CMAKEDIR}"
    COMPONENT pgnlib_Development
)

install(
    EXPORT pgnlibTargets
    NAMESPACE pgnlib::
    DESTINATION "${pgnlib_INSTALL_CMAKEDIR}"
    COMPONENT pgnlib_Development
)

if(PROJECT_IS_TOP_LEVEL)
  include(CPack)
endif()
