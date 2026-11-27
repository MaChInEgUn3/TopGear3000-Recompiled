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
  port/platform/sound_sets.c
  port/sound/convert_audio.c
  port/sound/mixer.c
  port/sound/sequencer.c
  port/video/convert.c
  port/video/assets.c
  port/video/palette.c
  port/screens/menu_screen.c
  port/screens/results_screen.c
  port/screens/intro_screen.c
  port/screens/title_screen.c
  port/screens/champ_screen.c
  port/screens/shop_screen.c
  port/screens/ticker_screen.c
  port/screens/race_start_screen.c
)
target_include_directories(tg3000 PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/port"
  "${CMAKE_CURRENT_SOURCE_DIR}/port/platform"
  "${CMAKE_CURRENT_SOURCE_DIR}/port/sound"
  "${CMAKE_CURRENT_SOURCE_DIR}/port/video"
  "${CMAKE_CURRENT_SOURCE_DIR}/port/screens"
)
target_compile_options(tg3000 PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(tg3000 PRIVATE tg3000_logic ${TG3000_SDL_TARGET})
set_target_properties(tg3000 PROPERTIES WIN32_EXECUTABLE OFF)

if(MINGW)
  target_link_options(tg3000 PRIVATE -static)
endif()
if(UNIX)
  # The sound conversions and mixer use the C maths library.
  target_link_libraries(tg3000 PRIVATE m)
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
# The options submenu reached from the title/intro memory via the bench movie
# (explore_options.txt, offset 403, then offset 413 from frame 2554): frames 2775 and 2776
# matched the original's pictures (steps 3190 and 3191, frame_03191.png and frame_03192.png)
# pixel for pixel. Skipped where the local WRAM file is absent.
add_test(NAME game_picture_options_submenu
  COMMAND tg3000 --wram notes/opt/wram_before_title.bin --screen intro,options --movie extern/tg3000-decomp/tools/bench/movies/explore_options.txt --movie-offset 403 --movie-offset-from 2554 413 --fast --frames 2777 --shot 2775 -
  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
)
set_tests_properties(game_picture_options_submenu PROPERTIES
  PASS_REGULAR_EXPRESSION "frame 2775 picture hash b288ee1fcad88168 brightness 15 drawn options"
)
# The results screen from the original's own game memory (saved on the decomp's bench just
# before the screen, autopilot_60k.txt step 16489; a local file under notes/, never in git):
# frames 73 and 74 matched the original's pictures (steps 16580 and 16581, frame_16582.png and
# frame_16583.png) pixel for pixel. Skipped where the local WRAM file is absent.
add_test(NAME game_picture_results
  COMMAND tg3000 --wram notes/res/wram_before_res.bin --screen results --fast --frames 74 --shot 73 -
  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
)
set_tests_properties(game_picture_results PROPERTIES
  PASS_REGULAR_EXPRESSION "frame 73 picture hash 5124a4f2bbfb1ccb brightness 15 drawn results"
)
# The intro screen (zoom phase) from the title/intro memory: port frames 295 and 296 matched
# the original's pictures (steps 700 and 701, frame_00701.png and frame_00702.png) pixel for
# pixel. Skipped where the local WRAM file is absent.
add_test(NAME game_picture_intro_zoom
  COMMAND tg3000 --wram notes/opt/wram_before_title.bin --screen intro --fast --frames 296 --shot 295 -
  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
)
set_tests_properties(game_picture_intro_zoom PROPERTIES
  PASS_REGULAR_EXPRESSION "frame 295 picture hash 5ad7d5ecba353d04 brightness 15 drawn intro"
)
# Regression only (not exactness): the intro's settle phase is 1 pixel off the original per
# frame (Mode 7 rounding, not copied from the chip; docs/DECISIONS.md). Locks the port's output.
add_test(NAME game_regression_intro_settle
  COMMAND tg3000 --wram notes/opt/wram_before_title.bin --screen intro --fast --frames 1316 --shot 1315 -
  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
)
set_tests_properties(game_regression_intro_settle PROPERTIES
  PASS_REGULAR_EXPRESSION "frame 1315 picture hash ed15392db6dbe6e0 "
  ENVIRONMENT "SDL_VIDEODRIVER=dummy;TG3000_HEADLESS=1"
  SKIP_RETURN_CODE 77
)
# The intro screen (static lit title logo, $013E == 30) from the title/intro memory: port
# frames 2298 and 2299 matched the original's pictures (steps 2700 and 2701, frame_02701.png
# and frame_02702.png) pixel for pixel. Skipped where the local WRAM file is absent.
add_test(NAME game_picture_intro_logo
  COMMAND tg3000 --wram notes/opt/wram_before_title.bin --screen intro --fast --frames 2299 --shot 2298 -
  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
)
set_tests_properties(game_picture_intro_logo PROPERTIES
  PASS_REGULAR_EXPRESSION "frame 2298 picture hash 757d5bd97fecbd24 brightness 15 drawn intro"
  ENVIRONMENT "SDL_VIDEODRIVER=dummy;TG3000_HEADLESS=1"
  SKIP_RETURN_CODE 77
)
# The title screen ($80:FBA5) from the original's own game memory (autopilot_60k.txt step
# 16662; a local file under notes/, never in git): port frame 40 matched the original's
# moving picture (step 16750, frame_16752.png) pixel for pixel. Skipped where absent.
add_test(NAME game_picture_title_screen
  COMMAND tg3000 --wram notes/title/wram_before_title_screen.bin --screen title --fast --frames 41 --shot 40 -
  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
)
set_tests_properties(game_picture_title_screen PROPERTIES
  PASS_REGULAR_EXPRESSION "frame 40 picture hash 0fc0f3b53eced1c0 brightness 15 drawn title"
  ENVIRONMENT "SDL_VIDEODRIVER=dummy;TG3000_HEADLESS=1"
  SKIP_RETURN_CODE 77
)
# The title screen settled moment (bench steps 16865/16866, frame_16867.png/frame_16868.png):
# port frames 155..164 match pixel for pixel (0 of 57,344 pixels different).
add_test(NAME game_picture_title_settled
  COMMAND tg3000 --wram notes/title/wram_before_title_screen.bin --screen title --fast --frames 160 --shot 159 -
  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
)
set_tests_properties(game_picture_title_settled PROPERTIES
  PASS_REGULAR_EXPRESSION "frame 159 picture hash 3df7e846c8aca260 brightness 15 drawn title"
  ENVIRONMENT "SDL_VIDEODRIVER=dummy;TG3000_HEADLESS=1"
  SKIP_RETURN_CODE 77
)
# The game's own start-up (decomp game_boot) then the intro, from nothing saved: the intro's zoom
# picture must be the one that matched the original pixel for pixel.
add_test(NAME game_boot_intro
  COMMAND tg3000 --screen boot,intro --fast --frames 300 --shot 295 -
  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
)
set_tests_properties(game_boot_intro PROPERTIES
  PASS_REGULAR_EXPRESSION "frame 295 picture hash 5ad7d5ecba353d04 "
  ENVIRONMENT "SDL_VIDEODRIVER=dummy;TG3000_HEADLESS=1"
  SKIP_RETURN_CODE 77
)
# The whole game in the original's order (decomp game_main): boot, title card, intro, options menu.
add_test(NAME game_main_to_options
  COMMAND tg3000 --fast --frames 3400 --shot 3300 -
  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
)
set_tests_properties(game_main_to_options PROPERTIES
  PASS_REGULAR_EXPRESSION "frame 3300 picture hash a312a352fc47767d brightness 15 drawn options"
  ENVIRONMENT "SDL_VIDEODRIVER=dummy;TG3000_HEADLESS=1"
  SKIP_RETURN_CODE 77
)
# The whole game with autopilot_60k.txt (offset 150) reaches the championship screen ($0008 = $B97B,
# split active): moment 0 (opening slide, port frame 3196 = bench frame_03373.png) and moment 1
# (settled menu, port frame 4215 = bench frame_04366.png), 0 of 57,344 pixels different each.
add_test(NAME game_main_movie_to_champ
  COMMAND tg3000 --movie extern/tg3000-decomp/tools/bench/movies/autopilot_60k.txt --movie-offset 150 --fast --frames 3200 --shot 3196 -
  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
)
set_tests_properties(game_main_movie_to_champ PROPERTIES
  PASS_REGULAR_EXPRESSION "frame 3196 picture hash cfc758786a2a003f brightness 15 drawn champ"
  ENVIRONMENT "SDL_VIDEODRIVER=dummy;TG3000_HEADLESS=1"
  SKIP_RETURN_CODE 77
)
# The opening slide with both halves moving (task 2's bar closing, task 1's slide): each half as
# its interrupt sent it; top = bench frame_03368.png, bottom = frame_03367.png, 0 pixels each (the
# one frame between the halves is the console's CPU time in the load, docs/DECISIONS.md).
add_test(NAME game_picture_champ_slide
  COMMAND tg3000 --movie extern/tg3000-decomp/tools/bench/movies/autopilot_60k.txt --movie-offset 150 --fast --frames 3195 --shot 3191 -
  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
)
set_tests_properties(game_picture_champ_slide PROPERTIES
  PASS_REGULAR_EXPRESSION "frame 3191 picture hash 9b584f6e12e30411 brightness 15 drawn champ"
  ENVIRONMENT "SDL_VIDEODRIVER=dummy;TG3000_HEADLESS=1"
  SKIP_RETURN_CODE 77
)
add_test(NAME game_picture_champ_menu
  COMMAND tg3000 --movie extern/tg3000-decomp/tools/bench/movies/autopilot_60k.txt --movie-offset 150 --fast --frames 4217 --shot 4215 -
  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
)
set_tests_properties(game_picture_champ_menu PROPERTIES
  PASS_REGULAR_EXPRESSION "frame 4215 picture hash 23aaa266fd1955f7 brightness 15 drawn champ"
  ENVIRONMENT "SDL_VIDEODRIVER=dummy;TG3000_HEADLESS=1"
  SKIP_RETURN_CODE 77
)
# The championship submenus reached with explore_menu.txt (offset 150): NAME entry ($80:C6D4,
# port frame 4648 = bench frame_04801.png, 0 of 57,344 pixels different) and CONTROL ($80:CB4A,
# port frame 5033 = bench frame_05186.png, 0 of 57,344 pixels different).
add_test(NAME game_picture_champ_name
  COMMAND tg3000 --movie extern/tg3000-decomp/tools/bench/movies/explore_menu.txt --movie-offset 150 --fast --frames 4650 --shot 4648 -
  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
)
set_tests_properties(game_picture_champ_name PROPERTIES
  PASS_REGULAR_EXPRESSION "frame 4648 picture hash 535429c768b6b085 brightness 15 drawn champ"
  ENVIRONMENT "SDL_VIDEODRIVER=dummy;TG3000_HEADLESS=1"
  SKIP_RETURN_CODE 77
)
add_test(NAME game_picture_champ_controls
  COMMAND tg3000 --movie extern/tg3000-decomp/tools/bench/movies/explore_menu.txt --movie-offset 150 --fast --frames 5035 --shot 5033 -
  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
)
set_tests_properties(game_picture_champ_controls PROPERTIES
  PASS_REGULAR_EXPRESSION "frame 5033 picture hash fa3442e3f08884b4 brightness 15 drawn champ"
  ENVIRONMENT "SDL_VIDEODRIVER=dummy;TG3000_HEADLESS=1"
  SKIP_RETURN_CODE 77
)
# The pre-race screen (race_start_screen) reached with explore_menu.txt (offset 150), drawn
# before the NMI (one frame later than the other screens). The bar phase ($E175): port frame 6563
# = bench frame_06716.png; the opening course view ($E28C): port frame 6585 = bench
# frame_06751.png; the open view with the meter, the planets' shadows, the discs and the viewport
# scroll: port frame 6598 (input re-aligned at 6571 for the console's longer load) = bench
# frame_06764.png. 0 of 57,344 pixels different each.
add_test(NAME game_picture_racestart_bar
  COMMAND tg3000 --movie extern/tg3000-decomp/tools/bench/movies/explore_menu.txt --movie-offset 150 --fast --frames 6564 --shot 6563 -
  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
)
set_tests_properties(game_picture_racestart_bar PROPERTIES
  PASS_REGULAR_EXPRESSION "frame 6563 picture hash 426fab719584a2b9 brightness 15 drawn race_start"
  ENVIRONMENT "SDL_VIDEODRIVER=dummy;TG3000_HEADLESS=1"
  SKIP_RETURN_CODE 77
)
add_test(NAME game_picture_racestart_opening
  COMMAND tg3000 --movie extern/tg3000-decomp/tools/bench/movies/explore_menu.txt --movie-offset 150 --fast --frames 6586 --shot 6585 -
  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
)
set_tests_properties(game_picture_racestart_opening PROPERTIES
  PASS_REGULAR_EXPRESSION "frame 6585 picture hash 03f03bcb14946a62 brightness 15 drawn race_start"
  ENVIRONMENT "SDL_VIDEODRIVER=dummy;TG3000_HEADLESS=1"
  SKIP_RETURN_CODE 77
)
add_test(NAME game_picture_racestart_open
  COMMAND tg3000 --movie extern/tg3000-decomp/tools/bench/movies/explore_menu.txt --movie-offset 150 --movie-offset-from 6571 163 --fast --frames 6599 --shot 6598 -
  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
)
set_tests_properties(game_picture_racestart_open PROPERTIES
  PASS_REGULAR_EXPRESSION "frame 6598 picture hash 91186d6108968d80 brightness 15 drawn race_start"
  ENVIRONMENT "SDL_VIDEODRIVER=dummy;TG3000_HEADLESS=1"
  SKIP_RETURN_CODE 77
)
# The pre-race screen with the camera moving, the leader lines sliding and their glow
# (explore_pw.txt, offset 150): port frame 6544 = bench frame_06710.png, 0 pixels different.
add_test(NAME game_picture_racestart_camera
  COMMAND tg3000 --movie extern/tg3000-decomp/tools/bench/movies/explore_pw.txt --movie-offset 150 --fast --frames 6545 --shot 6544 -
  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
)
set_tests_properties(game_picture_racestart_camera PROPERTIES
  PASS_REGULAR_EXPRESSION "frame 6544 picture hash 72890bfe6ba233c8 brightness 15 drawn race_start"
  ENVIRONMENT "SDL_VIDEODRIVER=dummy;TG3000_HEADLESS=1"
  SKIP_RETURN_CODE 77
)
# The shop (shop_screen, two tasks, NMI target $B97B) reached with explore_shop.txt (offset 150).
# Port frame N = bench frame_(N+169).png: the list of upgrade kinds, port 6333 = frame_06502.png;
# the fade-in at brightness 5, port 5750 = frame_05919.png. With the input re-aligned at 5800 for
# the console's longer loads: the Engine list's item screen, port 8333 = frame_08502.png; an item
# sliding out while its text is cleared, port 8274 = frame_08443.png. 0 of 57,344 pixels each.
add_test(NAME game_picture_shop_list
  COMMAND tg3000 --movie extern/tg3000-decomp/tools/bench/movies/explore_shop.txt --movie-offset 150 --fast --frames 6335 --shot 6333 -
  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
)
set_tests_properties(game_picture_shop_list PROPERTIES
  PASS_REGULAR_EXPRESSION "frame 6333 picture hash 07e5bd796ae4e4b9 brightness 15 drawn shop"
)
add_test(NAME game_picture_shop_fade
  COMMAND tg3000 --movie extern/tg3000-decomp/tools/bench/movies/explore_shop.txt --movie-offset 150 --fast --frames 5752 --shot 5750 -
  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
)
set_tests_properties(game_picture_shop_fade PROPERTIES
  PASS_REGULAR_EXPRESSION "frame 5750 picture hash e0051b651e9b64b5 brightness 5 drawn shop"
)
add_test(NAME game_picture_shop_item
  COMMAND tg3000 --movie extern/tg3000-decomp/tools/bench/movies/explore_shop.txt --movie-offset 150 --movie-offset-from 5800 166 --fast --frames 8335 --shot 8333 -
  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
)
set_tests_properties(game_picture_shop_item PROPERTIES
  PASS_REGULAR_EXPRESSION "frame 8333 picture hash 9ad5cbc49db376e2 brightness 15 drawn shop"
)
add_test(NAME game_picture_shop_slide
  COMMAND tg3000 --movie extern/tg3000-decomp/tools/bench/movies/explore_shop.txt --movie-offset 150 --movie-offset-from 5800 166 --fast --frames 8276 --shot 8274 -
  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
)
set_tests_properties(game_picture_shop_slide PROPERTIES
  PASS_REGULAR_EXPRESSION "frame 8274 picture hash bee0ba722836bffc brightness 15 drawn shop"
)
set_tests_properties(game_picture_shop_list game_picture_shop_fade game_picture_shop_item game_picture_shop_slide PROPERTIES
  ENVIRONMENT "SDL_VIDEODRIVER=dummy;TG3000_HEADLESS=1"
  SKIP_RETURN_CODE 77
)
set_tests_properties(game_smoke_title_card game_smoke_password_press game_picture_title_card game_screen_sequence game_picture_password_show game_picture_options game_picture_vs game_picture_password_entry game_picture_options_submenu game_picture_results game_picture_intro_zoom PROPERTIES
  ENVIRONMENT "SDL_VIDEODRIVER=dummy;TG3000_HEADLESS=1"
  SKIP_RETURN_CODE 77
)
# Sound set 1 (menus): load via sound_load_menu(DP, 1) and convert BRR samples to 16-bit PCM.
add_test(NAME sound_set_menu
  COMMAND tg3000 --screen sound_menu --fast --frames 20 --expect-return
  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
)
set_tests_properties(sound_set_menu PROPERTIES
  PASS_REGULAR_EXPRESSION "sound set: songs 2512 B, instruments 1066 B, samples 9, total pcm 51584 samples"
  ENVIRONMENT "SDL_VIDEODRIVER=dummy;TG3000_HEADLESS=1"
  SKIP_RETURN_CODE 77
)
# The native player on the menus' set (docs/DECISIONS.md, "The native sound player (set 1)"):
# the set-1 song from its start (port frame 12) for 1,600 steps, with the menus' effects asked at
# the steps where the original played them on the decomp's bench (autopilot_60k.txt, song start
# at bench frame 15861: $13 at step 1251, $11 at 1342..1442, $13 at 1492). Every key-on, cut,
# let-go and pitch change matched the bench's voice report (838 of 838 events, all 1,597 steps).
# Only the events' fingerprint is in the repository.
add_test(NAME sound_menu_events
  COMMAND tg3000 --screen boot,sound_menu --fast --frames 1612
    --sound-effect 1263 0x13 --sound-effect 1354 0x11 --sound-effect 1374 0x11
    --sound-effect 1394 0x11 --sound-effect 1414 0x11 --sound-effect 1434 0x11
    --sound-effect 1454 0x11 --sound-effect 1504 0x13
    --sound-events "${CMAKE_CURRENT_BINARY_DIR}/sound_menu_events.txt"
  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
)
set_tests_properties(sound_menu_events PROPERTIES
  PASS_REGULAR_EXPRESSION "sound events: 863, fingerprint 731d1e1223e0f496"
  FAIL_REGULAR_EXPRESSION "FATAL"
  ENVIRONMENT "SDL_VIDEODRIVER=dummy;TG3000_HEADLESS=1"
  SKIP_RETURN_CODE 77
)
# The mixer renders the menus' music headless into a WAV in the build directory (never in git);
# nothing may reach the output clamp (docs/DECISIONS.md, "The mixer's output level").
add_test(NAME sound_menu_wav
  COMMAND tg3000 --screen boot,sound_menu --fast --frames 600 --sound-effect 300 0x12
    --sound-wav "${CMAKE_CURRENT_BINARY_DIR}/sound_menu.wav"
  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
)
set_tests_properties(sound_menu_wav PROPERTIES
  PASS_REGULAR_EXPRESSION "sound wav: 479210 frames at 48000 Hz written, peak 0[.][0-9]+, clipped 0"
  FAIL_REGULAR_EXPRESSION "FATAL"
  ENVIRONMENT "SDL_VIDEODRIVER=dummy;TG3000_HEADLESS=1"
  SKIP_RETURN_CODE 77
)
# The native player on the intro's set (set 0) through the whole game (game_main, autopilot_60k.txt
# offset 150): the song starts at port frame 253 (bench frame 369); up to port frame 3000 (step
# 2747, before the first menu effect) every key-on, cut and let-go matched the bench's voice
# report (2,273 of 2,273 events).
add_test(NAME sound_set0_events_game_main
  COMMAND tg3000 --movie extern/tg3000-decomp/tools/bench/movies/autopilot_60k.txt --movie-offset 150
    --fast --frames 3000 --sound-events "${CMAKE_CURRENT_BINARY_DIR}/sound_set0_events.txt"
  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
)
set_tests_properties(sound_set0_events_game_main PROPERTIES
  PASS_REGULAR_EXPRESSION "sound events: 2290, fingerprint a9b08e203a7a5a8c"
  FAIL_REGULAR_EXPRESSION "FATAL"
  ENVIRONMENT "SDL_VIDEODRIVER=dummy;TG3000_HEADLESS=1"
  SKIP_RETURN_CODE 77
)
# The race sets (docs/DECISIONS.md, "The native sound player: the race sets"): the test scaffold
# `sound_setN` loads set N with the game's own loader and plays its song alone for 9,100 frames
# (more than one loop of each song). Sets 3-6: the music voices (0-3) matched the bench's voice
# report event for event over autopilot_60k.txt's races (the songs' voices are never taken by an
# effect); sets 2 and 7 are not reached by any bench movie (fingerprint only, not compared).
foreach(pair "2;6305 ba5c18e2c8600139" "3;4458 43da7196191417da" "4;4594 31fb59949c8fc1c5"
             "5;5499 4f2171afccf00310" "6;4300 c4d8d2038e085ea6" "7;11 08c7e5c40f909d5f")
  list(GET pair 0 set)
  list(GET pair 1 expect)
  string(REPLACE " " ", fingerprint " expect "${expect}")
  add_test(NAME sound_set${set}_song_events
    COMMAND tg3000 --screen boot,sound_set${set} --fast --frames 9100
      --sound-events "${CMAKE_CURRENT_BINARY_DIR}/sound_set${set}_song_events.txt"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
  )
  set_tests_properties(sound_set${set}_song_events PROPERTIES
    PASS_REGULAR_EXPRESSION "sound events: ${expect}"
    FAIL_REGULAR_EXPRESSION "FATAL"
    ENVIRONMENT "SDL_VIDEODRIVER=dummy;TG3000_HEADLESS=1"
    SKIP_RETURN_CODE 77
  )
endforeach()
# A whole race's sound as the original's driver heard it: the scaffold loads the race's set, then
# the messages the original sent from its song request to the race's end (recorded on the
# decomp's bench, autopilot_60k.txt; a local file under notes/, skipped where absent) replace the
# port's own. Music, engine, race effects and echo: every key-on, cut, let-go, pitch and volume
# change and echo setting identical to the bench's voice report (set 3: 7,440 of 7,440 events;
# set 4: 10,986; set 5: 11,047; set 6: 11,262).
foreach(race "3;8094;7740;7861 3af30fc4a9a95e42" "4;17574;10990;11562 3cf9b22e9c9110f1"
             "5;30292;9702;11610 b4927be987bda54a" "6;41951;10621;12154 ed464e59376f245d")
  list(GET race 0 set)
  list(GET race 1 from)
  list(GET race 2 frames)
  list(GET race 3 expect)
  string(REPLACE " " ", fingerprint " expect "${expect}")
  add_test(NAME sound_race_set${set}_replay
    COMMAND tg3000 --screen boot,sound_set${set} --fast --frames ${frames}
      --sound-replay notes/sound/seq_tools/replay/race_set${set}_${from}.txt
      --sound-events "${CMAKE_CURRENT_BINARY_DIR}/sound_race_set${set}_replay.txt"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
  )
  set_tests_properties(sound_race_set${set}_replay PROPERTIES
    PASS_REGULAR_EXPRESSION "sound events: ${expect}"
    FAIL_REGULAR_EXPRESSION "FATAL"
    ENVIRONMENT "SDL_VIDEODRIVER=dummy;TG3000_HEADLESS=1"
    SKIP_RETURN_CODE 77
  )
endforeach()
# The one-player race from the original's own game memory at race_wrapper ($82:8342, dumped on the
# decomp's bench by tools/make_reference_memory.sh; a local file under notes/, never in git): the
# setup (race_load, race_sound, the track, the road's first frame) runs through, and the port
# stops at the first DSP-4 sprite command ($09, object_sprites_stream in the first
# race_frame_draw), not native yet (docs/decomp-requests.md, A). Skipped where the file is absent.
add_test(NAME game_race_load_runs
  COMMAND tg3000 --wram notes/race/wram_before_race.bin --screen race --fast --frames 300
  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
)
set_tests_properties(game_race_load_runs PROPERTIES
  PASS_REGULAR_EXPRESSION "FATAL \\(frame 89\\): the race's DSP-4 commands are not native yet"
  ENVIRONMENT "SDL_VIDEODRIVER=dummy;TG3000_HEADLESS=1"
  SKIP_RETURN_CODE 77
)
# Two-task kernel (decomp request D, step 1): the ticker screen runs from saved game memory
# (a local file under notes/, never in git) and returns by itself. Skipped where absent.
add_test(NAME game_ticker_runs
  COMMAND tg3000 --wram notes/tick/wram_before_tick.bin --screen ticker --fast --frames 800 --expect-return
  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
)
set_tests_properties(game_ticker_runs PROPERTIES
  PASS_REGULAR_EXPRESSION "screen ticker returned at frame "
  ENVIRONMENT "SDL_VIDEODRIVER=dummy;TG3000_HEADLESS=1"
  SKIP_RETURN_CODE 77
)
# The ticker drawn natively (two tasks, Mode 7 top half) from the same saved memory: port frame F
# = bench frame_(F+15868).png, all 625 frames 0 of 57,344 pixels different. Pinned: a fade-in
# frame (23, brightness 11: frame_15891.png), the lit result text (142: frame_16010.png) and a
# "SECRET BONUS" text from bank $81 (382: frame_16250.png).
add_test(NAME game_picture_ticker_fade
  COMMAND tg3000 --wram notes/tick/wram_before_tick.bin --screen ticker --fast --frames 24 --shot 23 -
  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
)
set_tests_properties(game_picture_ticker_fade PROPERTIES
  PASS_REGULAR_EXPRESSION "frame 23 picture hash 5e82fbd5960c8541 brightness 11 drawn ticker"
)
add_test(NAME game_picture_ticker
  COMMAND tg3000 --wram notes/tick/wram_before_tick.bin --screen ticker --fast --frames 143 --shot 142 -
  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
)
set_tests_properties(game_picture_ticker PROPERTIES
  PASS_REGULAR_EXPRESSION "frame 142 picture hash 82eea19a0fbb1fd8 brightness 15 drawn ticker"
)
add_test(NAME game_picture_ticker_bonus
  COMMAND tg3000 --wram notes/tick/wram_before_tick.bin --screen ticker --fast --frames 383 --shot 382 -
  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
)
set_tests_properties(game_picture_ticker_bonus PROPERTIES
  PASS_REGULAR_EXPRESSION "frame 382 picture hash e74ddcb2681d7ab6 brightness 15 drawn ticker"
)
set_tests_properties(game_picture_ticker_fade game_picture_ticker game_picture_ticker_bonus PROPERTIES
  ENVIRONMENT "SDL_VIDEODRIVER=dummy;TG3000_HEADLESS=1"
  SKIP_RETURN_CODE 77
)
