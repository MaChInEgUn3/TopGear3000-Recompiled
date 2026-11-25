# Architecture

## Stack
- **Language:** C11 (GCC or Clang; MSVC unsupported due to `__builtin_trap()` in the decomp).
- **Build:** CMake (>= 3.24) + Ninja.
- **Platform / Window / Audio:** SDL3 (`release-3.4.16`, static in-tree from `deps/SDL3` or system via `-DTG3000_SYSTEM_SDL=ON`).
- **Game logic:** `extern/tg3000-decomp` (read-only git submodule, compiled as `tg3000_logic` static library; not yet linked into `tg3000`).

## Directory layout
- `extern/tg3000-decomp/` — read-only submodule with verified readable C of the SNES game logic.
- `port/` — native PC port code: `main.c` (command line, ROM check, window), `rom.c`, `sha1.c`.
- `port/platform/` — the port's side of the decomp's `platform.h`: `frame.c` (the frame boundary,
  `g_ram`/`g_rom`, timing, sound message, DSP-4 stop), `input.c` (keyboard and gamepad),
  `video_events.c` (named graphics/palette events, empty until step 4), `ignored.c` (every
  register-style call, empty bodies, one file so review is trivial).
- `docs/proposals/` — accepted design proposals (`video-layer.md`).
- `tools/` — helper CMake scripts (`fetch_deps.cmake`).
- `deps/` — fetched third-party dependencies (`deps/SDL3`, git-ignored).
- `input/` — default user ROM location (`input/TG3000.sfc`, git-ignored).
- `build/` — out-of-source build directory (git-ignored).
- `docs/` — architectural decisions (`docs/DECISIONS.md`) and `docs/img/` (original banner).
- `README.md` (English) / `README.pt-br.md`, `BUILDING.md` — user-facing docs.

## Current data flow (step 2)
```
main() -> rom_load() [1048576 bytes + SHA-1] -> g_rom -> SDL window + renderer (logical 256x224,
  integer scale) -> port_run(screen): zero g_ram, interrupt_vectors_init(), call the game's
  screen function (blocking) -> ... the game calls platform_wait_vblank() at every frame wait:
    0. SDL events (quit/Escape -> longjmp back to port_run; input state)
    1. the game's frame code (its NMI, in C): JML target at $0000, then nmi_prologue() and the
       target word at $0008 (fixed table; unknown -> port_fatal with the value)
    3. draw (black until step 4) and present
    4. pace at 60.0988 fps (SDL_DelayPrecise)
```
The game owns the loop; the port lives at the frame boundary (docs/proposals/video-layer.md).
CLI modes:
- `--selftest`: runs SHA-1 test vectors without initializing SDL video (used by `ctest` target `rom_sha1_selftest`).
- `--check-rom [path]`: verifies ROM size and SHA-1 without opening a window.
- `--screen NAME` (`title_card`, `password_show`, `vs`, `options`; default `title_card`):
  which game screen to run, until the decomp has its boot and dispatcher in C.
- `--frames N`: quit after N frames (tests); with no ROM it exits 77 (ctest: skipped).
- `--expect-return` (tests): exit 2 if the screen function has not returned by then.
- `--press F` (tests): hold A+B for 6 frames from frame F.

## Boundary
The decomp's `src/platform.h` is chip-shaped on purpose (its bench compares with the
original). The port answers it one level up: register-style calls are ignored, graphics and
palettes become named events (step 4), and each kind of screen is drawn by its own code from
game RAM and pictures converted from the cartridge. Rules: `docs/proposals/video-layer.md` and
`docs/DECISIONS.md`, "Video layer: the owner's answers".

## Build and test
```bash
cmake -P tools/fetch_deps.cmake
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

## Documentation rule
`README.md`, `ARCHITECTURE.md`, and `docs/DECISIONS.md` must be updated in the same change whenever architecture, build steps, or scope decisions change.
