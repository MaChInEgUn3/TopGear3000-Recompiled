# Architecture

## Stack
- **Language:** C11 (GCC or Clang; MSVC unsupported due to `__builtin_trap()` in the decomp).
- **Build:** CMake (>= 3.24) + Ninja.
- **Platform / Window / Audio:** SDL3 (`release-3.4.16`, static in-tree from `deps/SDL3` or system via `-DTG3000_SYSTEM_SDL=ON`).
- **Game logic:** `extern/tg3000-decomp` (read-only git submodule, compiled as `tg3000_logic` static library; not yet linked into `tg3000`).

## Directory layout
- `extern/tg3000-decomp/` — read-only submodule with verified readable C of the SNES game logic.
- `port/` — native PC port code (`main.c`, `rom.c`, `rom.h`, `sha1.c`, `sha1.h`).
- `tools/` — helper CMake scripts (`fetch_deps.cmake`).
- `deps/` — fetched third-party dependencies (`deps/SDL3`, git-ignored).
- `input/` — default user ROM location (`input/TG3000.sfc`, git-ignored).
- `build/` — out-of-source build directory (git-ignored).
- `docs/` — architectural decisions (`docs/DECISIONS.md`).

## Current data flow (Step 1)
```
main() -> rom_load() [checks 1048576 bytes + SHA-1] -> SDL_Init(VIDEO) -> 768x672 window (black clear + vsync)
```
CLI modes:
- `--selftest`: runs SHA-1 test vectors without initializing SDL video (used by `ctest` target `rom_sha1_selftest`).
- `--check-rom [path]`: verifies ROM size and SHA-1 without opening a window.

## Boundary plan (next steps)
The decomp's `src/platform.h` mirrors SNES hardware registers for differential testing. The port will introduce a native platform layer above it operating on game-level RAM data (tile maps, sprite lists, palettes, per-line road tables) and native audio, following the **"not even 1% emulator"** rule (see `docs/DECISIONS.md`).

## Build and test
```bash
cmake -P tools/fetch_deps.cmake
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

## Documentation rule
`README.md`, `ARCHITECTURE.md`, and `docs/DECISIONS.md` must be updated in the same change whenever architecture, build steps, or scope decisions change.
