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
  port/video/convert.c
  port/video/assets.c
  port/video/palette.c
  port/screens/menu_screen.c
)
target_include_directories(tg3000 PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/port"
  "${CMAKE_CURRENT_SOURCE_DIR}/port/platform"
  "${CMAKE_CURRENT_SOURCE_DIR}/port/video"
  "${CMAKE_CURRENT_SOURCE_DIR}/port/screens"
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
# The title card drawn natively must stay the picture that matched the original pixel for pixel
# (frame 119 of the screen = the decomp bench's frame 180 of explore_options.txt; see
# docs/DECISIONS.md "The title card drawn natively"). Only its fingerprint is in the repository.
add_test(NAME game_picture_title_card
  COMMAND tg3000 --screen title_card --frames 125 --shot 119 -
  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
)
set_tests_properties(game_picture_title_card PROPERTIES
  PASS_REGULAR_EXPRESSION "frame 119 picture hash 7c3658674cf68a42 brightness 15 drawn title_card"
)
# One screen after another: once the password display loads its own blocks, the title card's
# drawing must stop and password_show must be drawn instead (the port draws by the blocks each
# role holds, not by what was ever loaded).
add_test(NAME game_screen_sequence
  COMMAND tg3000 --screen title_card,password_show --frames 600 --press 420 --shot 330 - --expect-return
  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
)
set_tests_properties(game_screen_sequence PROPERTIES
  PASS_REGULAR_EXPRESSION "frame 330 picture hash [0-9a-f]+ brightness [0-9]+ drawn password_show"
)
# The password display from the original's own game memory (saved on the decomp's bench just
# before the screen, autopilot_60k.txt step 41070; a local file under notes/, never in git):
# frame 137 matched the original's picture pixel for pixel. Skipped where the file is absent.
add_test(NAME game_picture_password_show
  COMMAND tg3000 --wram notes/pw/wram_before_pw.bin --screen password_show --frames 140 --shot 137 -
  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
)
set_tests_properties(game_picture_password_show PROPERTIES
  PASS_REGULAR_EXPRESSION "frame 137 picture hash 67be51b43114d1f5 brightness 15 drawn password_show"
)
# The main options menu from the original's own game memory (saved on the decomp's bench just
# before the screen, explore_options.txt step 2955; a local file under notes/, never in git):
# frame 132 matched the original's picture (step 3100, frame_03101.png) pixel for pixel.
# Skipped where the file is absent.
add_test(NAME game_picture_options
  COMMAND tg3000 --wram notes/opt/wram_before_opt.bin --screen options --frames 135 --shot 132 -
  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
)
set_tests_properties(game_picture_options PROPERTIES
  PASS_REGULAR_EXPRESSION "frame 132 picture hash 818ba9b5248b7647 brightness 15 drawn options"
)
# The VS screen reached from the title/intro memory via the bench movie (explore_options /
# vs_race.txt, offset 403): frame 3788 matched the original's picture (step 4200,
# frame_04201.png) pixel for pixel. Skipped where the local WRAM file is absent.
add_test(NAME game_picture_vs
  COMMAND tg3000 --wram notes/opt/wram_before_title.bin --screen intro,options --movie extern/tg3000-decomp/tools/bench/movies/vs_race.txt --movie-offset 403 --fast --frames 3790 --shot 3788 -
  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
)
set_tests_properties(game_picture_vs PROPERTIES
  PASS_REGULAR_EXPRESSION "frame 3788 picture hash ad9e0aad0d4d3811 brightness 15 drawn vs"
)
# The password entry screen reached from the title/intro memory via the bench movie
# (explore_pw.txt, offset 403): frames 3132 and 3133 matched the original's pictures
# (steps 3550 and 3551, frame_03551.png and frame_03552.png) pixel for pixel.
# Skipped where the local WRAM file is absent.
add_test(NAME game_picture_password_entry
  COMMAND tg3000 --wram notes/opt/wram_before_title.bin --screen intro,options --movie extern/tg3000-decomp/tools/bench/movies/explore_pw.txt --movie-offset 403 --fast --frames 3135 --shot 3132 -
  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
)
set_tests_properties(game_picture_password_entry PROPERTIES
  PASS_REGULAR_EXPRESSION "frame 3132 picture hash bd2932edc0710119 brightness 15 drawn password_entry"
)
set_tests_properties(game_smoke_title_card game_smoke_password_press game_picture_title_card game_screen_sequence game_picture_password_show game_picture_options game_picture_vs game_picture_password_entry PROPERTIES
  ENVIRONMENT "SDL_VIDEODRIVER=dummy;TG3000_HEADLESS=1"
  SKIP_RETURN_CODE 77
)
