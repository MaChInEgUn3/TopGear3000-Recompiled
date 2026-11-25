<p align="center"><img src="docs/img/banner.svg" alt="Top Gear 3000: Recompiled" width="100%"></p>

<p align="center">
  <a href="https://github.com/MaChInEgUn3/TopGear3000-Recompiled/actions/workflows/build.yml"><img src="https://github.com/MaChInEgUn3/TopGear3000-Recompiled/actions/workflows/build.yml/badge.svg" alt="Build"></a>
  <a href="LICENSE"><img src="https://img.shields.io/badge/license-GPL--3.0--or--later-blue.svg" alt="License: GPL-3.0-or-later"></a>
  <img src="https://img.shields.io/badge/platforms-Linux%20%7C%20Windows-8b5cf6.svg" alt="Platforms: Linux | Windows">
  <img src="https://img.shields.io/badge/status-pre--alpha-orange.svg" alt="Status: pre-alpha">
</p>

**English** · [Português (BR)](README.pt-br.md)

# Top Gear 3000: Recompiled

Top Gear 3000: Recompiled is an early-stage native PC port (Linux and Windows x86-64) of the SNES game **Top Gear 3000**, built on top of the readable, verified C game logic from [tg3000-decomp](https://github.com/MaChInEgUn3/tg3000-decomp) and using SDL3 for windowing, input, and audio.

### **This repository and its releases do not contain game assets. The original game is required to run this project.**

You must provide your own legally obtained copy of **Top Gear 3000 (USA)**:
* Exact size: `1,048,576` bytes (no copier header)
* SHA-1: `058aadf1ee719cb9e0c7333e665797f990a6381d`
* Place it at `input/TG3000.sfc` (git-ignored) or pass its path on the command line.
* The DSP-4 firmware (`dsp4.rom`) is **not** needed.

## Table of Contents
* [Status](#status)
* [Not Even 1% Emulator](#not-even-1-emulator)
* [System Requirements](#system-requirements)
* [Planned](#planned)
* [FAQ](#faq)
* [Building](#building)
* [Project Layout](#project-layout)
* [Contributing](#contributing)
* [Libraries Used and Special Thanks](#libraries-used-and-special-thanks)
* [Legal](#legal)

## Status

The project is currently at **step 4 of 4** (pre-alpha). **The game is not playable yet** and **no releases exist yet**. Today, the executable builds on Linux and Windows x86-64, verifies your ROM (size and SHA-1), and runs a few of the game's screens from the decomp's C at the console's frame rate, with keyboard and gamepad input wired in (not yet usable: nothing is shown). The first screens are drawn natively, identical to the original pixel for pixel: the title card and the password display. Other screens stay black for now. No saves, settings, mods, widescreen, or gameplay exist yet.

| Step | Milestone | State |
|---|---|---|
| 1 | Skeleton (CMake, SDL3 fetch, ROM SHA-1 check, empty window, CI) | ✅ Done |
| 2 | Platform layer (frame loop, input; [design](docs/proposals/video-layer.md)) | ✅ Done |
| 3 | Sound interface ([design](docs/proposals/sound.md); player waits on the decomp) | ✅ Designed |
| 4 | First native menus (title card and password display done, pixel-exact) | ⏳ In progress |

## Not Even 1% Emulator

The shipped game contains **no** model of the SNES CPU (65816), video chip (PPU), sound processor (SPC700/S-DSP), or DSP-4 coprocessor.

* **Game logic:** comes from the readable C in [tg3000-decomp](https://github.com/MaChInEgUn3/tg3000-decomp), where every ported routine is proven against the original ROM in a differential test.
* **Video and sound:** rewritten natively, drawing each screen and playing audio directly from the game-level data the logic builds in memory (tile maps, sprite lists, palettes, per-line road tables).
* **DSP-4:** coprocessor commands become native C functions.

**The litmus test:** if the video or sound code could display or play *another* SNES game given its memory, it is an emulator and is not allowed here.

## System Requirements

* **OS:** Windows 10 or later (x86-64) or Linux (x86-64)
* **Hardware:** nothing exotic — the port uses SDL3 for windowing, input, and audio.

## Planned

The items below are **goals, not promises** — none of them are implemented yet:

* Native video renderer drawn per screen from game data in memory
* Native sound player for music and sound effects
* Native DSP-4 math functions
* Configurable keyboard and controller mapping
* Prebuilt releases for Linux and Windows x86-64

## FAQ

#### Is this a static recompilation?
No — despite the name. "Recompiled" names the kind of native PC port. Nothing is statically recompiled from the ROM; the game logic is hand-written, readable C from [tg3000-decomp](https://github.com/MaChInEgUn3/tg3000-decomp) verified routine by routine against the original.

#### How is this related to the decompilation project?
This repository consumes [tg3000-decomp](https://github.com/MaChInEgUn3/tg3000-decomp) as a read-only git submodule pinned at a named commit in `extern/tg3000-decomp/`.

#### Is it an emulator?
No. There is no 65816 CPU, PPU, SPC700/S-DSP, or DSP-4 emulation in the port. See [Not Even 1% Emulator](#not-even-1-emulator).

#### Why is MSVC not supported?
The decompilation C code uses GCC/Clang builtins (such as `__builtin_trap()`). Use GCC or Clang (on Windows, MinGW-w64 UCRT via MSYS2 or WinLibs).

#### Does it need the DSP-4 firmware?
No. `dsp4.rom` is never needed because DSP-4 commands will be implemented as native C functions.

## Building

No prebuilt releases exist yet. Quick start (Linux or Windows MSYS2 UCRT64):

```bash
git clone --recursive https://github.com/MaChInEgUn3/TopGear3000-Recompiled.git
cd TopGear3000-Recompiled
cmake -P tools/fetch_deps.cmake
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build && ctest --test-dir build --output-on-failure
```

See [BUILDING.md](BUILDING.md) for full prerequisites, Ubuntu 20.04 notes, Windows toolchain setup, system SDL3 usage, and test commands.

## Project Layout

```text
extern/tg3000-decomp/   read-only submodule with verified C game logic
port/                   native PC port code (main.c, rom.c, sha1.c)
port/platform/          the port's side of the game's platform calls (frame loop, input)
tools/                  helper CMake scripts (fetch_deps.cmake)
docs/                   decisions (DECISIONS.md) and accepted design proposals (proposals/)
deps/                   fetched dependencies (SDL3, git-ignored)
input/                  default user ROM location (TG3000.sfc, git-ignored)
build/                  out-of-source build directory (git-ignored)
```

## Contributing

Pull requests are welcome. Never include ROM data, extracted assets, firmware, or screenshots in commits or PRs.

Changes to the game logic itself belong in [tg3000-decomp](https://github.com/MaChInEgUn3/tg3000-decomp), not here.

## Libraries Used and Special Thanks

* [SDL3](https://github.com/libsdl-org/SDL) (zlib license) for cross-platform windowing, input, and audio
* [tg3000-decomp](https://github.com/MaChInEgUn3/tg3000-decomp) (GPL-3.0-or-later) for the verified C decompilation of the game logic

## Legal

Licensed under **GPL-3.0-or-later** — see [LICENSE](LICENSE).

Top Gear 3000 is © 1994 Kemco / Gremlin Graphics. This is an unofficial fan project, not affiliated with or endorsed by them or Nintendo.
