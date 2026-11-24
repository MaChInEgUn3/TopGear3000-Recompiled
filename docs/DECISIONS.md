# Decisions & confirmed facts

Single source of truth for the port's scope and decisions. Dates are absolute.

## Scope (2026-11-24)

- **Separate repository from the decomp.** `extern/tg3000-decomp` is read-only here, pinned as a submodule at a named commit (currently `f84afeb`, bumped from `2ade543` the same day) and bumped explicitly. Requests for changes in the decomp go to the list `tg3000-port-requests` in the claude-mem work state, and the owner is told; never direct edits in `extern/`.
- **"Not even 1% emulator" in the shipped game.** No CPU (65816), PPU (video chip), SPC700/S-DSP (sound), or DSP-4 model in the port. Video and sound are drawn and played natively from the data the game builds in RAM, with per-screen drawing code. The DSP-4 math becomes native C.
  - **Litmus test:** if the video or sound code could show or play *another* SNES game given its memory, it is an emulator and forbidden.
  - **Allowed:** converting cartridge graphics/colours to ordinary images, drawing from game-built RAM data (tile maps, sprite list, palette, the per-line road table) with own drawing code per screen type.
  - **Forbidden:** code that takes video register, VRAM, or HDMA-channel writes and reproduces the chip.
  - **Grey zone (owner decides when reached):** per-line effects.
  - **Sound:** same rule — nothing runs the sound CPU program; music and sound effects are rewritten natively.
- **No game data in git.** No ROM, no extracted graphics or sounds, no firmware, no screenshots. The port reads the user's legally obtained ROM at run time (`1048576` bytes, no copier header, SHA-1 `058aadf1ee719cb9e0c7333e665797f990a6381d`). `dsp4.rom` is never needed.
- **Licence:** GPL-3.0-or-later.

## Build and toolchain (2026-11-24)

- **CMake + SDL3 (`release-3.4.16`, pinned).** Fetched into `deps/SDL3` by `cmake -P tools/fetch_deps.cmake` (a permanent ignored folder), or provided by the system via `-DTG3000_SYSTEM_SDL=ON`.
- **Why SDL3:** current major version of SDL (SDL2 is in maintenance); provides one thin cross-platform layer for windowing, input, and audio streams. Built from source in `deps/SDL3` so the build does not depend on distros shipping SDL3 packages (the Ubuntu 20.04 used for the Linux check has no SDL3 package), and linked statically so the Windows `.exe` is self-contained.
- **Compilers:** GCC or Clang only (C11). MSVC is explicitly rejected in `CMakeLists.txt` because the decomp C uses `__builtin_trap()`. On Windows, the supported toolchain is MinGW-w64 UCRT (`-static` link flags so `tg3000.exe` runs standalone).
- **Decomp compile check:** `extern/tg3000-decomp/src/*.c` is built as a static library `tg3000_logic` (with `-Wall -Wextra`, without `-Werror`) and not yet linked into `tg3000`, proving the pinned C compiles cleanly on both Linux and Windows.

- **Checked (2026-11-24), decomp at `2ade543`, then again at `f84afeb`:** Windows 11 with WinLibs MinGW-w64 GCC 16
  (UCRT), and Linux in WSL Ubuntu 20.04 with GCC 9 and CMake 4.5 from Kitware: all of
  `tg3000_logic` and `tg3000` build without warnings, `ctest` passes and `--check-rom` accepts the
  ROM. The Windows `.exe` imports only system DLLs. The CI workflow's first run (push of
  `69adbec`, run 45504114049) passed on both jobs, Linux (ubuntu-latest) and Windows (MSYS2
  UCRT64).

## Platform layer: proposed, then accepted (2026-11-24)

- `docs/proposals/video-layer.md` (PROPOSED). Measured first: the decomp's screens run on the
  PC from zeroed memory (`title_card_show` returns by itself at frame 240); screens block inside
  `platform_wait_vblank`, so the game owns the loop and the port lives at that frame boundary;
  the menus' graphics arrive as whole decompressed blocks or direct ROM addresses.
