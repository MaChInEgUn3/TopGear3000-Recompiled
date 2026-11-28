# Building Top Gear 3000: Recompiled

This guide covers building the port from source on Linux and Windows (x86-64).

> [!IMPORTANT]
> The repository contains no game assets. To run the executable after building, place your legally obtained `Top Gear 3000 (USA)` ROM (`1,048,576` bytes, no copier header, SHA-1 `058aadf1ee719cb9e0c7333e665797f990a6381d`) at `input/TG3000.sfc` or pass its path on the command line.

## Table of Contents
* [Linux](#linux)
* [Windows](#windows)
* [Using System SDL3](#using-system-sdl3)
* [Updating the Submodule](#updating-the-submodule)
* [Running Checks and CLI Options](#running-checks-and-cli-options)
* [Reference Memories for the Picture Tests](#reference-memories-for-the-picture-tests)

## Linux

Install build tools and SDL3 dependencies (Debian/Ubuntu):

```bash
sudo apt-get update
sudo apt-get install -y \
  build-essential cmake ninja-build \
  libx11-dev libxext-dev libxrandr-dev libxcursor-dev \
  libxi-dev libxss-dev libxtst-dev libxkbcommon-dev \
  libwayland-dev wayland-protocols \
  libegl1-mesa-dev libgl1-mesa-dev \
  libasound2-dev libpulse-dev libpipewire-0.3-dev \
  libudev-dev libdbus-1-dev libdrm-dev libgbm-dev
```

> [!NOTE]
> On older Ubuntu releases such as 20.04, `libpipewire-0.3-dev` is not available — remove it from the `apt-get install` list. CMake **3.24 or newer** is also required (on Ubuntu 20.04, install it from the Kitware APT repository).

Clone with submodules, fetch pinned SDL3 (`release-3.4.16`), configure, build, and test:

```bash
git clone --recursive https://github.com/MaChInEgUn3/TopGear3000-Recompiled.git
cd TopGear3000-Recompiled
cmake -P tools/fetch_deps.cmake
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

Run:

```bash
./build/tg3000
```

## Windows

MSVC is **not** supported because the decompilation C code uses GCC/Clang builtins (`__builtin_trap()`). Use **MSYS2 (UCRT64)** or **WinLibs MinGW-w64 (UCRT)** with `cmake` and `ninja` on `PATH`.

In MSYS2 UCRT64, install the required packages:

```bash
pacman -S --needed \
  mingw-w64-ucrt-x86_64-gcc \
  mingw-w64-ucrt-x86_64-cmake \
  mingw-w64-ucrt-x86_64-ninja \
  git
```

Clone with submodules, fetch pinned SDL3 (`release-3.4.16`), configure, build, test, and run:

```bash
git clone --recursive https://github.com/MaChInEgUn3/TopGear3000-Recompiled.git
cd TopGear3000-Recompiled
cmake -P tools/fetch_deps.cmake
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
./build/tg3000.exe
```

## Using System SDL3

If SDL3 (3.4 or later) is already installed on your system and you do not want to build it from `deps/SDL3`:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DTG3000_SYSTEM_SDL=ON
```

## Updating the Submodule

If you cloned without `--recursive` or need to sync `extern/tg3000-decomp` after pulling:

```bash
git submodule update --init --recursive
```

## Running Checks and CLI Options

* `ctest --test-dir build --output-on-failure` runs `tg3000 --selftest` (`rom_sha1_selftest`), which verifies the built-in SHA-1 implementation against standard test vectors without initializing SDL video.
* `ctest` also runs the game tests, with no window (`SDL_VIDEODRIVER=dummy`):
  `game_smoke_title_card` (the title card must end by itself) and `game_smoke_password_press`
  (a scripted button press must end the password display). Without a ROM in `input/` they are
  reported as skipped.
* `tg3000 --screen NAME --frames N`: run one of the game's screens (`title_card`,
  `password_show`, `vs`, `options`, `race` with its saved memory, ...; `game`, the default, runs
  the whole game) and quit after N frames (0 = until the window closes).
  `TG3000_HEADLESS=1` turns error message boxes off.
* `--race-refreshes 2|3`: screen refreshes per race step (README, Settings; default 2, other
  values refused). Test only: `--race-refresh-replay FILE` plays the refresh count of each step
  the original took, recorded on the bench (`notes/race/race_refreshes.txt`, see below);
  `--screen race_wait` runs the race loop's wait alone from `notes/race/wram_race_wait.bin`.
* Controls: arrows = D-pad, Z = B, X = A, A = Y, S = X, Q = L, W = R, Enter = Start,
  Right Shift = Select, Esc = quit; or the first connected gamepad (SNES button positions).
* `tg3000 --check-rom [path]` verifies ROM size (`1,048,576` bytes) and SHA-1 without opening a window. If `[path]` is omitted, it checks `input/TG3000.sfc` first and falls back to `TG3000.sfc` in the current directory.

## Reference Memories for the Picture Tests

Three picture tests (`game_picture_password_show`, `game_picture_options`, `game_picture_vs`) start from 128 KB WRAM snapshots captured from the original game on the `tg3000-decomp` bench. Because those snapshots are game data, they are not tracked in git and are skipped by `ctest` when absent.

To regenerate them on Linux or WSL next to a built `tg3000-decomp` checkout:

```bash
bash tools/make_reference_memory.sh /path/to/tg3000-decomp input/TG3000.sfc /tmp/tg3000-ref-wram
cp /tmp/tg3000-ref-wram/wram_before_pw.bin notes/pw/wram_before_pw.bin
cp /tmp/tg3000-ref-wram/wram_before_opt.bin notes/opt/wram_before_opt.bin
cp /tmp/tg3000-ref-wram/wram_before_title.bin notes/opt/wram_before_title.bin
cp /tmp/tg3000-ref-wram/wram_before_race.bin notes/race/wram_before_race.bin
```

The script also writes `wram_before_tick.bin`, `wram_before_res.bin` and
`wram_before_title_screen.bin` (see its header for each one's test). `wram_before_race.bin`
(`game_race_load_runs`) is taken at a PC rather than a frame end: the script patches the bench's
`tools/bench/bench.c` for one run and restores it, so that file must have no local changes.
With `DSP4_FIRMWARE=input/dsp4.rom` (a bench tool only; the port never uses it) the script also
records the race's pacing on the firmware bench: `race_refreshes.txt` (the refreshes each loop
step of `autopilot_60k.txt`'s first race took, `$1E9E`) and `wram_race_wait.bin` (WRAM at the
race loop's wait); copy both to `notes/race/` (`game_race_load_runs_refresh_replay`,
`game_race_wait_*`).
