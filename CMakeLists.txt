cmake_minimum_required(VERSION 3.16)

project(dgcolor VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(DGCOLOR_BUILD_TESTS "Build test targets" ON)
option(DGCOLOR_BUILD_BENCHMARKS "Build benchmark smoke targets" ON)
option(DGCOLOR_WARNINGS_AS_ERRORS "Treat warnings as errors" OFF)

add_library(dgcolor_project_options INTERFACE)
target_compile_features(dgcolor_project_options INTERFACE cxx_std_17)
target_include_directories(dgcolor_project_options INTERFACE "${CMAKE_SOURCE_DIR}/include")

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
  target_compile_options(dgcolor_project_options INTERFACE -Wall -Wextra -Wpedantic)
  if(DGCOLOR_WARNINGS_AS_ERRORS)
    target_compile_options(dgcolor_project_options INTERFACE -Werror)
  endif()
elseif(MSVC)
  target_compile_options(dgcolor_project_options INTERFACE /W4)
  if(DGCOLOR_WARNINGS_AS_ERRORS)
    target_compile_options(dgcolor_project_options INTERFACE /WX)
  endif()
endif()

# Conservative ParlayLib include discovery:
# 1) Use PARLAYLIB_DIR if provided.
# 2) Optionally use external/parlaylib if it exists.
set(DGCOLOR_PARLAY_INCLUDE_DIR "")
set(_parlay_candidates "")

if(DEFINED PARLAYLIB_DIR AND NOT PARLAYLIB_DIR STREQUAL "")
  list(APPEND _parlay_candidates "${PARLAYLIB_DIR}" "${PARLAYLIB_DIR}/include")
endif()

if(EXISTS "${CMAKE_SOURCE_DIR}/external/parlaylib")
  list(APPEND _parlay_candidates
    "${CMAKE_SOURCE_DIR}/external/parlaylib"
    "${CMAKE_SOURCE_DIR}/external/parlaylib/include"
  )
endif()

foreach(_candidate IN LISTS _parlay_candidates)
  if(EXISTS "${_candidate}/parlay/primitives.h")
    set(DGCOLOR_PARLAY_INCLUDE_DIR "${_candidate}")
    break()
  endif()
endforeach()

add_library(dgcolor_parlay INTERFACE)
if(NOT DGCOLOR_PARLAY_INCLUDE_DIR STREQUAL "")
  message(STATUS "ParlayLib include directory found at: ${DGCOLOR_PARLAY_INCLUDE_DIR}")
  target_include_directories(dgcolor_parlay INTERFACE "${DGCOLOR_PARLAY_INCLUDE_DIR}")
  target_compile_definitions(dgcolor_parlay INTERFACE DGCOLOR_HAS_PARLAYLIB=1)
else()
  message(WARNING
    "ParlayLib was not found. Configure with -DPARLAYLIB_DIR=/path/to/parlaylib "
    "or place ParlayLib at external/parlaylib. Foundation step still configures, "
    "but targets requiring ParlayLib headers may fail in later steps."
  )
  target_compile_definitions(dgcolor_parlay INTERFACE DGCOLOR_HAS_PARLAYLIB=0)
endif()

if(DGCOLOR_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

if(DGCOLOR_BUILD_BENCHMARKS)
  add_subdirectory(benchmarks)
endif()