- Requests filed for the decomp (claude-mem list `tg3000-port-requests`, owner told):
  1 the boot and top-level dispatcher in C; 2 `tg3000_unported` passing its name to the
  platform; 3 (enhancement) the ROM source of decompressed uploads.

## Platform layer: background (2026-11-24)

- The decomp's `src/platform.h` is shaped like the SNES chips on purpose so its verification bench can compare against the original ROM.
- The port needs a higher-level platform layer above it at the level of game data (not SNES MMIO/VRAM). That design will be proposed to the owner before it is written (Step 2).

## Name, visibility and inputs (2026-11-24, owner's decisions)

- **Repository:** `github.com/MaChInEgUn3/TopGear3000-Recompiled`, public (the owner picked
  the name; public like the decomp). "Recompiled" names the kind of port, not the technique:
  nothing is recompiled from the ROM; the logic is the decomp's readable C, and video and
  sound are rewritten natively. The README says so.
- **Inputs:** the user's ROM goes in `input/TG3000.sfc` (ignored); the port looks there
  first. `dsp4.rom`, when present, sits in `input/` too and is never read by the port.
- **Commit identity:** the account's GitHub noreply address, as in the decomp.
- **READMEs (owner, 2026-11-24):** `README.md` in English, the universal default, with
  `README.pt-br.md` as its Portuguese twin and a language switch, like the decomp. Structure
  follows the best-known native PC ports (Zelda64Recomp): no-assets notice, contents, status,
  system requirements, planned (goals only), FAQ, building (full guide in `BUILDING.md`, English),
  credits, legal. Only what exists today is claimed. The banner (`docs/img/banner.svg`) is
  original drawing and text, no game art.
- **Local notes:** `notes/` (excluded through `.git/info/exclude`) holds unverified research
  summaries; facts move into this file only once checked against the source.

## Video layer: the owner's answers (2026-11-24)

`docs/proposals/video-layer.md` is accepted. Its three questions, answered by the owner:

1. **Per-line effects.** Owner, verbatim: "chama o advisor e vá pelo melhor caminho seguindo a
   regra de sem emular nada". **Delegated under the rule**, read with the owner's own criterion
   ("drawing each line from a table the game computed is the game's effect; a general per-line
   register mechanism is the chip"), case by case:
   - the screen's code is written against the format the game's own routine writes into RAM
     for that table, never against the chip's general table grammar;
   - test: if the code would still work pointed at another table or another target, it is the
     general mechanism and is not allowed;
   - which layer scrolls and how the colour combines are written in that screen's code (the
     register calls stay ignored);
   - if an effect can only be done by implementing the general format, stop and report;
   - every per-line effect done is listed in the step's report, so the owner can veto it.
2. **The per-screen graphics table.** Owner, verbatim: "é 0% emulado sem trapacear". Read as
   the yardstick, not a blank cheque: the drawing code understands only what this game's
   routines write. So: nothing indexed by a video-memory address (named roles: background, text
   drawings, object drawings); one function per kind of screen, with no mode or layer
   parameters; pictures converted from the cartridge blocks at start-up, never decoded from the
   uploaded bytes; the text map read only through the fields the game's text routine uses
   (anything else stops with a message); objects from the game's object table where it exists,
   rather than its chip-format copy; the ignored platform functions all in one file with empty
   bodies, so review is trivial. Screens that share the background still get one table each
   (their drawings and objects differ).
3. **Picture size.** Owner: no preference. Chosen: the original 256x224, scaled to the window.

## Steps (2026-11-24)

1. **Skeleton (current):** CMake build, SDL3 dependency fetch, SHA-1 ROM verification, empty 768x672 SDL3 window, CI for Linux and Windows.
2. **Platform layer proposal:** design the game-data boundary above `platform.h` and get owner sign-off.
3. **Sound interface:** native music and SFX playback interface.
4. **Menus drawn natively:** first playable/navigable screens drawn natively from RAM data.
