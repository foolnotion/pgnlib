cmake_minimum_required(VERSION 3.14)

project(pgnlibTests LANGUAGES CXX)

include(../cmake/project-is-top-level.cmake)
include(../cmake/folders.cmake)

# ---- Dependencies ----

if(PROJECT_IS_TOP_LEVEL)
  find_package(pgnlib REQUIRED)
  enable_testing()
endif()

find_package(Catch2 REQUIRED)
include(Catch)

# ---- Tests ----

add_executable(pgnlib_test source/pgnlib_test.cpp)
target_link_libraries(
    pgnlib_test PRIVATE
    pgnlib::pgnlib
    Catch2::Catch2WithMain
)
target_compile_features(pgnlib_test PRIVATE cxx_std_20)
target_compile_definitions(pgnlib_test PRIVATE
    PGNLIB_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/pgn"
)

catch_discover_tests(pgnlib_test)

# ---- End-of-file commands ----

add_folders(Test)
