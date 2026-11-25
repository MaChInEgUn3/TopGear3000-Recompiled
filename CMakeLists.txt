# SPDX-License-Identifier: GPL-3.0-or-later

cmake_minimum_required(VERSION 3.24)
project(tg3000_port LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

if(MSVC)
  message(FATAL_ERROR
    "MSVC is not supported because the decomp C uses __builtin_trap(). "
    "Use GCC or Clang (e.g. MinGW-w64 UCRT on Windows).")
endif()

file(GLOB TG3000_DECOMP_SOURCES CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/extern/tg3000-decomp/src/*.c"
)
if(NOT TG3000_DECOMP_SOURCES)
  message(FATAL_ERROR
    "extern/tg3000-decomp/src is empty. Run: git submodule update --init")
endif()

add_library(tg3000_logic STATIC ${TG3000_DECOMP_SOURCES})
target_include_directories(tg3000_logic PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}/extern/tg3000-decomp/src"
)
target_compile_options(tg3000_logic PRIVATE -Wall -Wextra)

option(TG3000_SYSTEM_SDL "Use system-installed SDL3 instead of deps/SDL3" OFF)
if(TG3000_SYSTEM_SDL)
  find_package(SDL3 REQUIRED CONFIG)
else()
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/deps/SDL3/CMakeLists.txt")
    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON CACHE BOOL "" FORCE)
    set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
    set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/deps/SDL3" EXCLUDE_FROM_ALL)
  else()
    message(FATAL_ERROR
      "SDL3 not found in deps/SDL3. "
      "Run: cmake -P tools/fetch_deps.cmake (or configure with -DTG3000_SYSTEM_SDL=ON)")
  endif()
endif()

if(TARGET SDL3::SDL3-static)
  set(TG3000_SDL_TARGET SDL3::SDL3-static)
else()
  set(TG3000_SDL_TARGET SDL3::SDL3)
endif()

add_executable(tg3000
  port/main.c
  port/rom.c
  port/sha1.c
  port/platform/ignored.c
  port/platform/video_events.c
  port/platform/input.c
  port/platform/frame.c
)
target_include_directories(tg3000 PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/port"
  "${CMAKE_CURRENT_SOURCE_DIR}/port/platform"
)
target_compile_options(tg3000 PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(tg3000 PRIVATE tg3000_logic ${TG3000_SDL_TARGET})
set_target_properties(tg3000 PROPERTIES WIN32_EXECUTABLE OFF)

if(MINGW)
  target_link_options(tg3000 PRIVATE -static)
endif()

enable_testing()
add_test(NAME rom_sha1_selftest COMMAND tg3000 --selftest)
add_test(NAME game_smoke_title_card
  COMMAND tg3000 --screen title_card --frames 300 --expect-return
  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
)
# The password display waits for any button: a scripted press must end it.
add_test(NAME game_smoke_password_press
  COMMAND tg3000 --screen password_show --frames 400 --press 120 --expect-return
  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
)
set_tests_properties(game_smoke_title_card game_smoke_password_press PROPERTIES
  ENVIRONMENT "SDL_VIDEODRIVER=dummy;TG3000_HEADLESS=1"
  SKIP_RETURN_CODE 77
)
