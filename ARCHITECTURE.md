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
  register-style call, empty bodies, one file so review is trivial), `sound_sets.c` (the
  driver's start-up and each sound set by role: songs, instruments, samples by slot; hands
  them to the player).
- `port/sound/` — the native sound player (`docs/DECISIONS.md`, "The native sound player (set
  1)"): `convert_audio.c` (every sound conversion, at load: BRR -> PCM, envelope codes ->
  seconds/levels, pitch values -> Hz, volume scale -> gains), `sequencer.c` (the game's songs
  and effects with named state, one step per game message, calls the mixer), `mixer.c` (our
  own voices, interpolation, envelope curves, echo delay line and one fixed output headroom; SDL3 audio stream and/or WAV,
  one game frame at a time from the main thread).
- `port/video/` — `convert.c` (every format conversion), `assets.c` (named cartridge blocks,
  recognised when the game uploads them; for the race also the track's tile sets and the road's
  block `$98:9FEB`, unpacked at start-up), `palette.c` (the screen's colours as the game sets them).
- `port/screens/` — one drawing function per kind of screen: `menu_screen.c` (the menus' text
  screens: title card, password display, options menu with its two per-line effects, and VS
  screen), `results_screen.c` (the results screen with 3D starfield sprites and `$0D00`
  per-line subtraction), `champ_screen.c` (the championship, two tasks; each half taken as its
  interrupt sends it: the top at the NMI, the bottom at the line interrupt), `shop_screen.c` (the shop, two tasks:
  the top half, with its fixed windows and the item pictures recognised by their list block), `race_start_screen.c`
  (the pre-race screen: the bar, the closed shutter, the opening course view with its per-line
  effects listed in `docs/DECISIONS.md`), `ticker_screen.c` (the ticker after a race, two tasks:
  the top half's Mode 7 picture moved by the game's view block taken when the NMI runs, and the
  scrolling text objects), `race_screen.c` (the one-player race, after the NMI: the sky, the
  horizon and the HUD, then the road: BG1's road rows with the band colours and 16x16 tiles from
  the first band, and the second road (BG2) where the windows show it; the cars and objects not
  drawn yet, marked in its mask),
  driven by tables of measured facts per screen. `screens.h` holds the
  shared brightness scaling (the bench's measured rule, `(c * ((b << 16) / 15)) >> 16`).
- `docs/proposals/` — accepted design proposals (`video-layer.md`, `sound.md`).
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
       (the pre-race screen alone is drawn here, before the NMI: its frame code's streamed
       pieces belong to the picture the last NMI started; docs/DECISIONS.md)
    1. the game's frame code (its NMI, in C): JML target at $0000, then nmi_prologue() and the
       target word at $0008 (fixed table; unknown -> port_fatal with the value); its
       sound_send_frame(msg) runs one step of the native sound player
    3. draw: the active screen's own function into a 256x224 picture (title card so far;
       other screens black), then present
    4. one frame of audio from the mixer (SDL3 stream; none when TG3000_HEADLESS), then
       pace at 60.0988 fps (SDL_DelayPrecise)
    (steps 0-4 are one refresh. The one-player race loop's wait (race NMI $85:8377 with $0008 =
    $83CE and the frame requested, $00B8 bit 0; not the pause's or race_end's frame waits)
    takes N refreshes, --race-refreshes 2 (default) or 3: the first NMI takes the frame, the
    others are the HUD's; race_wait_refreshes() in frame.c, docs/DECISIONS.md "Race pacing")
```
The game owns the loop; the port lives at the frame boundary (docs/proposals/video-layer.md).
CLI modes:
- `--selftest`: runs SHA-1 test vectors without initializing SDL video (used by `ctest` target `rom_sha1_selftest`).
- `--check-rom [path]`: verifies ROM size and SHA-1 without opening a window.
- `--screen NAME` (`game` by default; test scaffolds such as `title_card`, `password_show`, `vs`,
  `options`, `results`, `ticker`, `race`): which game code to run. `race` runs `race_wrapper()`
  (with `--wram notes/race/wram_before_race.bin`); the race NMI (`$85:8377`: `$838A` tail,
  `$83CE` full frame) is dispatched; since decomp `e152ffc` the whole one-player race runs to
  `race_wrapper`'s return (its sky, HUD and road drawn, docs/DECISIONS.md). The race wrapper's own NMI
  `$83:8623` (the VS race's load waits under it) and the VS race's NMI `$83:9721` (`$0008` =
  `$9777`) are dispatched; nothing of the VS race is drawn, and it stops at its sprite lists'
  DSP-4 commands (decomp request A's rest): the whole-game movies reach it at the title's VS
  demo race (docs/DECISIONS.md, "After the race"). `race_wait` (test) runs
  `race_frame_wait()` alone from `notes/race/wram_race_wait.bin` and prints the refreshes it took.
- `--race-refreshes 2|3` (setting): screen refreshes per race step; `--race-refresh-replay FILE`
  (test): the original's recorded count per step (`notes/race/race_refreshes.txt`).
- `--frames N`: quit after N frames (tests); with no ROM it exits 77 (ctest: skipped).
- `--expect-return` (tests): exit 2 if the screen function has not returned by then.
- `--press F` (tests): hold A+B for 6 frames from frame F.
- `--movie FILE --movie-offset N` (tests): replay a decomp bench input movie (`--movie-offset-from FRAME N` switches the offset from port frame `FRAME` onwards; up to 16 of them, for the console's longer loads); `--wram-out F PATH`
  saves game memory at frame F; `--fast`: no pacing.
- `--wram-at F FILE` (tests): replace the game memory with FILE at the start of refresh F
  (before its NMI): the bench's memory handed over at a race wait; `--compare F PNG`: count the
  pixels that differ from a bench picture at frame F, only where the screen drew (the race's
  mask; all pixels for other screens). Both exit 77 when the file is absent.
- `--wram FILE` (tests): start from a saved 128 KB game memory (e.g. the original's, from the
  decomp's bench) instead of zeroed memory.
- `--shot F PATH` (tests): print the picture's fingerprint at frame F and save it as BMP (`-`: no file).
- `--shots FIRST LAST EVERY DIR` (tests): save every EVERY-th picture of frames FIRST..LAST as
  `DIR/port_NNNNN.bmp`, for comparing with the bench's pictures. A `--movie` file that is absent
  (the port's own test movies under `notes/movies/` are local) exits 77 when `--frames` is given.
- `--sound-events FILE` (tests): write the sound player's calls to the mixer (frame, step,
  voice, event, sample, Hz, volumes) and print their fingerprint at exit; `--sound-wav FILE`:
  write the mixer's output as a WAV (git-ignored paths only) and print its peak and clamped
  samples; `--sound-effect FRAME ID`: ask
  the menus' effect ID at FRAME, as the menu code does; `--sound-replay FILE`: from the port's
  first request for song 1, replace each sound message with the next one the original sent
  (recorded on the decomp's bench, a local file; exit 77 when absent). Test scaffolds:
  `--screen sound_menu` (set 1 after a race) and `--screen sound_setN` (set N, N = 0..7, by the
  game's loader, its song asked for, volumes full).

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
