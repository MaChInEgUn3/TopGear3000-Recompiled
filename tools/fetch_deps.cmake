# SPDX-License-Identifier: GPL-3.0-or-later

set(SDL3_TAG "release-3.4.16")

find_package(Git REQUIRED)

get_filename_component(REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(DEPS_DIR "${REPO_ROOT}/deps")
set(SDL3_DIR "${DEPS_DIR}/SDL3")

if(EXISTS "${SDL3_DIR}")
  execute_process(
    COMMAND "${GIT_EXECUTABLE}" -C "${SDL3_DIR}" describe --tags --exact-match
    RESULT_VARIABLE _git_res
    OUTPUT_VARIABLE _git_tag
    ERROR_VARIABLE _git_err
    OUTPUT_STRIP_TRAILING_WHITESPACE
  )
  if(_git_res EQUAL 0 AND _git_tag STREQUAL "${SDL3_TAG}")
    message(STATUS "already at ${SDL3_TAG}")
    return()
  else()
    message(FATAL_ERROR
      "deps/SDL3 already exists at '${SDL3_DIR}' but is not at tag ${SDL3_TAG} "
      "(got '${_git_tag}'). Please delete deps/SDL3 by hand and re-run.")
  endif()
endif()

file(MAKE_DIRECTORY "${DEPS_DIR}")

message(STATUS "Cloning SDL3 (${SDL3_TAG}) into ${SDL3_DIR}...")
execute_process(
  COMMAND "${GIT_EXECUTABLE}" clone --depth 1 --branch "${SDL3_TAG}"
    "https://github.com/libsdl-org/SDL.git" "${SDL3_DIR}"
  RESULT_VARIABLE _clone_res
)
if(NOT _clone_res EQUAL 0)
  message(FATAL_ERROR "Failed to clone SDL3 tag ${SDL3_TAG}")
endif()
