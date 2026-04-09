set(pgnlib_FOUND YES)

include(CMakeFindDependencyMacro)
find_dependency(fmt)

if(pgnlib_FOUND)
  include("${CMAKE_CURRENT_LIST_DIR}/pgnlibTargets.cmake")
endif()
