include(CMakeFindDependencyMacro)

# lexy is a PRIVATE link dep; for static archives consumers must be able to
# resolve $<LINK_ONLY:foonathan::lexy> in INTERFACE_LINK_LIBRARIES.
find_dependency(lexy)
find_dependency(tl-expected)

include("${CMAKE_CURRENT_LIST_DIR}/pgnlibTargets.cmake")
