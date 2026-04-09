cmake_minimum_required(VERSION 3.14)

include(cmake/prelude.cmake)

project(
    pgnlib
    VERSION 0.1.0
    DESCRIPTION "PGN parser library built on foonathan/lexy"
    HOMEPAGE_URL "https://sr.ht/~bogdanb/pgnlib/"
    LANGUAGES CXX
)

include(cmake/project-is-top-level.cmake)
include(cmake/variables.cmake)

# ---- Declare library ----

add_library(
    pgnlib_pgnlib
    source/pgnlib.cpp
)
add_library(pgnlib::pgnlib ALIAS pgnlib_pgnlib)

include(GenerateExportHeader)
generate_export_header(
    pgnlib_pgnlib
    BASE_NAME pgnlib
    EXPORT_FILE_NAME export/pgnlib/pgnlib_export.hpp
    CUSTOM_CONTENT_FROM_VARIABLE pragma_suppress_c4251
)

if(NOT BUILD_SHARED_LIBS)
  target_compile_definitions(pgnlib_pgnlib PUBLIC PGNLIB_STATIC_DEFINE)
endif()

set_target_properties(
    pgnlib_pgnlib PROPERTIES
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES
    VERSION "${PROJECT_VERSION}"
    SOVERSION "${PROJECT_VERSION_MAJOR}"
    EXPORT_NAME pgnlib
    OUTPUT_NAME pgnlib
)

target_include_directories(
    pgnlib_pgnlib ${warning_guard}
    PUBLIC
    "\$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>"
)

target_include_directories(
    pgnlib_pgnlib SYSTEM
    PUBLIC
    "\$<BUILD_INTERFACE:${PROJECT_BINARY_DIR}/export>"
)

target_compile_features(pgnlib_pgnlib PUBLIC cxx_std_20)

find_package(lexy REQUIRED)
find_package(tl-expected REQUIRED)
target_link_libraries(pgnlib_pgnlib
    PRIVATE foonathan::lexy
    PUBLIC  tl::expected
)

# ---- Install rules ----

if(NOT CMAKE_SKIP_INSTALL_RULES)
  include(cmake/install-rules.cmake)
endif()

# ---- Developer mode ----

if(NOT pgnlib_DEVELOPER_MODE)
  return()
elseif(NOT PROJECT_IS_TOP_LEVEL)
  message(
      AUTHOR_WARNING
      "Developer mode is intended for developers of pgnlib"
  )
endif()

include(cmake/dev-mode.cmake)
