# Decisions & confirmed facts

Single source of truth for the port's scope and decisions. Dates are absolute.

## Scope (2026-11-24)

- **Separate repository from the decomp.** `extern/tg3000-decomp` is read-only here, pinned as a submodule at a named commit (currently `17dab56`; earlier `e152ffc`, earlier `6c6bd22`, earlier `6ec8da5`, earlier `ca47c97`, earlier `0aa501a`, earlier `7f1daa4`, earlier `0e2a6af`, earlier `edddd50`, earlier `416560f`, earlier `d0eaf01`, earlier `9bc4a9a`, earlier `d92e6e6`, earlier `506cc7d`, earlier `6755322`, earlier `23be63d`, earlier `63e1df5`, earlier `f3f7d5a`, `372d471`, `b284d72`, `e45e619`, `b67f44e`, `2d72646`, `e966da4`, `92d4d5a`, `77b8ebd`, `49b5583`, `2ade543`, `f84afeb`, `8238cd1`, `75e8d14`) and bumped explicitly. Requests for changes in the decomp go to the list `tg3000-port-requests` in the claude-mem work state, and the owner is told; never direct edits in `extern/`.
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

## The frame boundary in code (step 2, 2026-11-24)

- **Decomp bumped to `8238cd1`** (from `f84afeb`): composed rows (screens whole) and DSP-4
  measurements; nothing in `platform.h` changed.
- **`port/platform/`** answers all 71 `platform.h` functions as the accepted proposal says:
  `ignored.c` (register-style and per-line calls, empty bodies, one file), `video_events.c`
  (graphics and palette uploads, empty until step 4), `input.c`, `frame.c`.
- **`platform_wait_vblank`** pumps SDL events first (so the game's frame code reads this
  frame's input), then runs the game's frame code (the JML at `$0000`, then `nmi_prologue` and
  the word at `$0008` through a fixed table; the two-task menus' target, the race's handler and
  anything unknown stop with a message), presents a black frame, and paces at 60.0988 frames
  per second. Quit is a long jump back to `port_run`.
- **Pacing uses `SDL_DelayPrecise`.** Measured inside the frame boundary on Windows (240-frame
  windows, 1,200 frames): both delays keep the average at 16.639-16.641 ms per frame, but with
  `SDL_DelayNS` single frames ranged 11-22 ms in a quiet window, and with `SDL_DelayPrecise`
  16.1-17.2 ms. Both runs had one system stall (a 48-60 ms frame, made up by the next ones).
- **Known difference from the console:** the game's frame code runs only at the game's frame
  waits. On the console the NMI can land mid-routine; the decomp records one case (glue-3: an
  NMI during the VS screen's graphics unpacking, with the menus' target still live). The port
  does not reproduce that preemption; the decomp's bench still replays the original
  interrupts for composed rows, so it gives no C-only frame order to copy.
- **Controls:** arrows, Z/X/A/S = B/A/Y/X, Q/W = L/R, Enter = Start, Right Shift = Select,
  Esc = quit; the first gamepad by SNES button positions (left stick as D-pad, half dead zone).
  No multitap: the game reads pads 2-5 as absent. The path from controller 1 into the game is
  tested with a scripted press (`--press`): the password display, which waits for any button,
  returns at frame 181 after a press at frame 120, and does not return without it. Real keys
  and gamepads are wired but not yet exercised (nothing is visible to press against).
- **Until the decomp's boot and dispatcher are C** (request 1), `--screen` picks which screen
  function runs from zeroed memory plus `interrupt_vectors_init()`.
- **Checked:** Windows (GCC 16) and Linux (WSL Ubuntu 20.04, GCC 9): no warnings in `port/`;
  `ctest` passes. Its game tests (`game_smoke_title_card`: the title card must return by
  itself, and does at frame 240 as the throwaway recorder measured; `game_smoke_password_press`)
  run only where a ROM is present: CI reports them skipped. VS and options run 240-360 frames
  and quit cleanly; a bad screen name stops with its message. On Windows, `--check-rom` takes
  about 0.8 s and reaching the first frame about 2.8 s: about 2 s go to starting SDL, the window
  and the renderer, not broken down further.

## Sound: proposed, then accepted (step 3, 2026-11-24)

- `docs/proposals/sound.md` (PROPOSED). Part A, the loading interface, asked of the decomp as
  request 4 (three calls at the level each assembly routine already has in hand, so its bench
  can check them; the port names blocks by a fixed table of destinations and stops on anything
  else). Part B, the native player, waits for one owner ruling: how close the mixer may follow
  the sound chip (options a/b/c; recommended b).
- Request 5: a bench measurement of the driver's output per voice (note, pitch, volume per
  tick), the reference for the native sequencer.
- Until request 4 is done, no screen of the port loads sound: the loading routines are assembly.
- Driver tables and game sound data are never reproduced in the repository (source or docs);
  the port reads them from the cartridge. The research notes and tools stay in `notes/sound/`.

## Sound: the owner's questions (2026-11-24)

The mixer question of `docs/proposals/sound.md` went back and forth; the owner's words, verbatim:
"Qual a melhor decisão seguindo a regra do 0% emulado?", then "Qual a opção que deixa perfeito
com o original mas sem emular?", then "é que a gente tava fazendo o desasembly do chip DSP-4, não
rola de fazer parecido?", "e porque não?", and "porque não fazemos o desasembly do S-DSP igual
estamos fazendo com o DSP-4?".

- **Answered:** the sound driver (the Gremlin program the game sends to the sound processor) is
  the DSP-4's counterpart: game-specific logic, read from the cartridge and rewritten in C, checked
  against the original on the bench (request 5). The S-DSP is the console's fixed mixing circuit,
  the PPU's counterpart: it has no program to disassemble, and C that reproduces its behaviour
  exactly is a model of the sound chip, which the Scope rule forbids. Bit-identical sound without
  that is not possible; indistinguishable by ear is the aim.
- **The rule, kept for now:** at load, the cartridge's sound data become ordinary units (PCM audio,
  envelope times and levels, frequencies, echo delay in ms; the few format constants these
  conversions need sit in one file); while playing, our own mixer with our own interpolation,
  envelope curves and echo, tuned against recordings of the original made on the bench until no
  difference can be heard; only the envelope shapes the game's instruments use, anything else
  stops with a message.
- **To revisit after listening:** when the menus' music plays, the owner compares it with the
  original. If the owner hears a difference they do not accept, the exception for the mixer is decided
  then (swapping the mixer is cheap: the sequencer and conversions are shared by both options).

## The title card drawn natively (step 4, 2026-11-24)

- **The first screen the port draws:** the title card (`title_card_show`: KEMCO logo, "presents",
  the TOP GEAR 3000 logo, the copyright lines). It matches the original **pixel for pixel**: the
  port's frame 119 of the screen against frame 180 of the decomp bench's `explore_options.txt`
  movie (the title card runs there from frame 61 to 300), 0 of 57,344 pixels different.
- **How, under the rule:**
  - `port/video/convert.c`: every format conversion (planar tiles to colour numbers, the game's
    BGR555 colours to RGBA), in one file;
  - `port/video/assets.c`: a catalogue of named cartridge blocks, unpacked at start-up with the
    decomp's own `decompress` and recognised by content (hash) or ROM address when the game
    uploads them; nothing is kept by video address; the game's "clear all graphics" (a 64 KB
    fill) unloads them;
  - `port/video/palette.c`: the screen's 256 colours as the game sets them (named palette blocks
    and the colours it writes one by one; the title card zeroes 48 colours its background
    picture uses, which is why the picture has black parts);
  - `port/screens/title_card.c`: one function for this screen. Background picture (drawings
    `$96:EE52` + arrangement `$85:FA8B`, built once as a 256x256 colour-number image; the
    arrangement's rows 27-31 do not exist and are entry 0, as on the console after the game
    clears video memory), then the text from the game's map at `$7F:0000` (tiles 128-166 of
    `$87:F861`; all entries "in front"), then the objects from the game's object table (16x16,
    `$87:F5BC`, priority 2). Anything else stops with a message.
- **Positions:** the background sits 64 lines down and 4 pixels right: the screen set-up at
  `$8A:C4B0` scrolls it by -4 horizontally and `title_card_show` by -65 vertically, and the
  game's convention puts a vertical scroll of -1 at the top line (its text layer). Objects at
  their table's Y. Found by comparing pictures, region by region.
- **Brightness** (`$019B`, 0-15, the game's fade): linear, `c * b * 17 / 256`. During fades the
  bench's picture differs by at most 1 in 255 per colour channel (cause not established).
  Timing, measured: the game's brightness value steps on the same frames in both (it is the
  game's own C); the bench's picture of step n shows the value the game held at step n-2, the
  port's picture shows the value of its current frame. So the port's fade can appear up to 2
  frames earlier than the bench's picture. Whether that is the bench's dump timing or a real
  display latency of the console is not established.
- **Regression test:** `game_picture_title_card` checks the fingerprint (FNV-1a 64 of the RGB
  picture) of that frame; only the hash is in the repository. `--shot F PATH` (PATH `-` = no
  file) prints it and saves a BMP for local comparison (pictures stay outside the repo, under
  the excluded `notes/`).
- **The window:** the picture is scaled with nearest-pixel sampling (SDL3's default is
  linear); `--shot` also reads the window back and checks each game pixel is a solid block:
  768x672 at (0,0), 0 mismatched pixels, with the real Windows video driver and the dummy one.
- **The screen's colours** are the one table the port keeps that the chip also keeps. It holds
  only named cartridge palettes (catalogue checked by address and size; an unknown one stops),
  plus single colours the game writes; uploads from game RAM are ignored (screens that keep
  their colours in RAM are drawn by reading that RAM). Owner told; the owner's call.
- **Which screen is drawn** comes from roles: background drawings, background arrangement,
  text drawings, object drawings each hold the block the game uploaded last; the title card is
  drawn while its four blocks hold them. Test `game_screen_sequence`: title card then password
  display in one run; once the password display loads its own blocks the port stops drawing the
  title card. Its other screens still show black.
- **Per-line effects done:** none (the title card has none).

## The password display drawn natively (step 4, 2026-11-25)

- **The second screen, identical to the original pixel for pixel:** the player's password shown
  (`password_show`), frame 137 of the screen against the decomp bench's picture of step 41208 of
  `autopilot_60k.txt`: 0 of 57,344 pixels different.
- **Same game state as the original:** the shown password depends on the player's progress, so
  the port starts from the original's own game memory, saved on the bench at the end of step
  41070 (the original enters `$8A:BE2E` at step 41071, found by the WRAM stream and by a
  one-off program-counter print in a separate local copy of the bench, reverted afterwards).
  New test option `--wram FILE` (a 128 KB memory image; local, never in git; tests using it are
  skipped where it is absent).
- **What the screen uses (measured):** the menus' common background picture, unshifted
  vertically (scrolls -4 / -1) and with its whole palette (the stars and nebula the title card
  blacks out); text drawings `$88:8601` (186 tiles, first tile number 256), palettes `$8A:C658`
  and `$85:FA37`; text entries all in front, some flipped horizontally or vertically; no
  objects (the sparkles are part of the picture); no per-line effects.
- **One function for both screens:** `port/screens/menu_screen.c`, "the menus' text screens",
  with one table of measured facts per screen (blocks per role, picture shift, text tile range,
  blank entry, flips allowed, objects or none). Anything a table does not allow stops.
- **Known difference, measured:** the screen comes up about 10 frames earlier in the port (its
  first per-frame text map upload at frame 36 of the call against 46 steps on the bench).
  Hypothesis, not measured: the port unpacks the graphics at once where the console spends
  frames on it. The pictures once lit are identical.
- **Test:** `game_picture_password_show` (fingerprint of frame 137 from the saved memory).
- **Per-line effects done:** none.

## The options menu drawn natively (step 4, 2026-11-25)

- **The third screen, identical to the original pixel for pixel, glow included:** the main
  options menu (CHAMPIONSHIP / VS MODE, `options_screens(0)`), started from the original's memory
  saved at the end of step 2955 of `explore_options.txt` (entry at step 2956, found by the WRAM
  stream and a one-off program-counter print in the separate bench copy, reverted). Frames 132
  and 133 of the call match the bench's pictures of steps 3100 and 3101 with 0 of 57,344 pixels
  different each (the item under the cursor pulses every frame).
- **What it uses (measured):** the menus' picture (scrolls -4/-1); text drawings `$88:FEC3`
  (first tile 128, not visible here) and `$88:F87F` (first tile 256), all text palette 7 and in
  front; the cursor box: 30 objects, 8x8, priority 3, drawn with 97 words the game writes one by
  one from ROM `$8A:C8E4` (`text_tiles_put_0`), recognised by content (the run's length and hash)
  like the blocks; the whole palette `$8A:C658`.
- **Text drawings now have two roles,** by the tile number their block starts at (128 or 256):
  the menus load two text blocks at once. This "tile range -> block" mapping is the point closest
  to the line; only measured ranges are accepted.
- **The first per-line effects (listed for the owner's veto), both on the text layer only, in
  one options-only function:**
  1. **Item centring:** the game's table at `$0D00` (76 lines, then 7 bands of 24 lines) holds
     one horizontal offset per band, written by `func_8AB700` (4 when the item's name has an odd
     number of letters, else 0); the port moves that band's text left by it.
  2. **Item glow:** the game's table at `$0D20` (same bands) holds one colour byte per band,
     written by `func_8AA32B` (the item under the cursor pulses through a 32-step ROM table, the
     other items hold a fixed value); the port adds its intensity to each colour channel of that
     band's text, clamped, as the measured picture shows (background and objects unchanged).
  Both read the game's own values at fixed addresses with this menu's fixed band geometry and
  stop on any other count or colour-plane value: pointed at another table or target, the code
  would not work.
- **Test:** `game_picture_options` (fingerprint of frame 132 from the saved memory).

## The VS screen drawn natively, reached through the game's own code (step 4, 2026-11-25)

- **Reached the way the original does:** the VS screen reuses graphics the options menu loaded,
  so it has no clean entry. The port starts from the original's memory saved before the intro
  (`explore_options.txt`, end of step 367; entry of `intro_screen` at step 368), runs the boot
  path in C (`--screen intro,options`: `intro_screen`, `results_init`, `options_screens(0)`; the
  original runs nothing else between them) and replays the bench movie `vs_race.txt`'s buttons
  (`--movie`, `--movie-offset 403`: aligned on the intro's first fade-in step, measured on both
  sides; "nothing else runs between the screens" comes from a subagent's reading of the
  assembly, and the memory comparison supports it for this movie only). At the VS screen's entry
  the port's memory first differed from the original's in 17 bytes outside the stack, most of
  them because the port declared port 2 empty while the bench has a standard controller there.
  **Controller 2 (2026-11-25):** the port now answers the game's port-2 reads as a standard
  controller (first pair: its 16 bits, plugged; second pair: lines high, nothing plugged) when a
  second gamepad is connected, or during a bench movie replay (the bench has a pad there); the
  multitap (players 3-5) is not supported yet. With it, only these remain: the frame counter
  `$0024`, the stack, and the sound state `$045B-$045E`, `$0463`, `$046D-$046E` (the sound sets
  are not loaded in the port: their loader is still assembly, decomp request 4).
- **First try from the options menu's own entry failed, measured:** the title screen's "skip the
  pads" flag (`$0026` bit 2) is cleared by the title screen's last instructions, in the same step
  the options menu starts; memory saved at the end of the previous step still has it, so the
  menu never read the pads. Not patched by hand: started one screen earlier instead.
- **Identical to the original pixel for pixel, animation included:** frames 3788 and 3789 match
  the bench's pictures of steps 4200 and 4201 (0 of 57,344 pixels different each).
- **What it uses (measured):** a mode-1 screen: its own background picture from drawings
  `$8D:F696` and the panels' arrangement the game writes word by word (1024 words of its fill
  `$0023`, then 896 words built by `vs_graphics_load`); the port keeps those 896 words as the
  game hands them over, named by that write pattern while the VS drawings are current, and never
  recomputes them. **This bends the recorded reading of the owner's answer 2** ("pictures
  converted from the cartridge blocks, never decoded from the uploaded bytes"): this one
  arrangement is built by game code at run time, so the only sources are the words the game
  sends or a port-side copy of that game logic (tried, rejected: game logic belongs to the
  decomp). Owner told. Decomp request 6 asks for the game to hand it over by name. Text from the menus' text drawings A (still loaded: the VS screen does not
  clear video memory), tile 189 drawn as nothing (no block covers it); objects from the cursor
  drawings and text drawings A; colours `$8A:C5D8` plus those the menu left.
- **New test options:** `--movie FILE --movie-offset N` (bench input movies), `--wram-out F PATH`
  (save game memory at a frame), `--fast` (no pacing). Test `game_picture_vs`.
- **Per-line effects done:** none here.

## The password entry drawn natively (step 4, 2026-11-25)

- **The fifth screen, identical to the original pixel for pixel, blinking caret included:** the
  password entry (`password_screen_open` then `password_entry`), reached like the VS screen
  through the game's own code (`--screen intro,options`, movie `explore_pw.txt`, which is
  identical to `explore_options.txt` before step 368, offset 403). Frames 3132 and 3133 match the
  bench's pictures of steps 3550 and 3551 (0 of 57,344 pixels different each).
- **What it uses (measured):** the menus' picture, the password text drawings `$88:8601`
  (tiles 266-434, palettes 6 and 7, in front, some flipped), the cursor box and the caret from
  the cursor drawings the options menu wrote (8x8, priority 3), and the menus' two band tables.
  Here both tables are neutral (glow intensity 0, no offset); the measured picture agrees.
  The options-only per-line function now serves the menus' band tables (options and password
  entry), with the same strict checks. No new kind of per-line effect.
- **Test:** `game_picture_password_entry`.

## Decomp at 49b5583: four new platform calls (2026-11-25)

- The decomp reached 92.40% of the game's code in verified C (754 routines), mostly the VS race,
  and added four platform calls. `video_set_m7_vofs` and `dma_stream_set_source` are ignored like
  the other screen-register and per-line calls. **`video_read_vram_word`** (the game reads video
  memory back) and **`dma_hdma_get_table`** (it reads back an HDMA slot's table address) stop
  with a message: answering them would need the port to keep chip state (a copy of video memory,
  a slot's table address), which the rule forbids. Both are used only by the VS race so far; the
  owner will be asked when the race is reached.

## The OPTIONS submenu drawn natively (step 4, 2026-11-25)

- **The sixth screen, identical to the original pixel for pixel:** the options menu's OPTIONS
  submenu (1 PLAYER FULL, 1 PLAYER SPLIT, 2 PLAYERS, PASSWORD, EXIT), drawn by the options table
  as is. Frames 2775 and 2776 match the bench's pictures of steps 3190 and 3191 of
  `explore_options.txt` (0 of 57,344 pixels different each).
- **A first comparison seemed to show a 10-frame phase** between the box that slides to the
  title and the title's glow. Measured, it was the test: the movie's buttons were aligned only
  on the intro, but the options menu comes up about 10 frames earlier in the port, so the
  button-driven box and the menu's own glow clock drifted apart. A player presses on what they
  see, so there is no phase to keep. New test option `--movie-offset-from FRAME N` adds a second
  anchor (the menu's first fully lit frame: port 2598, bench 3011, N = 413). The owner had been
  asked whether to simulate load time; owner, verbatim: "Chama o Advisor e vá pelo melhor
  caminho" — delegated: the port keeps loading faster than the console (cause still a
  hypothesis); to be revisited when sound plays.
- **Test:** `game_picture_options_submenu`.
- Also: the title card's and password display's text palettes are checked again against the
  measured ones (6 and 7); the two new platform calls that stop (`video_read_vram_word`,
  `dma_hdma_get_table`) are called only by `vs_hud_draw` and `irq_vs_split` (the VS race).

## The results screen drawn natively (step 4, 2026-11-25)

- **The seventh screen, identical to the original pixel for pixel, animation included:** the
  results screen (`results_screen_run`), from the original's memory saved at the end of step
  16489 of `autopilot_60k.txt` (entry at 16490; `tools/make_reference_memory.sh` regenerates
  it). Port frames 73 and 74 match the bench's pictures of steps 16580 and 16581 (0 of 57,344
  pixels different each). A new kind of screen, `port/screens/results_screen.c`.
- **What it uses (measured):** mode 1; no background picture (its layer is blank); the text
  from the game's map at `$7F:0000` (vertical scroll 8) drawn with three drawings sources by
  tile range (`$87:F861` from 128, the cursor word run from 256 over the start of `$88:8601`,
  then `$88:8601`), palettes 0, 1, 6, 7; 128 stars drawn from the game's sprite table at `$0900`
  (computed by the game's 3D star code; no 2D object table exists here), drawings `$91:AADF`,
  priority 0, between the text's two priorities; four named colour blocks.
- **Per-line effect (listed for the owner's veto): the results' text fade.** The game's table
  at `$0D00` (54 lines, then 21 bands of 8 lines) holds one byte per band, stepped by
  `hdma_table_0d00_settle`; the screen subtracts its intensity from each colour channel of the
  text on those lines (the screen set-up's colour math subtracts on that layer only). Read at
  fixed addresses with this screen's fixed bands; any other count or plane value stops.
- **Temporary bridge (removed 2026-11-26: the decomp's verified `nmi_target_80FC14()`, commit
  `92d4d5a`, replaced it; the picture test still passes):** the screen's frame code `$80:FC14` was assembly in the
  decomp; three lines of it are translated by hand in `port/platform/frame.c` (marked), not
  verified by the decomp's differential test. Decomp request 8 asks for it in C.
- **Test:** `game_picture_results`.

## The ticker screen: measured, blocked (step 4, 2026-11-25)

(Unblocked by the two-task kernel and the Mode 7 answer; drawn natively since 2026-11-26, below.)

- Measured (`autopilot_60k.txt`, entry at step 15858; memory saved for later in
  `tools/make_reference_memory.sh`): the ticker is a **two-task screen** (`$0008` = `$B97B`, the
  menus' NMI frame with a line interrupt at line 108), so it waits on decomp request 7 like the
  championship and the shop; and its top region is **Mode 7**: a 128x128 picture rotated and
  scaled line by line from matrix tables the game computes (channels 1-4 to the Mode 7
  registers, from WRAM `$012E-$013B`), with the scrolling ticker text as objects; the bottom
  region is blanked.
- **Owner's ruling needed before any code:** drawing a Mode 7 picture means mapping every
  screen pixel through the game's per-line matrix, the closest any screen comes to the chip.
  The title screen and the intro use Mode 7 too.

## Mode 7: the owner's answer (2026-11-25)

Asked whether a screen may draw its Mode 7 picture with its own code that reads the matrix
tables the game computed and applies them line by line to the picture converted from the
cartridge. Owner, verbatim: "Pode fazer da melhor forma possível contanto que não quebre a regra
do 0% emulado". **Delegated under the rule**, with the same safeguards as the per-line effects:
- one function per screen, reading only that screen's tables at their fixed addresses, in the
  format the game's own routine writes, with that screen's fixed line geometry; no interface
  shaped like the Mode 7 registers, nothing generic that another table or target could drive;
- the picture (128x128 map and its drawings) converted once from the named cartridge blocks;
- every value checked against what was measured for that screen; anything else stops;
- each Mode 7 use listed in the step's report, so the owner can veto it.

## The intro: measured, an open question before code (step 4, 2026-11-25)

- Measured (`explore_options.txt` steps 368-2955; port frame + 402 = bench step): about 30
  phases (`$013E` 0-32): a Mode 7 picture zooming in with 128 3D stars, a title logo built in
  slices (`$91:AB17` unpacked to `$7E:2000`, uploaded in 1,556-byte slices), a curved window
  (`$1500`/`$1540`), typed text, a colour cycle; per-band mode and layer switches by static
  tables in ROM (`$81:D712`-`$81:D74C`); window tables in RAM that change per frame.
- **Open question:** in the measured moments the Mode 7 matrix is not a table in RAM: the game
  computes it (`mode7_view`, `mode7_zoom_view`) and writes it straight to the Mode 7 registers
  (`video_set_m7a-d`, ignored by the port). The owner's Mode 7 permission covers "reading the
  matrix table the game computed"; keeping register values would be chip state. To be settled
  before any intro code.
- **Settled under the rule (2026-11-25; the owner asked not to be brought options):** in the
  zoom phases the matrix is only "scale the picture by a factor about a centre" (`mode7_zoom_view`:
  A = D = scale, B = C = 0, the centre put at screen point ($80, $6F)), and the factor and centre
  are game variables in RAM. The intro's screen code reads those variables and draws the picture
  scaled about that centre; the `video_set_m7*` calls stay ignored (they also serve the stars'
  and tables' multiplications, so a "last value written" would not even be the matrix).
- **Plan, sliced:** first the zoom phase with stars at its first measured moment (bench steps
  700/701 = port frames 298/299), the phase taken from the game's `$013E`; the ROM band tables
  become a fixed layout per phase. Before the spec: how the Mode 7 picture reaches the port (its
  uploads go through the stream calls the port ignores; the map is built in the scratch area,
  like the VS panels), whether the sliced logo's RAM buffer stays intact while shown, and the
  first colours that come from game RAM (the copy at `$81:0500`) and their cycling. Scaling
  needs rounding: do the plain fixed-point maths and measure; if only copying the chip's
  truncations would match, stop and report. Guard for every subagent spec and a grep: the port
  never writes game RAM nor calls decomp routines outside the frame dispatch.



## The intro's zoom phase drawn natively, Mode 7 included (step 4, 2026-11-25)

- **Identical to the original pixel for pixel:** the intro's first phase (`$013E` = 0: the
  Mode 7 picture zooming in, 128 stars, typed text below), port frames 295 and 296 against the
  bench's pictures of steps 700 and 701 (0 of 57,344 pixels different each). First Mode 7 use,
  listed for the owner's veto.
- **How, under the rule:** `port/screens/intro_screen.c`. The Mode 7 picture's drawings
  (`$94:8020`) and map (`$90:C343`) reach the chip through stream calls the port ignores; the
  port converts them from the cartridge at start-up (catalogue by ROM address). The map's layout
  is a measured fact (an 8x10 block at column 11, row 10 of the 128x128 map, the rest empty).
  The picture is drawn scaled by the game's own zoom variable about the game's centre variables
  (plain fixed-point, nearest pixel: it matched with no chip rounding copied), on the lines the
  intro's fixed band layout gives to Mode 7 (0-183); the typed text below from `$7F:0000`.
  Colours are read from the game's RAM copy at `$0500` (stable during the phase; the first
  screen whose colours come from game RAM). Stars from the game's sprite table, as on the
  results screen. No game RAM written, no game routine called, no game maths copied.
- **Test:** `game_picture_intro_zoom`. The intro's later phases (logo slices, curved window,
  colour cycle) are not drawn yet.

## The intro's phases 1-4 drawn natively (step 4, 2026-11-25)

- Phases 1-4 (stars spreading, the picture panning and cycling, then settling): port frames
  1315/1316 against the bench's steps 1720/1721 differ by **1 pixel each**, in the Mode 7
  picture (scaling rounding). Not copied from the chip (the rule); a known difference.
  Everything else (stars, text, window edges) matches.
- **Per-line effect (listed for the owner's veto): the intro's window.** The game's two window
  tables (`$0D00`/`$0E00` and `$0D40`/`$0E40`, from the offset in `$0136`, rebuilt every frame)
  cut the Mode 7 picture line by line; the screen reads them at their fixed addresses in the
  format the game writes and masks the picture as the screen set-up says (both windows,
  combined XOR, on that layer). Strict checks as elsewhere.
- **Test:** `game_regression_intro_settle` locks the port's own output (not exactness).

## The intro's phases 5-32 drawn natively (step 4, 2026-11-25)

- **Identical to the original pixel for pixel at the lit title logo (`$013E` = 30):** port
  frames 2298 and 2299 match the bench's pictures of steps 2700 and 2701 (`frame_02701.png`
  and `frame_02702.png`) with 0 of 57,344 pixels different each.
- **How, under the rule:** `port/screens/intro_screen.c`.
  - **Logo buffer (`$7E:2000..$7E:C765`, unpacked from `$91:AB17`):** stays intact in game RAM
    throughout the intro. When `video_upload_vram` sends a piece from `$7E:2000..`,
    `port/video/assets.c` recognises it by its WRAM source address and size (never by VRAM
    destination) and marks that named piece sent (`INTRO_PIECE_BG1_A`/`B`, `SLICE_0..15`,
    `FRAME_0..3`; cleared by the 64 KB clear). `intro_screen.c` maps BG1 (`0..89`) and
    subscreen BG2 (`0..777`) tile numbers to those pieces by fixed per-tile-range tables,
    drawing a tile only if its piece has been sent and stopping on any out-of-range tile.
  - **Colour cycle (`palette_fade_60`):** writes the cycled 16 colours of palette 6
    (`0x60..0x6F`) into `$0500..$051E` and uploads them to CGRAM `0x60..0x6F`; CGRAM `1..15`
    remain at `$89:84F2`.
  - **Per-line effect (listed for the owner's veto): the intro's curved window.** From
    `title_frame_tail` (`$0140 >= 0x015C`, `$0136 == 0x0800`), the repeat HDMA table at `$1500`
    (`window_curve_build`) masks BG1 and main-screen OBJ while leaving subscreen BG2 and OBJ
    unmasked, combined by additive colour math (`CGWSEL = 0x02`, `CGADSUB = 0x25`).
  - **Stars:** from `title_mode7_setup` (`$013E >= 7`), `stars_draw` sets OBJ priority 2
    (behind BG1/BG2 priority 1, in front of BG1/BG2 priority 0).
- **Test:** `game_picture_intro_logo`.

## The title screen drawn natively (step 4, 2026-11-25)

- **Identical to the original pixel for pixel at both moments:**
  started from `notes/title/wram_before_title_screen.bin` (`autopilot_60k.txt` step 16662,
  entry of `title_screen_run(0)` at step 16663), port frame 40 matches `frame_16752.png`
  (moving moment) and port frames 155..164 match `frame_16867.png` / `frame_16868.png`
  (settled moment) with 0 of 57,344 pixels different each. Priority-0 objects are drawn
  behind non-transparent Mode 7 pixels (and priorities 1..3 in front), while preserving
  OAM `127..0` sprite-vs-sprite order.
- **How, under the rule:** `port/screens/title_screen.c`, `--screen title` ->
  `title_screen_run(0)`.
  - **Assets (`port/video/assets.c`):** `$9A:EB44` is unpacked at start-up to extract the
    Mode 7 tiles (`$7E:8633` 4608 B, `$7E:9833` 1728 B), Mode 7 base map (`$7E:80BA` 90 B),
    and four OBJ blocks (`$7E:8256`, `$7E:B385`, `$7E:9F07` recognised by size+hash from
    `$7F:0000`, and `$7E:A071` recognised by WRAM source+hash). Mode 2 BG1 horizon map and
    tiles (`$7F:6460`, `$7F:6660`, `$7F:6860`) and palettes (`$7E:8114`, `$7E:81E2`,
    `$7E:8202`, `$7E:8132`, `$7F:6440`) are read from intact game RAM.
  - **Fixed region layout (from ROM HDMA tables `$8A:88DE`..`$8A:88F9`):** lines 0..167 are
    Mode 7; lines 168..223 are Mode 2 (BG1 horizon; BG2 empty; Mode 2 per-tile offset unused).
  - **Mode 7 use (listed for the owner's veto):** lines 0..167 draw the 128x128 Mode 7 map
    (with `tile_anim_step` patch from `$7E:8000..$7E:809B` when `$0142 != 0`) using matrix
    `DP+VIEW_MATRIX` (`$0114..$011B`), fixed centre `(128, 64)`, wraparound (`M7SEL=0`), and
    plain fixed-point math (`floor_div256`).
  - **Per-line effects (listed for the owner's veto):**
    1. **Sky gradient (`$7E:2C00` -> `$7E:2800` / `$7E:2A00`):** lines 0..126 read
       `$7E:2800 + 4*y` and lines 127..223 read `$7E:2A00 + 4*(y-127)` for the per-line
       backdrop colour (CGRAM 0), with strict checks on the table header and CGADD=0.
    2. **Mode 7 / Mode 2 scroll tables (`$0D00` M7VOFS/BG1VOFS, `$0D20` M7HOFS/BG1HOFS):**
       read at fixed addresses with the screen's fixed band counts (`1, 127, 40, 1, 0`) written
       by `mode7_view`.
- **Tests:** `game_picture_title_screen`, `game_picture_title_settled`.

- **Measured (2026-11-25):** the port's game state matches the bench's frame by frame (port
  frame F = bench step F + 16707: the slide `$0136`, objects, scroll tables). The moving
  moment's second picture pairs that step's scroll table with the previous step's objects:
  the bench's picture timing, not a port difference. At the settled moment 35 pixels differ
  where object 8 (priority 0) overlaps the Mode 7 picture: drawing priority-0 objects behind
  the picture fixes those lines but breaks lines 160-167 (objects 0-2), so the ordering rule
  there is not established; left as a known difference. `game_picture_title_screen` locks the
  exact frame 40.

## Decomp requests 8 and 2 delivered (2026-11-26)

- Decomp `92d4d5a`: `nmi_target_80FC14()` (request 8) replaces the port's hand-translated bridge;
  `platform_unported(const char* what)` (request 2) is called by `tg3000_unported` before its
  trap, and the port stops with "the game reached code not converted to C yet: <what>".

## Sound request 4, step 1: the driver's start-up (2026-11-26)

- Decomp `e966da4`: the driver's start-up upload is C (`sound_boot`, `sound_boot_upload`) on
  four platform calls, different in shape from the proposal's single `sound_driver_boot`:
  `sound_boot_begin`, `sound_boot_block(spc_dest, src, size)` (one per driver block),
  `sound_boot_start(spc_entry)`, `sound_clear_ports`. All carry the sound CPU's program, which
  the port never runs: they are ignored (in `port/platform/ignored.c`). The scene loader's
  calls (songs, instruments, samples) come next from the decomp.

## Sound request 4, step 2: the scene sets (2026-11-26)

- Decomp `2d72646`: the set loader `sound_load_set` ($86:DDEF) is C on four calls:
  `sound_upload_begin`/`_end` around each upload; `sound_upload_block(spc_dest, src, size)` (a
  block unpacked into game RAM; the destination names its role: the songs, the instruments and
  settings, or last the sample directory); `sound_upload_sample(slot, spc_dest, src, size,
  spc_loop)` (BRR bytes from the cartridge; the loop as an absolute address).
- `port/platform/sound_sets.c` keeps the current set by role: songs, instruments, and samples
  by slot with their loop as an offset; the directory is ignored (redundant); any other
  destination, a slot past 31, or a loop outside its sample stops. Nothing of the sound CPU's
  memory is modelled. The callers of the loader (the scene changes) are still assembly in the
  decomp, so no screen loads a set yet; the native player (step 3) reads these.

## Native sound player, step 1: a set loaded and converted (2026-11-26)

- Test scaffold `--screen sound_menu` calls the game's own `sound_load_menu(DP, 1)` (as the top
  loop does after the shop) so the menus' set reaches the port at run time: songs (2,512
  bytes), instruments and settings (1,066 bytes), 9 samples in slots 0-7 and 16, as measured.
- `port/sound/convert_audio.c`: the one conversion file for sound: each sample's BRR bytes
  become 16-bit PCM with its loop point when received (two samples do not loop). Test
  `sound_set_menu`. No sequencer or mixer yet.

## The DSP-4: no chip-protocol model in the port (2026-11-26)

- Decomp `13e8350` offered `Dsp4Chip` (reset/write/read/lost, native maths behind a byte
  protocol that reproduces the chip's quirks) to put behind `dsp4_port_write16/read16`.
  Decided under the rule (the owner asked for decisions under "0% emulated", not options): not
  in the port. A byte-level interface with the chip's behaviour is a model of the DSP-4. The
  port needs the race's C to call native command functions directly (as `dsp4_edges`,
  `dsp4_row`, `dsp4_sprite` already are); `Dsp4Chip` stays a bench tool. The decomp agreed:
  `e45e619` moved it to `tools/bench/` and plans one native function per streamed command
  (`$01`, `$09`, `$06`, `$0D`), called directly by the race's C. Asked of the decomp
  (`docs/decomp-requests.md`, A). The port's `dsp4_port_*` keep stopping with a message.

## The game's own start-up (decomp request C, step 1; 2026-11-26)

- Decomp `b284d72`: `game_boot()` (reset path, WRAM clear keeping the save areas, start screen
  registers, `sound_boot`, vectors, `save_data_check`). Two new platform waits,
  `platform_wait_vblank_start` and `platform_wait_dsp4_start`, return at once in the port.
  `--screen boot` calls it. From zeroed memory, `--screen boot,intro` reaches the intro's zoom
  picture that matched the original pixel for pixel (same fingerprint as from the bench's saved
  memory): test `game_boot_intro`, which needs no saved memory. The top loop comes next.

## The whole game runs from the game's own top loop (2026-11-26)

- Decomp `f3f7d5a`: `game_main()` (boot, title card, sound set 0, intro, then the main menu and
  the championship loop). It is now the port's default (`--screen game`). With no input the port
  runs boot, title card, intro and the options menu, all drawn natively (test
  `game_main_to_options`: frame 3300 is the options menu's picture that matched the original).
  It stops at the championship screen until decomp request D (the two-task kernel) lands.
  `--screen` lists stay as test scaffolding.

## Two tasks (decomp request D, step 1) (2026-11-26)

- Decomp `63e1df5`: `platform_task_split(task)` and `platform_task_join()`, plus the split
  frame handlers (`irq_menu_frame`, `irq_menu_frame_tail`, `nmi_menu_frame`,
  `nmi_menu_frame_tail`, `nmi_tail`).
- Implemented in `port/platform/frame.c` using a second SDL thread (`SDL_CreateThread`) and
  two SDL semaphores (`SDL_Semaphore`) for strict one-thread-at-a-time hand-off. Task 2 starts
  blocked until task 1's next `platform_wait_vblank`, which runs: SDL events/input,
  `irq_menu_frame()`, `irq_menu_frame_tail()`, task 2's turn (until its `platform_wait_vblank`
  or return), draw + present, `nmi_menu_frame()`, `nmi_menu_frame_tail()`, `nmi_tail()`, and
  pacing. `platform_task_join()` and quit paths signal task 2 to exit cleanly via `longjmp`
  and join the thread. Nested splits call `port_fatal`.
- `--screen ticker` runs `ticker_screen()`. Test `game_ticker_runs` verifies it returns
  cleanly from `notes/tick/wram_before_tick.bin` (at frame 3, game frame counter 7957; picture
  not drawn yet).

- **Fixed (2026-11-26):** the ticker returned after 3 frames because, with a split active, the
  port called `nmi_menu_frame()` without `nmi_prologue()` first (every NMI starts with it: the
  brightness fade lives there), so the screen never lit and its loop ended at once. With it,
  the ticker runs 625 frames from the saved memory, as long as the original's (lit about 560).
  The decomp's `platform.h` contract text omits `nmi_prologue()`; told the decomp.

- **Fixed (2026-11-26): the whole game runs into the championship.** With
  `autopilot_60k.txt` (offset 150) the port stopped at frame 3173: the menu screens' recogniser
  matched the championship (NMI target `$B97B`) because the menu roles stay loaded after the
  options screen. The menu screens all run with NMI target `$FC5F`, so the recogniser now also
  needs `$0008 = $FC5F` (measured at frames 3165-3175 and 4322); the checks on lit menu frames
  are unchanged. Test `game_main_movie_to_champ` (championship lit at frame 3200, not drawn yet).

## The championship drawn natively (step 4, 2026-11-26)

- **The first two-task split screen (`$0008` = `$B97B`), reached from `game_main()` with
  `autopilot_60k.txt` (offset 150):**
  - **Moment 1 (settled menu, `NAME` / `CONTROL` / `SPEED` / `RACE`):** port frame 4067 matches
    `frame_04365.png` (`0` of `57,344` pixels different) and port frame 4215 (4214 before each
    half was taken at its interrupt, below) matches
    `frame_04366.png` (`0` of `57,344` pixels different). Before the `Down` press at movie step
    4360 (port frame 4212), the console's unthrottled `$80:C2BB` menu loop runs ~3 passes per
    display frame while the decomp's `champ_menu` runs one pass per frame (`platform_wait_vblank`
    at the end of each pass), so the 16-step cursor colour cycle (`dp+$1E`) advances at 1x speed
    in the port until `Down` resets `dp+$1E = 0` on both sides.
  - **Moment 0 (opening slide, no objects):** since each half is taken at its interrupt (below),
    every frame of the fade-in and the slide matches the bench half by half, `0` pixels each:
    the top half (lines `0..107`) at port frame N = bench `frame_(N+177).png` (3173..3204), the
    bottom half (lines `116..223`, plus the 8-line blanked split gap `108..115`) at N =
    bench `frame_(N+176).png` (3173..3195). Once task 2's bar has closed (port 3196 on), the
    bottom half stands still and whole frames match: port 3196..3204 = `frame_03373..03381.png`,
    `0` of `57,344` pixels different. The remaining one frame between the halves is the
    console's CPU time in the load (below).
- **How, under the rule (`port/screens/champ_screen.c`, `port/video/assets.c`):**
  - **Screen recognition:** `$0008 == 0xB97B`, `port_split_active()`, and both halves have noted
    all five unpacked ROM streams and captured their initial BG1 map, BG2 map, and fire band
    frame. Any non-championship stream noted via `video_note_source` (or the 64 KB clear) clears
    the championship state, so the shop and ticker (`$0008 == 0xB97B`) are not taken for it
    (the shop is drawn by its own screen since 2026-11-26, below).
  - **Unpacked ROM streams (`video_note_source`, moved from `ignored.c` to `video_events.c`):**
    `$87:8190` (80 4bpp OBJ font tiles `256..335`), `$90:C320` (3 4bpp OBJ bar tiles `336..338`),
    `$87:86BE` (160 4bpp OBJ tiles `340..499`), `$8F:83CC` (30 4bpp BG tiles `0..29`), and
    `$98:8000` (319 4bpp tiles: BG tiles `32..350`, with the last 95 tiles at byte offset 7168
    also serving OBJ tiles `0..94`) are unpacked and converted once at start-up, and marked
    loaded per half by which task thread is running (`port_current_task_half()`), ignoring the
    VRAM word.
  - **Captured game-built `$7F` pieces per half (by offset from the task's `$7F` base
    `$0000`/`$8000` and byte size, never read live from `$7F` at draw time):**
    1. `bg1_map`: 2048 B from `base + $0000` (32x32 map).
    2. `bg2_map`: 2048 B from `base + $0800` (32x32 map).
    3. `fire_band`: 512 B from `base + $11C0 + k*$0200` (`k = 0..8`), patching rows `3..10` of
       the half's BG1 map (tiles `256..350`, palette 2, priority 0).
    4. `bg2_panel_rows`: 896 B from `base + $0800` (`champ_big_panel`), updating rows `0..13` of
       the half's BG2 map.
    5. `noise_tiles`: 512 B from `base + $0000` (`champ_noise_panels`), converted as 16 4bpp
       BG2 noise tiles `352..367` (palette 3, priority 0).
  - **Per-half layout, palettes, brightness, and objects:** top half (task 1, `DP = $0100`,
    lines `0..107`) and bottom half (task 2, `DP = $0200`, lines `116..223`), separated by the
    8-line blanked IRQ split gap (`lines 108..115`, forced blank during `irq_menu_frame`). Each
    half validates its shadow register block (`$019C-$01C1` / `$029C-$02C1`: Mode 1, 8x8,
    `TM = 0x13`, fixed map/tile bases, no windows, no colour math), reads its BG1 vertical
    scroll (`$019E` top, `$029E - 0x74` bottom), reads its 256 colours from `$81:0500` (top) or
    `$81:0700` (bottom), scales its band by its own brightness (`$019B` top, `$029B` bottom), and
    draws its 8x8 priority-0 objects from its own OAM pointer (`$016E` in `{$0900, $0D00}` top,
    `$026E` in `{$1100, $1500}` bottom), clipped to its band in Mode 1 priority order
    (`backdrop -> BG2 prio 0 -> BG1 prio 0 -> OBJ prio 0 -> BG2 prio 1`).
  - **Per-line effect (listed for the owner's veto): the championship BG2 panel shift
    (`$81:1B00`).** The 22-byte table at `$81:1B00` holds two 3-entry direct 16-bit vertical
    offset sub-tables (`$1B00..$1B08` for lines `0..107`, `$1B0C..$1B14` for lines `116..223`,
    separated by the fixed 8-line gap `$1B09..$1B0B = 08 20 00` and terminated by `$1B15 = 00`),
    stepped by `menu_bar_lines` and `menu_bar_lines_end`. When a bar is opening or closing,
    `menu_bar_lines` writes entries 1 and 2 with the current frame's height `l`, then steps `l`
    by `+/-4` before `menu_bar_lines_end` writes entry 0 for the next frame (so the three line
    counts in RAM sum to `112` or `104`); `champ_screen.c` recovers the active frame's entry 0
    (`cnt0 -/+ 4`, `val0 +/- 4`) so the three segments sum to the half's 108 lines.
- **Tests:** `game_main_movie_to_champ` (moment 0, port frame 3196 = `frame_03373.png`; was 3191,
  which matched no single bench frame), `game_picture_champ_slide` (port 3191, both halves
  moving: top = `frame_03368.png`, bottom = `frame_03367.png`, 0 pixels each),
  `game_picture_champ_menu` (moment 1, port frame 4215; was
  4214, same picture hash), `game_picture_champ_name` (`explore_menu.txt` offset 150, NAME
  entry `$80:C6D4`, port frame 4648 against `frame_04801.png`, `0` of `57,344` pixels different;
  port frame 4348 against `frame_04501.png` and port frame 4649 against `frame_04802.png` also
  `0` of `57,344` pixels different), `game_picture_champ_controls` (`explore_menu.txt` offset 150,
  CONTROL submenu `$80:CB4A`, port frame 5033 against `frame_05186.png`, `0` of `57,344` pixels
  different).
- **Submenus (`champ_name_entry` `$80:C6D4`, `champ_controls` `$80:CB4A`, `champ_gears` `$80:CEE2`,
  and `champ_menu_back` `$80:C506`):**
  - Four additional cartridge blocks are unpacked and converted once at start-up and recognised by
    content hash when queued from `$7F:(base + $2000)` / `$2800` / `$4000`:
    `$8C:8000` (`CHAMP_NAME_BG_TILES`, 173 4bpp BG tiles `32..204`),
    `$9F:8000` (`CHAMP_CTRL_BG_TILES`, 207 4bpp BG tiles `32..238`),
    `$8C:8B54` (`CHAMP_GEARS_BG_TILES`, 166 4bpp BG tiles `32..197`), and
    `$87:898E` (`CHAMP_GEARS_OBJ_TILES`, 48 4bpp OBJ tiles `208..255`).
  - The 512-byte BG2 panel patch from `$7F:(base + $08C4)` updates byte offset `$00C4` (rows
    `3..10`, cols `2..29`) of the half's BG2 map; in NAME entry and SPEED/GEARS, `TM = 0x12`
    disables BG1; in CONTROL (`TM = 0x13`), the 512-byte upload from `$7F:(base + $11C0)` sets
    BG1 rows `3..10` to tiles `172..238` (palette 0).
  - Submenu objects use 8x8 and 16x16 (`is_large`) sprites in palettes `0..2` with priorities `2`
    and `3` (drawn in front of BG2 priority-1 shutter tiles).
- **Open:** the cursor glow before the first key press cycles slower than on the console (the
  console's menu loop runs several passes per frame); only the 4 letters of the lit item differ.
  The halves' remaining one frame during the opening: see "The championship's halves, each as
  its interrupt sends it".

## The championship's halves, each as its interrupt sends it (2026-11-26)

- **Was:** during the opening slide the bottom half ran 2 frames behind the top half compared
  with the original (each half alone exact). Measured per frame, port and bench (game memory at
  every step, `notes/champ/slide/`: `trace_bench.sh`, `match.py`), it is two things of one frame
  each:
  1. **The draw point (fixed).** On the console the top half's registers, colours and sprite
     table go out in the NMI (`nmi_menu_frame`, from `$0100`'s block, `$0500` and the table at
     `$016E`) and task 1 runs right after it; the bottom half's go out in the line interrupt
     (`irq_menu_frame`, from `$0200`'s block, `$0700`, `$026E`) and task 2 runs right after it.
     The port draws after task 2's turn and after task 1's, so it read both tasks' latest
     writes one picture early. In the fade only the NMI changes anything (no effect); in the
     slide task 1 moves `$019E` itself, so the top half came one frame early: the top matched
     bench N+177 in the fade but N+178 in the slide, the bottom N+176 throughout.
  2. **The console's CPU time in the load (not imitated).** Both tasks unpack the same five
     streams (`queue_unpacked_upload`). On the bench the first four uploads come out at the same
     steps for both tasks (3315, 3316, 3321, 3323), but task 1 finishes the large `$98:8000`
     one later in real time (task 2 queues it at step 3334, task 1 in the next frame's top half),
     so task 2 starts its fade one frame period (`$0024`) earlier: `$029A` at step 3344, `$019A`
     at 3345; task 2's bar hook at counter `$0CEE`, task 1's first slide step at `$0CF0`. In the
     port the tasks unpack instantly, are in step, and both fade in the same frame. Matching
     it would need the console's CPU speed per task (a CPU model), like the intro's 35 load
     frames ("The native sound player: the race sets"); not imitated. Under the agreed order
     the hand-off has nothing to change: each task's turn, slot drain (`upload_slot_drain`,
     NMI for task 1, IRQ tail for task 2) and hook come in the same order on both sides.
- **How (listed for the veto):** `champ_screen_note_nmi()` (after `nmi_prologue`, just before
  `nmi_menu_frame`) and `champ_screen_note_irq()` (just before `irq_menu_frame`) take, for
  their half, the tasks' direct pages `$0100..$02FF`, the half's 256 colours and its sprite
  table at the pointer, which is what each handler sends; `champ_screen.c` draws from those
  (registers, scroll, colours, objects, brightness). The per-line table `$1B00` stays read at
  the draw point (each half's hook writes it right after the interrupt, for the same picture).
  Like the ticker's view block (taken when the NMI runs). No game RAM written, no counter
  stepped, no routine retyped. `platform_task_split` clears what was taken.
- **Checked:** every fade and slide frame matches per half (above); moment 1, NAME, CONTROL:
  0 pixels at port 4067, 4215, 4348, 4648, 4649, 5033 (4214 became 4215, same picture). The
  bottom half's taking at the IRQ is not yet told apart from a read at the draw point by any
  compared frame (task 2 changes nothing it draws in them); it follows the same order as the
  top's. The shop and the ticker do not use it; all tests pass.
- **For the decomp:** the question whether the original gives task 2 a head start (or it is only
  CPU time) is filed as `docs/decomp-requests.md`, L.
- **Open:**
  - the halves' one frame (above), unless the decomp's answer to L shows an order to follow;
  - the first lit frame: the bench lights the bottom half at brightness 1 in `frame_03348.png`
    and the top in `frame_03349.png`; the port draws nothing at brightness 1 (port 3172: the
    halves' pieces are not all in yet, the screen is not recognised) and starts at brightness 2
    (port 3173). What the bench's brightness-1 pictures show was not compared.

## Decomp at 416560f: requests J and C's top loop delivered (2026-11-26)

- **J:** `video_note_vs_panels(vram_word, rows)` names the VS panels before the game writes them.
  The port now builds the arrangement from the 14 WRAM rows it names, as the decomp's
  `platform.h` describes it (two halves, each row twice, ORed with $0000/$0400 and $0800/$0C00);
  the earlier capture by write pattern (the recorded bend of the rule) is removed. The VS picture
  test is unchanged (same picture hash).
- **C, the top loop's ends (`ecb9653`):** end of the championship, last track, not in the top
  five, the two-player loop. No change needed in the port. The traps left are named one level
  down; the port reports which ones its full runs reach.
- **A, `416560f`:** the DSP-4 road command ($01) is being measured again (not waiting on the
  owner any more); nothing to adopt yet.

## The native sound player (set 1) (step 3, 2026-11-26)

The menus' music and their three effects play natively, under the rule "nem 1% emulador": no
sound-CPU program runs, and no model of the SPC700 or of the S-DSP exists in the port. Spec:
the local `notes/sound/SEQUENCER.md` (the driver's song format and timing, read from its code
and checked on the decomp's bench).

- **The sequencer (`port/sound/sequencer.c`)** is a rewrite against the game's song format,
  with named state:
  - per channel: order list, pattern position, wait, instrument, volume;
  - per voice: note pitch, tune, hold, volume, effect owner, and the two per-step pitch tables
    (offset sequence, sweep).
  - **Clock:** one step per game message (`sound_send_frame`), as the driver takes one. Each
    step runs in the driver's order: the message (volumes, music request, effect request), a
    song start, each voice's output, the pitch tables, the pending note starts, then the song's
    ticks. Ticks come from a tempo accumulator: tempo/197 a step at 60 Hz.
  - **Notes:** a note read in a tick cuts its voice at once and starts on the next step. A note
    is held for its instrument's hold time, or until a let-go event (`$E8`). Once let go, it
    falls at the instrument's sustain rate.
  - **Effects:** effect N plays instrument N-1 at the effect note, on the voice (of those the
    request allows) whose hold runs out first. Music notes skip that voice until the effect's
    hold has run out.
  - **Volume:** the music volume reaches a channel at its volume commands and at each pattern
    start, as in the original.
  - **Output:** the sequencer calls the mixer's own interface (start with sample, Hz,
    left/right gain and envelope; pitch; volume; let go; cut). It never builds register values.
- **What stops with a message (`port_fatal`):** only things no song or menu effect of the
  game uses:
  - any song command other than notes, rest, drum, instrument, volume, `$E8` and `$FF`;
  - an instrument with noise or the chip's direct gain, when it is keyed;
  - an effect request carrying its own note, and the second effect request;
  - 50 Hz timing, and the "tick every step" setting.
  - (Echo and the engine sound stopped here too until the race sets: see "The native sound
    player: the race sets".)

  Without a loaded set or a started driver (tests that start from saved memory), nothing plays.
- **Read from the cartridge, never copied into source:**
  - songs, instruments and the two pitch tables, from the set's blocks as the game sends them;
  - the volume-step table, from the same blocks;
  - the driver's left/right balance table, read out of the driver's start-up upload
    (`sound_boot_block`, now in `port/platform/sound_sets.c`). Every note of the game plays
    at its centre entries, which sit slightly left.
  - The driver's 192-step pitch octave is computed (it equals `round(16384*2^(r/192))`,
    measured).
- **Converted at load, in one file (`port/sound/convert_audio.c`):**
  - each sample's BRR to PCM (15-bit, given a gain to full scale);
  - each instrument's ADSR codes to seconds and levels: a linear attack time, a time constant
    for the fall to the sustain level, the sustain level in eighths, a time constant for the
    fall once let go (none when its code is 0), and an 8 ms cut;
  - the driver's pitch value to Hz;
  - the chip's volume scale (127 = full) to a gain.

  The chip's envelope rate codes and these scales sit only there.
- **The mixer (`port/sound/mixer.c`), ours:**
  - 8 voices;
  - 4-point cubic Hermite (Catmull-Rom) interpolation, not the chip's Gaussian filter;
  - smooth exponential envelope curves through the converted times (the chip steps in
    integers): the timing is the original's, the curve is ours;
  - no echo for sets 0-1 (none of their instruments asks for it; the echo came with the race
    sets);
  - 48 kHz stereo float, rendered one game frame at a time from the main thread at the frame
    boundary. It is pushed to an SDL3 audio stream (`SDL_OpenAudioDeviceStream`), which starts
    with 3 frames of silence queued against jitter; frames are dropped when more than 6 are
    queued (fast runs).
  - With `TG3000_HEADLESS` set, no device is opened.
- **Test options:**
  - `--sound-events FILE`: the player's calls to the mixer, with frame and step, plus a
    fingerprint printed at exit;
  - `--sound-wav FILE`: the mixer's output (git-ignored paths only);
  - `--sound-effect FRAME ID`: an effect request written as the menu code writes it.
- **`--screen sound_menu`** now runs the driver start-up first, then the set. It also fades the
  master volume up, as the game does after loading the set.
- **Checked against the bench (`voices_15790_22000`, song start at bench frame 15861 = step 0;
  port: `--screen boot,sound_menu`, song start at port frame 12):**
  - Every voice's key-on (step, sample, pitch, left/right volume), cut, let-go and pitch change
    is identical over all 1,597 steps of the bench run: 838 of 838 events.
  - The run includes 8 menu effects (`$13` at steps 1251 and 1492; `$11` at 1342, 1362, 1382,
    1402, 1422 and 1442), asked at the same steps. The voice each one took and its pitch tables
    (32 pitch changes) match.
  - `$12` has no bench reference; it plays without error.
  - Tests: `sound_menu_events` (the fingerprint of that run) and `sound_menu_wav` (the mixer,
    headless).
- **Set 0 through the whole game (`game_main`, `autopilot_60k.txt`, offset 150):**
  - The intro's song starts at port frame 253; the bench has it at frame 369 (bench minus
    port: 116, not the movie's 150).
  - The first menu effect the game itself asks for comes at port frame 3011 (song step 2758);
    the bench's at frame 3163 (step 2793): bench minus port 152, the movie's 150 within 2.
  - Both sides send one message per frame in between. So at the menus the port's song is
    about 35 steps (0.6 s) behind the original's relative to the game's frames. Where the
    35 frames go (the port's intro or the frames before the song) is not located yet; the
    player only counts the messages it gets.
  - Up to step 2747 (before the first effect) every music event is identical: 2,273 of 2,273.
    The effects after it fall on other steps and voices than on the bench, because of that
    shift, so they are not compared.
  - Test `sound_set0_events_game_main`.
- **Open:**
  - The owner listens to the menus' music (a local WAV, never in git) against the original.
    The mixer's curves, interpolation and output level are tuned by ear from there.
  - Done (2026-11-26, "The mixer's output level"): the output level was the converted samples'
    scale alone and clipped (12 of about 3 million samples in the 31 s menu WAV); now one fixed
    headroom gain, measured, and nothing clips.
  - The set-0 shift of about 35 frames between the song and the game's frames: located (see
    "The native sound player: the race sets", set 0).
  - Live playback through the audio device runs (checked with SDL's dummy audio driver); its
    smoothness on real hardware (queue underruns with frame jitter) is not measured.
  - Echo and the engine sound: done with the race sets. Noise: no set uses it.
- **Decided (2026-11-26, under the rule): the format constants in the player.** `sequencer.c`
  names a few numbers of the song format (the command ranges, the instrument block's layout, the
  NTSC tempo divisor 197, the drum pitch, the effect note) and `sound_sets.c` the place of the
  balance table in the driver's start-up upload. They describe the game's data, like a picture's
  layout, and are read against the cartridge's own blocks; no table of the driver is copied
  (the pitch table is computed, the volume steps and balance are read from the set and the
  start-up upload). Allowed; any new one goes into the same named lists.
- **Open:** in the whole game, the intro's song (set 0) starts about 35 steps later than on the
  bench relative to the game's own timing; located since (the next section). Nobody has listened
  yet: the owner has the menus' recording to compare by ear.

## The native sound player: the race sets (step 3, 2026-11-26)

- **Set 0's 35 steps: the original's intro load, not the sound.** Measured on the bench
  (`autopilot_60k.txt`, marks on routine entries, below):
  - The set-0 loader (`$86:DDEF`) runs from bench frame 300 to 367: 13 uploads (the songs, the
    instruments, 10 samples, the directory), one message each. In the port each upload costs one frame (the loader waits for one), so the port's load
    is shorter; that is before the song and inside the 150-frame anchor.
  - The song request (`top_volumes`, `$80:89EB`) and `intro_screen` (`$80:EE56`) come at 368.
    The intro's load (five decompressions, the mode 7 map, the clears) reaches its first wait
    (`$80:F115`) at 403: 35 frames of the console's CPU time.
  - During those 35 frames the console's NMI keeps sending one message a frame (logged:
    368-403). So the original's song is already 35 steps in when the intro starts fading in. In
    the port the load takes no frame, and the song starts with the intro's first wait, at port
    frame 253 (= 403 - 150).
  - Not the ready handshake (the port answers "ready" at once, and the bench sent a message every
    frame here), and not the message order (on both sides the song request goes out with the
    first message after the load).
  - **Not fixed in the port.** Matching it would need the port to know how long the console's
    CPU took for the intro's load (a model of the 65816's speed), or to skip the song's first
    0.6 s.
  - **Decided (2026-11-26, under the rule):** not imitated. Making the port wait the console's
    35 load frames would copy the 65816's speed, a piece of CPU model; the port loads instantly
    everywhere else too. Accepted as a known difference.
  - So, from the intro on, the port's song runs 35 steps (0.58 s) behind the original's
    relative to the pictures. The effects the game asks for land on the right frames, and every
    new set restarts its song, so the shift does not carry past the set.
- **The original's messages, measured:** the local bench clone (`notes/sound/seq_tools/bench`,
  not the decomp) logs every port write by the game's message sender (`$86:DFFB`) and marks
  chosen routine entries (`BENCH_MSG_LOG`, `BENCH_PC_MARKS`; local tool `msgs.py`).
  - `autopilot_60k.txt` over 60,000 frames reaches set 3 (frame 7981), set 4 (17461), set 5
    (30188), set 6 (41859) and set 3 again (54200), with set 1 between the races.
  - Set 7 (the VS race) needs the DSP-4's firmware on this bench, which is not available here.
  - Set 2 is reached by no bench movie; the code that loads it is not located.
- **What the race messages use** (all five races):
  - the engine flag on every race step, always on voice 6;
  - race effects `$12` (the engine itself, volume 0, voice 6), `$14`, `$19`, `$1D`, `$1E`,
    `$20`, `$21`, `$22`, `$24`, `$26`, each on one of voices 5-7;
  - no second effect request, and no effect gives its own note;
  - music volume `$7F`, and the master volume fading out at the race's end.
  - Effects never take the songs' voices: the race songs have 4 channels (voices 0-3).
- **Implemented:**
  - **The engine (`sequencer.c`):** at each step with the engine flag, the engine byte's voice
    plays at that byte's volume (its top 5 bits), and the two pitch bytes are added after the
    pitch table (`convert_pitch_hz` already took that offset). A song start clears it with the
    song's voices.
  - **Echo, the sequencer's side:**
    - Each instrument's echo bit goes with its notes to the mixer (`mixer_start(..., echo)`;
      event `on ... echo`).
    - The set's echo settings come from the instruments-and-settings block (offsets
      `$400`-`$402`: volume, feedback, delay). Measured: sets 2-7 ask for a 32 ms delay with no
      feedback; sets 0-1 for no delay (and none of their instruments uses the echo).
    - Each step, the echo volume is scaled by the music and master volumes (both doubled) as
      the driver scales it: `((v*music + $80) >> 8) * master + $80) >> 8`. The mixer is told
      when it changes. The event `echo delay_ms= feedback= vol=` is written only for sets with
      a delay, so the earlier fingerprints did not change.
    - `convert_audio.c`: a delay unit is 16 ms (512 stereo frames of the chip's echo memory at
      32 kHz); the gains on the chip's scale /128. A delay beyond the chip's range, or an echo
      volume that would not stay positive, stops with a message.
  - **The mixer's echo, ours:**
    - one stereo delay line (up to 240 ms), fed by the echo voices after their left/right
      gains;
    - output = dry sound × master + delayed sound × echo volume; the feedback (0 in every set)
      sends the delayed sound round again;
    - no filter: the driver's start-up sets the chip's echo filter to pass the sound through
      unchanged and never changes it (measured on the bench). The chip's filter is not
      reproduced;
    - a new set or a new delay starts from silence.
  - **Test options:**
    - `--screen sound_setN`: the game's loader for set N after the driver's start-up, song 1
      asked for, the volumes full;
    - `--sound-replay FILE`: from the port's first request for song 1, each message is replaced
      by the next one the original sent. The files are local (`notes/sound/seq_tools/replay/`,
      made by `msgs.py`); without them the tests skip.
- **Checked against the bench** (`voices_60000`). A local tool, `bench_events.py`, turns the
  voice report into the port's event words pass by pass. It was checked first on the earlier
  runs: set 1 gave 854 of 855 (the one left is the stray key-on left by the previous set at the
  song's start, section 6.5 of the spec) and set 0 gave 2,289 of 2,289.

  | Race | Set | Steps | Identical events |
  |---|---:|---:|---:|
  | 1 | 3 | 7,700 | 7,440 of 7,440 |
  | 2 | 4 | 10,950 | 10,986 of 10,986 |
  | 3 | 5 | 9,662 | 11,047 of 11,047 |
  | 4 | 6 | 10,581 | 11,262 of 11,262 |
  | 5 | 3 | 5,688 | 5,934 of 5,934 |

  - Every key-on (sample, Hz, left/right, echo), cut, let-go, pitch change (the engine's
    included), volume change (the engine's) and echo setting (through the end fade) is
    identical, step for step.
  - The scaffold's song alone (nothing replayed) gives the same music voices up to the race's
    end fade. Set 3: 3,558 of 3,561 over 7,640 steps (the 3 left are the fade's echo volume).
    Set 4: 4,531 of 4,531, set 5: 5,445 of 5,445, set 6: 4,266 of 4,266, each over 9,000
    steps.
  - Tests: `sound_set{2..7}_song_events` (ROM only, one loop of each song) and
    `sound_race_set{3..6}_replay` (skipped without the local recordings). Sets 2 and 7 are
    fingerprints only, not compared with the original (no movie reaches them on this bench);
    set 7's song is a single rest.
- **Still stops with a message:** noise, the chip's direct gain, an effect's own note, the second
  effect request, 50 Hz timing, "tick every step", and any song command not measured. No set's
  song and none of the races' messages uses any of them.
- **To listen (git-ignored):**
  - `notes/sound/race_set3_native.wav`: set 3's song alone, 151 s, one loop;
  - `notes/sound/race_set3_replay_native.wav`: the movie's first race, with the music, the
    engine and the effects, 129 s. It clipped (1,413 samples) because the engine and the effects
    play at full volume over the music; re-rendered with the headroom below, peak 0.741, none.
- **Open:**
  - the mixer's curves, by ear (the output level: below);
  - sets 2 and 7 against the original;
  - the race's sound in a running race (the port cannot run a race yet).

## The mixer's output level (2026-11-26)

- **Was:** the mix went out at the converted samples' scale (`convert_pcm_gain`, 15-bit to full
  scale) and was clamped, so the races clipped where the engine and the effects play over the
  music.
- **Measured** (the loudest sample of the mix before the clamp, full scale = 1; `--sound-wav`
  now prints it with the number of clamped samples):

  | Render | Peak | Clamped samples |
  |---|---:|---:|
  | menu song alone (`--screen boot,sound_menu --frames 1900`: `menu_set1_native.wav`, 31.6 s) | 1.089 | 12 |
  | menu song with the 8 effects of `sound_menu_events` (same, plus its `--sound-effect` list) | 1.405 | 72 |
  | race 1, set 3 (`race_set3_8094`, 7,740 frames) | 1.482 | 1,412 |
  | race 2, set 4 (`race_set4_17574`, 10,990) | 1.523 | 7,513 |
  | race 3, set 5 (`race_set5_30292`, 9,702) | 1.763 | 7,010 |
  | race 4, set 6 (`race_set6_41951`, 10,621) | 1.345 | 1,109 |
  | race 5, set 3 (`race_set3_54313`, 5,800) | 1.483 | 1,496 |

  (the races: `--screen boot,sound_setN --sound-replay notes/sound/seq_tools/replay/race_setN_FROM.txt`.)
- **Decided:** one fixed output gain, `OUTPUT_HEADROOM` = 0.5 (-6 dB) in `port/sound/mixer.c`,
  on the whole mix after the echo: the loudest measured (1.763) becomes 0.881, about 1.1 dB below
  full scale. No compressor or limiter: a fixed gain keeps every voice's level relative to the
  others as before, and nothing measured needs more. The clamp stays as a guard only.
- **After:** peaks 0.545 (menu song alone), 0.702 (with effects), 0.741, 0.761, 0.881, 0.672,
  0.742 (races 1-5); 0 samples clamped in all seven. Re-rendered (git-ignored):
  `notes/sound/menu_set1_native.wav` (the menu song alone, as before) and
  `notes/sound/race_set3_replay_native.wav` (race 1).
- **Tests:** no test hashed audio samples (the sound tests fingerprint the player's events,
  which the gain does not touch: all unchanged). `sound_menu_wav` now also requires `peak 0.x,
  clipped 0`.
- **Open:** the level by ear against the original (the owner); a set or race louder than these
  seven would clamp again (the WAV line shows it).

## The pre-race screen drawn natively (step 4, 2026-11-26)

- **Identical to the original pixel for pixel** (`explore_menu.txt`, offset 150; `RACE` chosen
  in the championship): the bar (`$E175`), port frames 6563 and 6568 against `frame_06716.png`
  and `frame_06721.png`; the closed shutter (`$D784`/`$8934`), port 6571/6573 against
  `frame_06734.png`/`frame_06735.png`; the course view opening (`$E28C`), port 6585 and 6586
  against `frame_06751.png` and `frame_06752.png`. 0 of 57,344 pixels different each. The
  console's load takes 13 frames longer than the port's, so for the open view the input is
  re-aligned (`--movie-offset-from 6571 163`); then port frame N matches bench `frame_0(N+166)`:
  N = 6583, 6590, 6595-6599, 0 pixels each (meter, planets' shadows, discs, scroll, objects);
  the glow frames 6622 and 6625 against `glow/frame_06788.png`/`06791.png`, 0 pixels (leader
  lines and their glow on screen, brightness 5 and 3); the other fade frames (6605-6628 against
  `frame_06771` and `glow/frame_06775..06794`) differ only by 1 level in some channels.
- **Where the picture is taken: before the NMI (listed for the veto).** On the console this
  screen's frame code, right after its wait, still lands in the picture the NMI has just
  started: it streams the planets' shadow frames, writes the meter's 10 words, and rewrites the
  window tables `$7F:0000`/`$7F:0100` before the beam reaches them. The port draws every other
  screen right after the NMI, before that code runs. So this screen alone is drawn at the start
  of the next frame wait, before the next NMI (`port/platform/frame.c`): then the frame code's
  output is in, and what the NMI itself sent is taken as it sent it (the sprite table, captured
  at its upload; the glow word `$017C`, read when the NMI target `$E28C` is dispatched). The
  phase is the NMI target the port last dispatched. The screen's pictures come one frame later
  than before (the bar's first frame is drawn right after its NMI and shown twice). No game
  counter is stepped and no game routine is retyped in the drawing.
- **How, under the rule (`port/screens/race_start_screen.c`, `port/video/assets.c`):**
  - One drawing function for the screen, three phases. No VRAM-shaped buffer: the shutter map
    (`$9C:8FC3`) and the course pictures (`$92:80B2`: BG tiles, object tiles, BG3 tiles; the
    sun's 16 frames too, not used yet) are unpacked and converted at start-up and recognised at
    upload by WRAM source and content hash; the six course maps (`$7F:AE50`, `$CB10`, `$B550`,
    `$2000`, `$BD10`, `$C410`) are captured when the game sends them, by WRAM source and size;
    the blank BG3 tiles (`$7F:D210`) are checked blank. Colours from `$0500`. The upload
    destinations are ignored.
  - **Handed over, not computed:** the sprite table as the NMI sends it (`video_upload_oam`,
    `$81:0900`, `$220` bytes); each planet's shadow frame as the frame code streams it (128
    bytes from `$7E:8000 + n*$800 + frame*$80`: the source names the planet and the frame); the
    meter's 10 words as `draw_meter` writes them (`video_write_vram_word`: once the course maps
    are in, `draw_meter` is the only caller; a run of 10 words is taken only if it reads full
    tiles, at most one part tile, then empty tiles). The meter capture is by call pattern (like
    the VS panels before request J); a named note is to be asked of the decomp.
  - The streamed uploads come as three calls (`dma_stream_setup`/`_size`/`_kick`); the port
    joins them into one (WRAM source, size) at the kick, ignoring mode, target register and
    destination (`port/platform/video_events.c`; listed for the veto).
  - Everything not measured stops with a message (band lists, window table layout, map entries,
    object attributes, more than 32 objects or 34 tile slices on a line, a see-through shutter
    pixel while it opens, an open viewport line before the viewport is on, an animated sun:
    `$0700` not 0, never compared with the original).
- **With the camera moving** (`explore_pw.txt`, offset 150; the same course): port frame N
  matches bench `pw/frame_0(N+166).png` for N = 6508, 6511, 6514 ... 6565 (every third frame),
  0 pixels each: the viewport scroll ($0120 from $2A to $B1), the leader lines sliding and the
  glow at full brightness.
- **Per-line effects (for the owner's veto):**
  - **The merging shutter (bar phase):** each line shows a row of the championship's BG2 map;
    the row offsets are read from the band list `nmi_target_80E175` writes at `$1B00` for the bar
    height h (h from its first band, checked against `$0114`/`$0116`; every other byte checked
    against the routine's layout for that h).
  - **The closed shutter:** the fixed list `$81:C207` at `$1B00` (checked): lines 0-113 from the
    shutter map's second half, 114-223 from its first.
  - **The shutter opening:** `$017A` (v) with the four lists `nmi_target_80E28C` writes in step
    (`$1B00` row offsets, `$0D00` mode, `$0D28` masked layers, `$0D14` BG2 scroll reset), all
    checked against the routine's layout for v: lines before v and from 228-v show the shutter,
    the lines between the course view.
  - **The leader lines and the glow (lines up to 126):** window table `$7F:0000` (window 1) masks
    every layer to colour 0 (`$1BE6`); inside window table `$7F:0100` (window 2) the fixed colour
    the NMI sent (`$017C`/`$017D`) is added to BG1-3 and objects of palettes 4-7. Tables read in
    the format `window_shape`/`func_80DC5A` write (header, optional vertical line, horizontal
    line, empty run, 52-line disc block, end), checked.
  - **The discs' rims (lines 127-179):** the disc blocks of the same two tables; where exactly
    one of the two discs covers a point, every layer is masked to colour 0 (`$7CC6`).
  - **The backdrop split:** colour 0 is `$1BE6` up to line 126, `$7CC6` from line 127.
  - **The viewport scroll:** BG1-3 move by `$0120` on lines 127-179 (0 elsewhere).
  - **The starlight (lines 127-179):** where the front pixel is BG1 or BG3 (the black and the
    planets' shadows), the front object pixel at the same point is added.
  - **The planets' shadows:** BG3 tiles 2.. (16 per planet), their second plane as streamed.
  - **The meter:** row 6, columns 2-11 of BG2, the 10 words `draw_meter` wrote.
- **Tests:** `game_picture_racestart_bar` (6563), `game_picture_racestart_opening` (6585),
  `game_picture_racestart_open` (6598, re-aligned input), `game_picture_racestart_camera`
  (`explore_pw.txt`, 6544).
- **Open:**
  - Closed 2026-11-27 ("Closing the unmeasured stops", below): the animated planet (`$0700`
    not 0, called "the sun" here before it was seen: it is scenery kind 2, a 6x6 turning planet)
    is drawn from its streamed frame and compared in all four systems that have one.
  - The meter's capture is by call pattern until the decomp names it.
  - Fixed (2026-11-26, the brightness rule measured, below): the fade frames differed by 1 level
    in some channels (e.g. port 6605 against `frame_06771.png`: 5,424 pixels); now 0.
  - Objects of palettes 0-3 are drawn without the glow (as the screen's colour set-up says);
    the glow frames match, but this case was not isolated.

## The shop drawn natively (step 4, 2026-11-26)

- **Identical to the original pixel for pixel** (`explore_shop.txt`, offset 150; port frame N =
  bench `frame_(N+169).png`): the list of upgrade kinds, port 6332-6334 against
  `frame_06501..06503.png`; the fade-in at brightness 3 and 5, port 5748 and 5750 against
  `frame_05917.png` and `frame_05919.png`. The console loads the item screens more slowly, so for
  them the input is re-aligned (`--movie-offset-from 5800 166`): the Engine list's item screen
  (picture, price, level marks, typed text), port 8331/8333/8335/8341 against
  `frame_08500/08502/08504/08510.png`; an item sliding out while its text is cleared, port
  8270-8278 (even frames) against `frame_08439..08447.png`. 0 of 57,344 pixels different each,
  both halves (the bottom half is dark in one-player games). Measurements:
  `notes/probe/SHOP.md`.
- **How, under the rule (`port/screens/shop_screen.c`, `port/video/assets.c`):**
  - One drawing function, the top half only: task 2 takes the early leave in one-player games
    and its half stays dark (`$029B` = 0 for the whole run); a lit bottom half stops the port.
  - The top half's shadow block is one fixed, measured block while lit; it is checked byte for
    byte and the screen draws what it sets as constants: Mode 1, BG1 scrolled (4, 5), BG2
    (0, -1), no colour maths, and two fixed windows for the whole half: BG1 hidden at x 0..15 and
    77..85, objects at x 0..15. No per-line table: no HDMA channel is on in the shop (bench
    report over the whole movie).
  - Cartridge blocks, unpacked and converted once at start-up: `$8D:8000` (BG 1..304),
    `$88:8000` (BG 305..439), `$87:8190` (objects 0..79), `$8D:895F` (objects 80..163), the BG2
    map `$87:8000`, and the six lists' picture blocks (`$8F:8000`, `$99:8000`, `$9D:8000`,
    `$9C:8040`, `$9E:8000`, `$9D:8F55`). The shop is recognised by its four streams in order
    (`video_note_source`) and `$87:8000` noted per half; any other stream ends it. The VRAM
    destinations are ignored.
  - Captured when the game sends them, by WRAM source and size: the BG1 panel map
    (`$7F:0000`, 960 bytes), the static's 16 tiles (`$7F:0700`, 512 bytes, BG 442..457), the
    sprite table (`$81:0900`/`$0D00`, `$220` bytes) and the 256 colours (`$81:0500`, 512 bytes).
    The colours and the sprite table are taken at their upload, not read from RAM at draw time:
    the panel's frame hook rewrites the icon's colours in `$0500` right after they are sent.
  - Item pictures: the game unpacks the list's block to `$7F:0900` with no platform call, then
    sends slices of it, sometimes split by the frame's upload budget. An upload from
    `$7F:0900+offset` is recognised by its bytes against the block at that offset, inside one of
    36 measured pictures (block, offset, size, first object drawing; read from the bench's
    upload queue in `explore_shop`, `explore_buy`, `explore_menu`, `explore_pw`, the first
    drawing derived from the measured upload destination); its drawings come from the converted
    block, never from the sent bytes. A later picture replaces the drawings it covers (the parts
    in place are kept by object-drawing range, trimmed or split). Anything else from there stops
    the port (lists 6-10, `$97:8000`, were never reached), and so does any other upload from
    `$7F` while the shop runs (a map or the static's tiles split by the upload budget would).
  - Objects 8x8 and 16x16 from the captured table, the lowest number in front, layers back to
    front: backdrop, objects 0, objects 1, BG2 low, BG1 low, objects 2, BG2 high, BG1 high,
    objects 3. Map entries, object attributes, more than 32 objects or 34 slices on a line:
    checked against what was measured, anything else stops.
  - Drawn at the existing two-task draw point (before the NMI): every piece is as the last NMI
    sent it; the brightness `$019B` changes only inside the NMI. No game RAM written, no game
    routine called, no game formula retyped.
- **Per-line effects:** none.
- **For the owner's veto:** (1) the item pictures' parts kept by object-drawing range, trimmed
  and split as new parts arrive: the closest this screen comes to a video-memory-shaped
  structure (it holds only named pictures of named blocks); (2) the first drawing of each
  picture, derived from the upload destinations measured on the bench (the port ignores them at
  run time); (3) the colours and the sprite table taken at their upload, where the championship
  reads them from RAM at draw time.
- **Coverage:** the port's own runs (explore_shop with both alignments, explore_menu,
  explore_pw, race1, autopilot_60k) draw the Paintshop picture and six of the Engine list's
  seven; Engine `+$2E40` and the Tires, Gearbox, Armor and Boost rows come from the bench's
  upload queue only and were never drawn or compared by the port.
- **Tests:** `game_picture_shop_list` (6333), `game_picture_shop_fade` (5750),
  `game_picture_shop_item` (8333, re-aligned), `game_picture_shop_slide` (8274, re-aligned).
- **Open:**
  - The panel's static differs from the original (port 8346 against `frame_08515.png`: 2,937
    pixels, all inside the static's square): the random state differs after the console's extra
    load frames; the drawing of the static's tiles and map is the same code path.
  - Fixed (2026-11-26, the brightness rule measured, below): the fade frames differed by 1 level
    in some channels; port 5747..5758 now 0 pixels.
  - **The first lit frame (brightness 1, port 5746 against `frame_05915.png`, 21,602 pixels):
    analysed 2026-11-26, not a draw-point or hand-off matter; kept black.** The traced words
    (frame counter, NMI target, both halves' brightness, fade, slots, hooks, queues) are the
    same on both sides, step for step (port frame N = bench step N+166): the NMI that starts the
    fade (`$019A` = `$0100`, port 5745, bench 5911) comes while task 1's queue still holds the
    panel's map (head `$018C` = 2, tail `$018E` = 0); the queue is sent at the next NMI (5746 /
    5912: head = tail = 4), from which on the port matches (5747 = `frame_05916.png`, 0 pixels).
    So the original's brightness-1 picture shows what the video memory still held: the shop's
    BG2 is right, but the BG1 panel shows a map left there from before (which screen's was not
    checked) read with the shop's tiles (stray letters in the list and price areas), and no
    objects (the list, the credits and the icon are absent: the sprite table at `$016E` holds no
    visible object at port 5745, 47 at 5746). Drawn from the later map, 2,718 pixels still
    differ, all in those areas. The
    port cannot draw the left-over video memory without a model of it (no VRAM-shaped buffer),
    so it keeps the frame black (the screen is recognised only once its pieces are in).
  - Lists 6-10, player 2's half and the two-player shop were never measured: they stop the port.
    Lists 6-8 closed 2026-11-27 ("Closing the unmeasured stops", below); list 9 waits on the
    decomp (request R), list 10 never opens; the two-player shop needs a second controller.

## The ticker drawn natively (step 4, 2026-11-26)

- **Identical to the original pixel for pixel on every frame** (`--screen ticker` from
  `notes/tick/wram_before_tick.bin`, `autopilot_60k.txt` step 15857; port frame F = bench
  `frame_(F+15868).png`): all 625 frames, 0 to 624, 0 of 57,344 pixels different each: the dark
  start, the fade-in (brightness 1..14), the lit run with the road stripes passing and the
  scrolling texts (the player's result, then the `$81` "SECRET BONUS" lines and the end marker),
  and the fade-out (14..1). Measurements: `notes/probe/TICKER.md`, section F (the earlier
  probe's object drawings and colour 0 were the bottom half's; corrected there).
- **How, under the rule (`port/screens/ticker_screen.c`, `port/video/assets.c`):**
  - One drawing function, the top half only: task 2 takes the one-player early leave and its
    half stays dark (`$029B` = 0 the whole run); a lit bottom half stops the port.
  - The top half's shadow block `$019C..$01C1` is one fixed block for the whole run, checked
    byte for byte: Mode 7, BG1 and objects on the main screen, objects 8x8/16x16 with their
    drawings at the font, no windows, no colour maths.
  - Cartridge blocks, unpacked and converted once at start-up: the Mode 7 drawings `$88:8C7D`
    (101 8bpp tiles), the Mode 7 map `$95:8000` (17x7 tiles at the map's top-left, tile 0
    elsewhere), the object drawings `$88:8601` (the font, 186 tiles, already a known block).
    The ticker is recognised by its load in order: `$88:8601` noted and sent, then three
    streamed uploads joined at their start (the tiles by size and content, the map cleared from
    `$81:CBF8`, its byte checked 0, then the map's 7 rows of 17 bytes, each compared with the
    block's row). Any other unpacked stream ends it. The VRAM destinations are ignored.
  - Captured when the NMI sends them: the top half's sprite table (`$81:0900`/`$0D00`, the last
    one sent) and its 256 colours (`$81:0500`).
  - Objects from the captured table, the lowest number in front; checked: drawings 0..185,
    priority 0, 1 or 3, at most 32 on a line. Priorities 1 and 3 in front of the picture.
- **Mode 7 use (listed for the owner's veto):** lines 0..107 show the 128x128 Mode 7 map
  through the game's own view block `$012E..$013D` (ticker_scene_step: two offsets, the matrix,
  the centre), read at that fixed address in the format the routine writes. In every measured
  frame (the pass after a race, `$03A8` = 5) the matrix is the identity and the centre (64, 28):
  the code checks those six words and draws the map moved by the two offsets (13-bit, wrapping
  at 1024); a turning or zooming view stops. Screen line y is the picture's line y + 1.
- **Line splits (listed for the veto):** (1) the split at line 108 (the menus' line interrupt):
  lines 108..223 black, the bottom half being dark; (2) the screen's four line tables
  (`$81:CBF9..$81:CC0E`, cartridge) hand the top's view block to lines 0..115 and the bottom's
  (`$022E..`) from line 116, which lies in the dark half: nothing of the second is drawn.
- **Where the view is taken: when the NMI runs (listed for the veto).** Task 1 writes the next
  view block right after the NMI, while the picture is being drawn, and the line tables take it
  at the picture's first line: the bench's picture after the NMI of step N shows step N-1's
  block with the sprite table that NMI sent. The port takes the block in the two-task branch of
  `platform_wait_vblank` just before `nmi_prologue` (`ticker_screen_note_nmi`), and draws at the
  existing two-task draw point. Read at the draw point instead, the Mode 7 picture was one
  step (3 pixels) off: port 142 against `frame_16010.png`, 144 pixels at x 253..255 (the box
  entering on the right); the objects, the road stripes among them, matched. No game counter is
  stepped, no routine retyped.
- **Per-line effects:** none besides the split above (the line tables carry one block a half).
- **Tests:** `game_picture_ticker_fade` (port 23, brightness 11, `frame_15891.png`),
  `game_picture_ticker` (port 142, `frame_16010.png`), `game_picture_ticker_bonus` (port 382, the
  `$81` text, `frame_16250.png`), all from the saved memory (skipped where absent).
- **Open:**
  - The zoom-in (`$03A8` = 0: the turning matrix and the sparks) was never reached in a bench
    movie: it stops. So do a priority-0 object over an opaque picture pixel (never happened;
    the title has them behind), a lit bottom half and priority 2.
  - In full runs the ticker follows a race, which the port cannot run yet (DSP-4).
  - The port's fade runs half a step out of phase with the bench's (`$019A` low byte `$00`
    against `$80`; the cause is not located, the port's load also takes 7 frames fewer); each
    drawn picture's brightness still matches.

## The brightness rule measured (2026-11-26)

- **The rule the bench's pictures follow at brightness 1..14:** each 8-bit channel c (the
  5-bit colour widened as `c5 << 3 | c5 >> 2`) becomes `(c * ((b << 16) / 15)) >> 16`, integer
  division first. Brightness 0 is black, 15 unchanged. The shared `screen_apply_brightness`
  (`port/screens/screens.h`) used `(c * b * 17) >> 8`, one level low in some channels at
  brightness 2, 4 and 6..14 (identical at 1, 3 and 5). `champ_screen.c` had its own copy for
  each half; it now calls the shared one (`screen_apply_brightness_lines`).
- **Checked:** every fade frame of the ticker's fade-in and fade-out (0 pixels; before, up to
  26,212 pixels a frame, all off by exactly one); the pre-race fade, port 6605, 6609,
  6612, 6614, 6616, 6619, 6622, 6625, 6628 against `frame_06771.png` and
  `glow/frame_06775..06794.png` (brightness 13 down to 2): 0 pixels each (before: e.g. 5,424 at
  6605); the shop's fade-in, port 5747..5758 against `frame_05916..05927.png` (brightness
  2..13): 0 pixels each. The ticker's colours rule out `c * b / 15` at every level. Port 5746
  (shop, brightness 1) still differs: the panel's map is not sent yet there; the original shows
  left-over video memory (analysed in the shop's section).
- **Tests:** all 35 earlier tests pass with their hashes unchanged, so nothing was re-pinned.
  The new `game_picture_ticker_fade` pins a fade frame at brightness 11.

## The race: measured (2026-11-26)

Measure only: nothing of the race is drawn. Full report in `notes/probe/RACE.md` (local); bench
with the firmware DSP-4 (`--dsp4-firmware`; bsnes v087 fetched by the decomp's
`tools/refbench/fetch.sh`), `autopilot_60k.txt` and `race1.txt`.

- **Pacing (for the owner; not decided here).** The race loop waits until the NMI has taken its
  last picture (`$00B8` bit 0), not a fixed number of vblanks. On the bench every loop step took
  2, 3 or 4 vblanks, never 1: `autopilot_60k.txt`'s first race, 3045 steps over 8151 NMIs (995
  of 2, 2042 of 3, 7 of 4; about 22 pictures a second); `race1.txt`, 1027 steps (692 of 2, 334 of
  3). The body alone takes 368-691 scan lines. The bench's firmware DSP-4 answers at once, so a
  console lags at least this much. Motion is scaled by the vblanks the step took (`$1E9E`); a
  few things are per step (the nitro shake's random draws, the automatic gear index). The HUD
  (`race_hud_draw`) is drawn only by NMIs that do not take a picture (3033 of them; 0 of 3045
  taking ones), so a port running one step per vblank would never redraw it. Whether the port
  runs one step per vblank or keeps 2-3, and how the HUD then gets drawn, is the owner's
  game-speed question (decided: "Race pacing", below).
- **Entry: `--screen race`** runs `race_wrapper()` from the original's WRAM at its first
  instruction (`$82:8342`, step 7903 line 10), `notes/race/wram_before_race.bin`. No step end is
  clean there (the end of step 7902 is before `top_race_prep`: `$0005`, `$0008`, `$0024`,
  `$03E0` still change), so `tools/make_reference_memory.sh` patches the bench for one run (a WRAM
  write at that PC) and restores it.
- **The race NMI in the port:** target `$85:8377` → `nmi_race_prologue`, then `$0008` = `$838A`
  (`nmi_race_tail`, during the load) or `$83CE` (`nmi_race_full`), as measured; anything else
  stops. The band IRQ `$85:87BA` is not run (it only sends the chip the band streams; its one
  WRAM word `$00BC` is the band cursor).
- **The load gets through:** `race_load`, `race_sound`, the track, the road's first frame; the
  port stops at port frame 89 at the first DSP-4 sprite command (`object_sprites_stream`, `$09`)
  in the first `race_frame_draw` (test `game_race_load_runs`).
- **`$8D:8B70` is not track-dependent** (the premise was wrong): `race_load` always sends those
  32 colours. Added to the palette catalogue as "race colours 0-31".
- **Track-dependent blocks: the game's own tables, enumerated at start-up.** The race's track
  pictures are unpacked by the game from tables indexed by the track (`$83:8410` → `$83:8447` →
  `$83:8585`/`$8597`: 9 packed tile sets; `$84:FD33`, `$84:FDA1`/`$FE0F`, `$84:FE7D`; the HUD's
  laps-total digit `$83:A484` by `$1F06` (first called a "sky row"; corrected by the race
  measurement, `notes/probe/RACE.md`); the objects' table `$86:AE36` → `$86:0000`). Chosen: the drawing run converts every
  entry these tables name at start-up (as the other screens' named blocks) and recognises each
  upload against that catalogue by WRAM source, size and content; the port never reads `$047B`
  to pick a block (that would retype the game's choice). Not cataloguing only the tracks the movies
  reach. Checked: from each race's wrapper memory, the port's WRAM after its own load (the C
  reading the same tables) equals the bench's at `race_track_start` for tracks `$00`, `$22` and
  `$0C`, except the pads and counters (`$000C-$0024`, `$00BA`), the stack, and `$045F` on track
  0 (a sound flag; port `$01`, bench `$FF`: a timing difference, not a port bug, see "Race
  pacing", check O).
- **Draw point: after the NMI** (the usual one, not the pre-race screen's). At three anchors no
  byte of a live table (windows, both scroll tables and their data, the band streams, band list,
  mode bytes, the live sprite table) changes between the picture's first band IRQ and the next
  NMI; the bench's HDMA report lists mid-picture writes into enabled tables only at
  `$0200-$0203` (134 of about 6,900 frames) and `$11B2-$11B5` (8), besides the first frame.
- **Not done here:** the sky/road/HUD band rows against pictures, the catalogue code, any drawing.

## Race pacing (2026-11-27)

**The owner (verbatim):** "é um port, então não se beneficiar de hardware infinitamente mais
potente é maluquice também, porém para quem quer uma opção mais próxima do original também
deveria ser possível". Under the rule "nem 1% emulador" (no CPU/PPU/sound/DSP-4 model; never
change or predict the game's logic in the port).

**Decided (with the advisor):** a port setting, "screen refreshes per race step",
`--race-refreshes N`: **2 by default** (the game as written with the console's slowdown
removed: the console's own light-scene rate, about 30 pictures a second), **3** allowed (the
console's heavy-scene rate, closer to the original), anything else refused at start-up. 1 is
not offered: the HUD would never be redrawn (only NMIs that do not take a picture draw it) and
the per-step logic (gear index, the nitro shake's random draws shared with the AI's route
choice) would run about 3x faster; it would need game-logic changes. A fixed count is a pacing
setting, not a CPU model: the console's count comes from its CPU and DSP-4 time, which the port
does not have and does not estimate. True 60 pictures a second (drawing interpolated between
steps) is parked for later. No settings file exists yet; the setting is a command-line option
(README, Settings).

- **Without pacing the port runs 1 refresh a step** (checked by reading the decomp at `0aa501a`):
  the loop `race_mode_one` is head, `race_frame_wait` (`while($00B8 & 1) platform_wait_vblank()`),
  body, `race_frame_rest`; the head and body never wait (every `platform_wait_vblank`,
  `wait_frames`, `wait_one_frame`, `wait_03e0` and `wait_flag_76` call in `src/` listed with its
  function: the one-player race's are `race_wrapper_head`/`_tail`, `race_sound` and its set
  loader, `race_track_start_head`/`_tail`, `race_frame_wait`, `race_pause` and `race_end`; the
  generic ones, `fade_out_wait`, `queue_unpacked_upload`, `wipe_columns`, have no caller in the
  race), so the port's wait sees exactly one
  race NMI (`$85:8377` → `$83CE`, `nmi_race_full`), which takes the frame (clears bit 0) and
  ends the wait. `nmi_race_tail` (every race NMI) sends the sound message (`sound_frame`), reads
  the pads (`pads_read_long_2`) and counts `$0024` and `$00BA`; `race_frame_rest` keeps `$00BA`
  in `$1E9E` (the step's refresh count, which scales the motion) and clears it.
- **Where the refreshes go:** `port/platform/frame.c`, `race_wait_refreshes()`, keyed on the
  race's NMI: vector `$85:8377`, `$0008` = `$83CE`, and the frame requested (`$00B8` bit 0) at
  the wait's entry. That wait gets N refreshes, each a whole `refresh()` as at any other wait:
  input pumped, the full NMI dispatch (the first takes the frame; the others are the HUD's NMIs,
  with their pads, sound message and `race_hud_draw` as the game has them), the picture
  presented, a frame of sound, pacing to 60.0988 Hz, the port's frame counter +1 (so `--movie`
  stays one line per refresh). Every other wait gets 1.
- **Waits excluded (frame waits):** `wait_frames` waits for `$0024` to reach an exact value, so
  several refreshes in one call would overshoot it and hang. With the frame requested these are
  the pause's (`$0026` bit 4, set by `race_pause`) and `race_end`'s (the loop over:
  `race_frame_rest`'s own end test, `$0146` = 0 and no brightness in `$014A & $0F00`, still
  holds, since no NMI runs between the rest and the next wait in the port). Before step 1 the
  only wait is `race_track_start_tail`'s frame wait (frame not requested), so **step 1 takes 1
  refresh** in the port (the console's step 1 counts from that frame wait: 3 in the recording).
- **Open point: where the HUD's NMIs fall.** On the console the NMIs that do not take a frame
  fire during the step's body (the body takes 368-691 scan lines); the port can only place them
  at its wait, right after the taking one, before the body. Same count and order relative to the
  taking NMI, different place relative to the body's writes (e.g. `race_hud_digits` sets
  `$00B8` bit 2 in the body: on the console the HUD NMI of the same step may already draw those
  digits; in the port they are drawn at the next step's wait). Measured on the bench (W lines of
  `notes/race/probe_autopilot_16300.log`): `$00B8` at the wait's entry is `0000` 1181 times,
  `0001` 1852, `0004` 12, never `0005`; in the port it will be `0005` at every step (the digits
  always pending when the HUD's NMI runs).
- **Open point: the step after a pause.** The pause's first frame wait takes the frame (its NMI
  clears bit 0), then `race_frame_rest` clears `$00BA`; the next step finds the frame not
  requested and does not wait, so its rest keeps `$1E9E` = 0 in the port, a value the console
  never produces (there NMIs fall during that step's head and body), and some motion code loops
  on it (`misc.c` near `$ED8A`, "one step per frame counted in `$1E9E`"). Not reachable yet; to
  be settled when the race runs.
- **Test only: `--race-refresh-replay FILE`** plays the refresh count the original took at each
  step, recorded on the decomp's bench with the DSP-4 firmware (`notes/race/race_refreshes.txt`,
  `DSP4_FIRMWARE=... tools/make_reference_memory.sh`: `$1E9E` at every loop head after the first
  and at `race_end`; 3045 steps of `autopilot_60k.txt`'s first race: 999 of 2, 2039 of 3, 7 of 4;
  counted between rests, so slightly different from the head-to-head 995/2042/7 above). Like
  `--movie`, it is a recording of the original, not a processor model; it never ships as a
  setting. Line 1 (step 1) is skipped (no wait to place it); the port's first race wait takes
  line 2; after the last line the setting applies.
- **Tested** (`ctest`; local files under `notes/race/`, skipped where absent):
  `port_race_refreshes_refuses_{1,4,2x}` (no ROM needed); `game_race_load_runs`,
  `game_race_load_runs_paced_3`, `game_race_load_runs_refresh_replay` (the load and the first
  frame reach the same stop, `object_sprites_stream` at port frame 89: step 1 is not paced);
  `game_race_wait_{default,3,replay}`: `--screen race_wait` runs the game's `race_frame_wait()`
  once from the original's WRAM at that wait with the frame requested
  (`notes/race/wram_race_wait.bin`: the first entry from loop step 300 on), giving 2 / 3 / 3
  (step 2's recorded count) refreshes, `$00BA` the same, `$00B8` 1 → 0, no stop. The replay was
  thus exercised at one wait only (its line 2, on a step-300 memory). By hand: the same dump
  with `$00B8` bit 2 set (the port's usual case, which the bench never shows at a wait), the
  second (non-taking) NMI ran `race_hud_draw` without a stop (bit 2 cleared).
- **Untested** (the race cannot run past port frame 89 until `$09` is native, request A): any
  step's body and rest after a paced wait (`$1E9E` = N reaching the motion code), the HUD drawn
  in a running race, the pause and `race_end` exclusions in a run, the replay past its first
  line in a run, a whole race at 2 or 3 against the original's timing, input feel.
- **Check O (`$045F` after the load), answered:** not a port bug. On the firmware bench
  (`autopilot_60k.txt`) `$045F` = `$01` at `race_track_build` (`$82:856D`, step 8093 line 220)
  and `$FF` at `race_track_start` (`$82:85D6`, 8110 line 253), with 18 race NMIs (`$838A`,
  `nmi_race_tail` → `sound_frame` → `sound_send_message`, which writes `$FF` after every message)
  in between: the console's load frames (the only writer of `$FF` in `src/` is
  `sound_send_message`; the others write `$00`/`$01`: `boot.c`, `race_sound`, the set loader,
  `sound_load_menu`, the VS sound). The port has no wait in `race_track_build`; its `$01`
  goes out with the first race NMI after it (`race_track_start_head`'s frame wait, port frame
  87 → `$FF` at 88).
- **Check N (the wrapper's mode 1), confirmed:** WRAM at the mode call (`$82:83B8`, every entry)
  on the firmware bench: `$03DC` = `$2001` (mode 1, `$82:89B5`) at steps 44736 (track `$14`)
  and 57099 (track `$13`), both with `$047C` = `$FF00`; the other entries mode 0 (7903, 23259,
  33079) or 2 (20362, 39876). Reported to the decomp (docs/decomp-requests.md, N).
- **Since then:** the race runs in tests with a recording of its DSP-4 sprite calls ("The DSP-4
  tape (tests only)", below): 911 refreshes from the loop's first head, every `$09`/`$06` call as
  recorded, `$1E9E` reaching the motion code at the recorded counts.

## The DSP-4 tape (tests only) (2026-11-27)

**REMOVED 2026-11-28 (decomp `e152ffc`: the one-player race's `$09`/`$06` native).** The file
`port/platform/test_dsp4_tape.c`, the flags `--dsp4-tape`/`--dsp4-tape-nmis`, `--screen
race_loop`, its tests and `record_race_tape` are gone; the race runs from `race_wrapper` to its
end with the native commands (tests `game_race_runs*`). Kept below as the record of what was
measured with it.

**Listed for the owner's veto: two test-only recordings, nothing in the shipped path.** Under the
rule "nem 1% emulador" no DSP-4 model goes into the port, not even in a dev build (the
byte-protocol model was refused: "The DSP-4: no chip-protocol model in the port"). A tape is a
recording, like `--movie` and `--race-refresh-replay`: values taken from one bench run, replayed
in order, nothing computed. It lets the one-player race run in tests while the decomp finishes the
native sprite commands (request A); it is deleted when the race's C calls them (the decomp has
`dsp4_list_*`, `dsp4_objects_*`, `dsp4_high_table` in its gate).

1. **`--dsp4-tape FILE`** (`port/platform/test_dsp4_tape.c`, the one place): `dsp4_port_read16`
   answers the next recorded read; `dsp4_port_write16` must equal the next recorded write, and
   any difference stops the port (`port_fatal`: the call's index, its bench step, expected and
   got, the port frame). Without the flag both calls stop the game with the old message.
2. **`--dsp4-tape-nmis`** (a second recording, vetoable on its own): the tape also says where the
   bench ran each race NMI between two of those calls (from loop step 1's head on); each is run
   there as one whole port refresh (input, the NMI dispatch, the picture, sound, pacing, frame
   counter), and each race-loop wait takes the next one instead of the pacing setting's count.
   `--race-refresh-replay` is then unused. Without it the tape's NMI lines are ignored.
3. **`--screen race_loop`** (frame.c, test scaffold): `race_mode_one`'s loop (head, wait, body,
   rest) and `race_end` retyped from the decomp's C, entered from the original's memory at the
   loop's first head. It does not follow the decomp by itself: if `race_mode_one` changes, this
   copy must be changed by hand (it goes with the tape).

- **Source (decomp `ca47c97`, bench clone `~/tg3000-decomp-bench` checked out there and
  rebuilt):** `tools/make_reference_memory.sh`, `record_race_tape`, one run of
  `autopilot_60k.txt` (its first race, track `$00`: `race_wrapper` at bench step 7903 to
  `race_end` at 15760). The bench's **default DSP-4**, not `--dsp4-firmware`: the port answers
  `$01`/`$07`/`$08` with `src/dsp4.c` as that bench does, and the firmware run takes another path
  from the race's first frames (a first-frame rounding `src/dsp4.c` does not model). So the
  `$09`/`$06` answers on the tape are the bench's clean-room stand-in's, **not the console's**.
  The bench's own formats are reused: `--dsp4-tape-out` (every data-port byte: PC label, value,
  read/write) and `--dsp4-report` (each PC label's command); a one-run patch of `bench.c`
  (restored with `git checkout`, as the script's other patches) adds the marks only. Kept: the
  bytes at the PC labels the report gives `$09` and `$06` (no such label is shared with a
  native command: checked), paired into words (both bytes of a word carry the same label).
  Checked: 2848 `$09` streams, each opening `0009 0080 00A9 0000 0000 0100 0004`, and 2848 `$06`
  loads, each one write and 16 reads: 4,727,876 calls, 2848 loop steps.
- **Files (local, `notes/race/`, never in git; `race_tape*.txt` also in `.gitignore`):**
  `race_tape.txt` (`W hhhh` / `R hhhh` a call each; `N S` a race NMI the bench ran before the
  next call, in bench step S; `P S` one after its step's last call and before its rest, run as
  that call returns; 7561 N, 86 P), `race_refreshes_tape.txt` (`$1E9E` per loop step, this run:
  904 of 2, 1937 of 3, 7 of 4; the firmware run's `race_refreshes.txt` stays for its tests),
  `race_tape_marks.txt` (every NMI, loop head, first wait entry, rest, with bench step, `$00B8`
  and the raw tape position), `wram_race_loop.bin` (WRAM at the loop's first head, `$82:8645`,
  bench step 8112), `tape_anchors/` (below).
- **Test scaffold `--screen race_loop`** (frame.c): `race_mode_one`'s loop and `race_end` as the
  game has them, from `wram_race_loop.bin`. Needed because of finding 1.

**Findings (measured, in order):**

1. **From `race_wrapper` (`--screen race`, the tape alone): stop at port frame 89, call 439**
   (bench step 8113), in step 1's first draw: `object_sprite_shadowed`'s bob height, read from
   the frame counter `$0024` through `$84:A200`: `FFC5` against `FFC7` (test
   `game_race_tape_wrapper_stop`; the same with the firmware run's `race_refreshes.txt`, whose
   lines are not used before frame 89). The counter differs by the load's length: from
   `race_wrapper` to the NMI before step 1's first draw the console ran 210 NMIs (one menu NMI and
   209 race NMIs, bench steps 7903-8112), the port 89 (its waits only; the rest is the console's
   CPU time, which the port has not and does not estimate). Measured: `$0024` = `$0058` in the
   port at frame 89 and `$00D1` in the original's memory at the loop's first head (121 apart),
   both just before the NMI of bench step 8113. With the tape's NMIs placed
   (`--dsp4-tape-nmis`) the same stop comes at frame 90: placement does not rescue this entry. Not pacing, not step 1's
   count (step 1 has no race wait in the port or on the bench: `$00B8` = 0 at its wait in both);
   not fixable in `frame.c` without a CPU model. Harmless for a player (an animation phase); for
   the tests the race is entered at the loop's first head instead.
2. **From the loop's head with the tape alone: stop at frame 0, call 439** (`FFC8` against
   `FFC7`): the bench's NMI of step 8113 fell in step 1's body before its stream, and the port
   runs NMIs only at its wait. Measured over the run: 2084 of 2848 steps have an NMI between two
   of their `$09`/`$06` calls, and 1039 of 2847 steps took their frame before the wait (in the
   rest or the head). Placing the HUD's NMIs at the wait (the "open point" of "Race pacing") cannot
   follow the tape past one step; hence `--dsp4-tape-nmis`.
3. **With `--dsp4-tape-nmis` and the movie's pads (`--movie-offset 8113`): 911 refreshes**, loop
   steps 1-389, bench steps 8113-9023 (about 15 s of race), every call as recorded, port frame F =
   bench step F + 8113 throughout (the tape checks it at every NMI). Tests `game_race_tape_runs`
   (900 refreshes, 530,261 calls) and `game_race_tape_stop`. Fixed on the way, in the tape's
   placement (port side): 86 NMIs fell after a step's last call and before its rest (the first at
   loop step 9, bench 8138); run at the next call they counted for the next step (`$1E9E` 2
   instead of 3 at step 9, stop at frame 28); now `P`, run as the call returns. Without the movie
   the run stops at frame 578 (the pads drive the car).
4. **The stop at frame 911, call 538280** (bench step 9023, loop step 389): `object_sprite_shadowed`
   again (`FFC1` against `FFC0`): the bench's NMI of step 9023 fell after call 538274 and before
   the object's `$0024` read; the port can place a recorded NMI only at a call, here 538275, after
   the read. The limit of a tape placed at DSP-4 calls. Checked: with that one NMI marked to run
   after call 538274 instead (a scratch copy of the tape), call 538280 passes and the run goes to
   frame 965 (bench step 9077, loop step 408), the next known stop: in step 407 (bench 9073) an
   NMI fell between `race_frame_rest`'s read of `$00BA` and its clear, counted by no step on the
   console; the port counts it for the next step. Running every mid-step NMI right after the
   previous call stops sooner (frame 670).
5. **WRAM at three anchors** (`tape_anchors/`: the bench's WRAM at the entry of the J-th taking
   race NMI, the first from about 1 s, 3 s and 10 s on that falls in the loop's wait, as the
   port's do; the bench's pictures of that step and the next, **mis-paired:** a bench
   `frame_N.png` shows step N-3's picture (the bench renderer's two alternating buffers), so port
   frame F pairs with `frame_(F+8117).png`, and the saved pictures are earlier ones (the WRAM
   comparison below is not affected). The port's by `--wram-out` at frame
   bench step - 8113; `notes/race/tape_anchor_diff.py`): takes 25 (bench 8171, port frame 58), 72
   (8271, 158) and 221 (8624, 511) **equal the bench's but for `$00BC`** (the band IRQ's cursor;
   the port does not run that IRQ) **and `$1FD5-$1FF3`** (the 65816 stack, which the C does not
   use): 31, 30 and 29 bytes. (Two takes during the head, 70 and 220, differ also in the head's
   work half done on the bench: scratch `$0114-$011E`, the race order `$1E6A-$1E90`, the cars at
   `$FAxx-$FCxx`; hence anchors at the wait.)

**Not done here:** no change to the shipped pacing; the `frame.c` edits are test hooks only (one
line in `race_wait_refreshes`, `port_refresh_once`, `race_loop`). The expected cause, step 1
taking 1 refresh where the console took 3, is not the first stop and stays open (with the NMI tape
its NMIs are placed; without it, the port's step 1 keeps `$1E9E` = 1). The anchors were chosen
again, at takes in the wait, after a first set (takes 25, 70, 220) caught the head half done.

**Since decomp `c38d5ee` (2026-11-27):** `oam_high_load` (`$06`) and the sprite list's opening
are native, the `$09` stream not yet; the native `$06` then lacks the stream's entries, so no
recording can match the bench in this half-wired state. `game_race_tape_runs` and
`game_race_tape_stop` are disabled until the stream lands (`game_race_tape_wrapper_stop` still
passes). **Open items:** the native `$09`/`$06` (request A), then this file and the flags go; the race
drawn (nothing of it is drawn yet); for the shipped pacing, the HUD's NMIs and the frame counter
advance at the wait in the port and during the body on the console (an animation phase of a
frame, not a game difference); the pause and `race_end`'s frame waits under the NMI tape
(untested: the run stops before them).

## Closing the unmeasured stops (2026-11-27)

Ordinary play reached states the port stopped on because they were never compared. New input
movies (the port's own, `notes/movies/`, local and git-ignored, the bench's text format; their
recipes below and in each file's header; the scripts that made and compared them in
`notes/movies/tools/`), run on the decomp's bench at the pinned `ca47c97` and in the port.

- **How the movies reach later championships: the password screen's star names.** The code table
  at `$8A:CDAB` (strings XOR `$9A`) holds 12 star names (`password_code_run`: `MERAK` track 0,
  `ZOSMAR` 3, `SARIN` 7, `ALDERAM` `$0B`, `KAJAM` `$0F`, `LESATH` `$14`, `MIRAM` `$18`, `SUBRAT`
  `$1C`, `TOYGETA` `$20`, `VEGA 5` `$23`, `NAOSPHEIN` `$27`, `KRAZ` `$2A`). Typed (the grid's
  taps as `tools/password_movie.py`, with the 60-step glide wait for every last-column key, SPACE
  included), they set `$0444`; then 1 PLAYER FULL starts the championship at that track and
  `top_tallies` opens the shop lists the earlier tracks would have (`result_tally`). The
  difficulty variant (`$03DC` bits 8-9) is OPTIONS > DIFFICULTY (once: variant 1 to 2). All
  measured on the bench: `$0444` = `$2A`, `$03DC` = `$1110` / `$1210`, the list counts at `$036E`.
- **Movies:** `star_<name>.txt` (12: `code_infodata.txt` to the password screen, the name typed
  from step 3700, its tail from 5100 to the track description); `shop_buy_jump.txt`,
  `shop_buy_attractor.txt`, `shop_buy_warp.txt`, `shop_buy_infrared.txt` (OPTIONS, DIFFICULTY
  once, PASSWORD, KRAZ, 1 PLAYER FULL, RACE, the track description; in the shop Down to the
  list, A, B: one item each, see request Q); probes `kraz_probe.txt`, `kraz_probe2.txt`,
  `hard_probe.txt`, `two_probe.txt` (2 PLAYERS; also a test movie), `split_probe.txt`. The bench
  pictures named below are kept in `notes/movies/ref/` (renamed by test).

**Closed, pixel for pixel against the bench (0 of 57,344 pixels):**

- **The pre-race screen's animated planet** (`scenery_build` kind 2: `$0700` = its step, a 6x6
  block of BG1 tiles 476..511 from map word `$11DC`, palette 4). In every frame the frame code
  streams its picture: `$480` bytes from `$7F:6640 + k*$480`, the unpacked `$92:80B2` at offset
  `$3F40 + k*$480` (frame k of 16; the block was already converted at start-up). The port takes
  k from the stream's source (as the planets' shadows), checks the bytes against the block, and
  draws the frame's tiles from the converted block; a planet tile on screen before any frame was
  streamed stops. Compared: KRAZ (track `$2A`), every port frame 6298..6720 against bench
  N+165, 422 frames (the shutter opening, the camera moving, the planet turning, at rest);
  SARIN, LESATH, VEGA 5 (the three other systems with one), every 15th frame 6290..6560 at lag
  164/165; the other 8 systems every 5th frame of the same window (54 each). No per-line effect
  added.
- **"More planets than measured"** is not reachable: `scenery_build` gives every system at most
  six records (ROM table `$81:C3D0` by `$84:A240`), the animated one not counted, so at most 6
  shadow slots; the guard stays.
- **Shop lists 6-8** (Jump, Attractor, Warp; `$97:8000`, now converted at start-up as
  `SHOP_LIST_SPECIAL`): the bench's upload queue (`notes/shop/scan_ring.sh` on the movies) gives
  each item's picture (`+$1F20`/`$E60` Jump, `+$0E60`/`$10C0` Attractor, `+$0000`/`$E60` Warp,
  `+$2D80`/`$EA0` Infra-Red; first object drawing 276, sent as `$840` bytes and the rest) and,
  once bought, its level picture (`+$4C80`, `+$4A40`, `+$47C0`, `+$4F00`, `$280` bytes, drawing
  256). Lists 6-8's rows added to the measured table (list 9's left out: never compared, see
  below; its picture still stops the port, at `$7F:3680`); the item-picture cap rose to 134
  drawings (Attractor's). Compared: port 9296 = bench `frame_09475.png` for each (bought); Jump
  also at port 8984 (typed) and Warp at 9056. Other frames of the item screens match only in
  part (every 4th port frame 8600..9440 against every bench frame at lags 130..200: 36-46 of 211
  exact per list); the rest differ in small boxes (x 92..98, the caret's place; the turning icon
  at x 25..72, y 58..85; up to ~400 pixels while the text types). Not isolated; the console's
  longer loads put those animations in another phase is the likely cause (the item screens run
  at lag 152 before the text and 178..181 after it).
- **The two-player championship's opening, both halves lit** (2 PLAYERS, `two_probe.txt`): port
  frames 3248..3332 match the bench half by half (top at lag 170..177, bottom at 170..180,
  each half exact at every frame), with the bottom half's noise panels and fire band; whole
  frames port 3278 = `frame_03454.png` and 3320 = `frame_03497.png`, 0 pixels. No change was
  needed in `champ_screen.c` (its bottom-half path is the one-player one).

**Tests** (skipped where the movie is absent; a missing `--movie` file now exits 77 when
`--frames` is given): `game_picture_racestart_kraz_moving` (6340 = `frame_06505.png`),
`game_picture_racestart_kraz` (6600 = `frame_06765.png`), `game_picture_racestart_vega5` (6500 =
`frame_06665.png`), `game_picture_shop_jump`, `game_picture_shop_attractor`,
`game_picture_shop_warp` (9296 = `frame_09475.png`), `game_picture_champ_two_players` (3278 =
`frame_03454.png`). Test option `--shots FIRST LAST EVERY DIR` writes the pictures of a range of
frames (comparisons only).

**Out of reach, and why:**

- **Shop list 9 (Infra-Red):** reached (variant 2, KRAZ) and its picture uploads match the
  measured slices, but the C has no case for its view (`$80:AFE1`): no objects, no price, the
  view state differs from the bench's (decomp request R). List 10 ('P'): its count stays 0 in
  every variant (ROM tally tables `$85:D501`/`D530`/`D55F`), it never opens.
- **Back from an item screen to the list (blocks ordinary shop play):** observed: after Down on
  an item (row 0) the bench goes back to the list (`$014A` = `$E800`, the list slides in), the
  port stays on the item with its panel blank, for good. Inferred from the C: `shop_item_screen`
  loops for good and drops `shop_item_leave`'s result (request Q). So each movie sees one item.
- **The two-player championship's menus:** from port 3338 (bench ~3514) the menu text of both
  halves differs; the port stops at 3350 on the bottom half's sprite 7 (drawing 157, at
  136,148). Game memory differs there, not the drawing: port 3350 against bench 3526, task 2's
  table at `$1100` holds the four letters of NAME at the bench's places (x `$70..$88`, y `$94`)
  but with drawings `$A6`, `$99`, `$A5`, `$9D` and attribute 0 (the bench: `$127`, `$11A`, `$126`,
  `$11E`, attribute 3, in its table at `$1500`); task 2's D+`$14`/`$16` are `$0100`/`$0000` (bench
  `$7FF0`/`$B5A0`), and D+`$1E`, `$0270`-`$0278` differ. Decomp request S; out of reach.
- **The two-player shop and pre-race: not attempted.** Player 2's half waits for its own RACE
  (the bench's top bar stays closed from 4600 on) and both movie readers drive controller 1 only:
  a controller-2 column in both readers is needed first. `shop_screen.c`'s lit-bottom stop stays.
- **1 PLAYER SPLIT** ran without a stop up to the shop (bottom half dark); not compared.
- **The ticker's and the championship's lit bottom half after a race, the zoom-in ticker:**
  only after a race, which the port cannot run (DSP-4 sprite commands, request A).
- **Pre-race and shop states after a race** (later tracks of a championship played through):
  reached here by the star names instead; the same screens.

## The race runs (2026-11-28)

Since decomp `e152ffc` (the one-player race's `$09`/`$06` native) and `17dab56` (the power-on
rounding), the whole one-player race runs in the port from `race_wrapper` to its return.
**What this proves:** the race runs to its end in the port. With no pads, the tests pin the
port against itself, not against the original. Nothing of the race was drawn then (since
2026-11-28 its sky and HUD: "The race's sky and HUD drawn", below).

- **Length by pacing** (no pads, the original's memory at `race_wrapper`): 8259 frames with 2
  refreshes a step (the default), 8095 with 3, and 8231 with the original's recorded counts.
  The game's physics take the refreshes a step took (`$00BA`), so the race's real-time length
  stays close at either setting. This is the first evidence that the pacing setting keeps
  real-time speed (tests `game_race_runs*`).
- **The frame counter `$0024` in the race** (checked in the C at `e152ffc`): it is read only by
  the objects' animation rows (`object_sprite_kind0`/`_raised`/`_shadowed`/`_hover`) and
  by `nmi_race_full`'s block cycling every 4 frames (`race_cycle_blocks`, WRAM
  `$2000-$213F`). Both are pictures, not gameplay, so the console's longer load (210
  NMIs against the port's 89) shifts animation phases only.
- **Whole-game movies:** `race1` and `explore_buy` now pass their race and stop at an unknown
  palette block `$8E:8000` (port frames 19137 and 16809, after the race). `autopilot_60k`
  stops at the race wrapper's mode 1 (`$82:89B5`, port frame 20214), which the decomp is porting.
- **References:** until the decomp's chip matches the firmware everywhere, every race reference
  is the bench's stand-in DSP-4, not the console. Each race picture test says which.

## The race's sky and HUD measured (2026-11-27)

**For the owner's veto: the per-line effects the race's first slice will use.** Measured on the
bench at the tape's three anchors (`notes/probe/RACE.md` §12-20, local): a re-drawing from the
measured facts alone (every video word and colour from an upload the port makes, recognised by
its source; the per-line values from the game's WRAM tables) gives 0 of 57,344 pixels
different at all three. Nothing is drawn yet; this fixes what the draw function will rest on.

- **The sky** is colour 0 (`$7E:6A33`, set every NMI); the scroll table aims rows 4-106 at a
  transparent line. **The horizon** (rows 107-170) is BG1 (tiles from `$7F:6860`, map rows from
  `$7F:6460`/`$6660`, cut from the track's tile set); BG2 is a copy behind it that never wins a
  pixel there. **The HUD** is BG3 (map unpacked from `$87:8B3D`, font `$8F:85F5`, minimap tiles
  `$7E:230C`) plus sprites 0-1 (the minimap dots, after request A). No colour maths, no masking
  window at the anchors. **Mode 7: none** in the race (the load writes the identity matrix once).
- **Per-line effects the slice needs (each vetoable):**
  1. BG1's horizontal scroll per line (the game's HDMA 5; its table from the WRAM headers at
     `$06B3`/`$06D1`/`$06EF`, rows from `$83:A500`/`$83:A5DC` and WRAM `$1536`, `$1100-$15FF`);
  2. BG2's, the same (HDMA 6, `$072B`/`$0749`/`$0767`, `$1556`);
  3. the window edges per line (HDMA 4, `$7E:4CCC`, rows at `$7E:8AB7`, `$7E:2E2C`/`$318C`).
  Measured but for later slices: the band colour streams (`$21-$24` from `$7E:(00C8)`,
  `$31-$34` from `$7E:(00CC)`), the band BG mode byte (`$047E`/`$0480`) and the band line
  interrupts (`$0486`, masks `$0596`).
- **Bends still open:** the BG mode (request M), the HUD's words (request T: until named, a
  capture of `race_hud_draw`'s 52-word run by call pattern, like the pre-race meter), the scroll
  table in effect at non-taking refreshes (request U). Since 2026-11-28: U not needed, M guarded,
  T in use (below).

## The race against the bench, step by step (2026-11-28)

Measured (decomp `17dab56`, the bench's default DSP-4; `notes/probe/RACE.md` §21, local): the
port's race (`--screen race` from `race_wrapper`, `autopilot_60k.txt`'s pads, the recorded
refresh counts) against the bench's, WRAM at each loop step's wait entry (`$82:8656`: the port's
`--wram-out` saves before a refresh's NMI, which in the port always falls after the step's
head, so the head itself is not a comparable point). **The first gameplay difference is step 2
(port frames 89-91), from step 1:** step 1 has no race wait in the port and keeps `$1E9E` = 1
where the console counted 3 (the open pacing item), and the per-NMI accumulators lag by the two
NMIs (`$00D4` the horizon's scroll, the fade, `$0024`). From step 3 the speed, the road and the
cars differ and never re-converge. The matched prefix ends in forced blank: **no race picture can
be tested from the movie alone.** With the bench's memory handed over at step 2's wait (and the
pads re-aligned: port frame F = bench step F + 8026), the race is the bench's in gameplay through
step 406 (port frame 1045, about 16 s); only picture-side residue differs (the HUD's digits and
strip one HUD draw behind, objects' animation and the sprite list, the upload queue's split). At
step 407 one NMI the console counts for no step shifts the alignment by a frame (F + 8027 from
then on, the only shift in the whole race). Hence the race's picture tests hand the bench's
memory over at a wait (below).

## The race's sky and HUD drawn (step 4, 2026-11-28)

**Identical to the bench's pictures where drawn (the bench's stand-in DSP-4, not the console):**
the sky, the horizon and the HUD of the one-player race, `port/screens/race_screen.c`, drawn
after the NMI. **The road and the cars are not drawn yet: left black and marked not drawn**
(the screen's mask; a test compares only the drawn pixels and pins their count, so a blank never
passes as exact). Compared with `--compare` (below) at 6 frames, 0 differing pixels each: port
145 (brightness 6) against `frame_08175.png`, 400 / `frame_08430.png`, 800 / `frame_08830.png`
(hand-over at step 2), 1616 / `frame_09647.png` (a 19-row horizon on a hill; hand-over at step
600), 1651 / `frame_09682.png` (a sky past 127 rows, no horizon; step 612), 1885 /
`frame_09916.png` (rows from `$83:A968`; step 700): 35,396 to 44,042 pixels each. Scanned
(pictures 89-1045 after the step-2 hand-over, where the port's picture is not black): 657 equal;
231 differ only in HUD cells (the digits, the bar and the strip one HUD draw behind: the HUD's
NMIs placed at the wait, `$00B8` bit 2, `$4E4E`); 69 also elsewhere, which at frames 207 and 222
are bench object pixels (`render_check.py`'s layer map) of objects the port's sprite table
lacks (residue: `notes/probe/RACE.md` §21). At frame 400 the mask holds only bench backdrop,
BG1 (sky and horizon rows) and BG3 pixels (the layer map at `17dab56`). The pairing (port F = bench `frame_(F + 8030)`, 8031 from step 408) checked on two
fade frames.

- **How, under the rule:**
  - One draw function. Uploads recognised by source, in the load's order (`port/video/assets.c`,
    `race_pieces`): the track's tile set made planar (BG1/BG2 tiles `$2FE` on), its pixels equal
    to one of the **9 track tile sets unpacked at start-up** at the same offset (the game's own
    table, `$83:8585`/`$8597` by `$83:8447`; `$047B` never read); its four map slices (BG1 map rows
    24-31 left and right, then BG2's) checked against the set; the HUD font `$8F:85F5` (noted);
    the HUD map, the unpacked `$87:8B3D` as sent (all but its 26 patched words checked against the
    block); the markers' 4 words. Each NMI: the minimap's tiles (`$7E:230C`), colours `$71-$EF`
    (`$7E:6A35`), the sprite table (`$81:1576`/`$1796`), the HUD's words and strips.
  - Read from WRAM at draw time (the port's draw point sees the NMI's values): the brightness
    `$014B`, the window and colour-maths bytes `$01B0-$01B2` (checked `C2 B0 A1`), the scroll
    tables at `$83:(1E46)`/`(1E48)`, the windows' table at `$7E:4CCC`, the first band line.
  - Static registers (the load's screen image `$83:96DD`) as measured: mode 1 with BG3 in front.
  - Stops (`port_fatal`) on anything not measured: a lit picture before the first take or after a
    non-`$83CE` NMI, the pause, other window/maths bytes, other table layouts, a horizon line
    outside BG1 rows 24-31, another BG1 palette than 7, BG2 not a copy of BG1, a masking window 1
    on a drawn row, HUD tiles never sent or of priority 0, a first band line outside 109-188.
  - BG3 tile `$157` is never sent in the race (left-over video memory, zero on the bench): its
    cells (the markers, the unlit lights) are not drawn.
- **For the owner's veto, the per-line effects used** (no Mode 7: the race has none):
  1. BG1's scroll per row (HDMA 5's table, read in `scroll_tables_build`'s layout, only the
     entries measured, in their order: 4 rows under the frame; the sky from ROM `$83:A5DC`, past
     127 rows continued at `$83:A7D8`; the horizon, one WRAM entry `$1536`/`$153A`; rows from ROM
     `$83:A968..`; the road; checked to sum to 216 rows). The sky and horizon rows are drawn;
     the rest is the road (not drawn). The first entry's pointer high byte is left over in memory
     (`A5`, `55`, `0A`, `02` seen): not checked, its 4 rows lie under the HUD's opaque frame
     (checked every pixel).
  2. BG2's table (HDMA 6): only checked to give BG1's rows (`$1556`/`$155A`), with BG2's map
     slices equal to BG1's, so BG2 is a hidden copy (it is not drawn).
  3. The windows' table (HDMA 4): read only to check window 1 empty on every drawn row (no
     masking, no colour maths); masking stops.
  4. The first band's line (`$0486` + `$00BE`): BG1 is drawn above it only (from it the band
     interrupt switches BG1 to 16x16 tiles and the road's colours).
- **Request U: not needed by the port.** The port runs no loop body between a step's NMIs, so at
  its draw point WRAM `$1E46`/`$1E48` always name the tables the last `dma_hdma_set_table` calls
  gave: checked with a logging build (`notes/race/sync/make_sync_port.py`, `UCHECK_LOG`) over
  three whole races (`autopilot_60k.txt` with the recorded counts, no pads at 2 and at 3
  refreshes a step; 8,132-8,270 race refreshes each): different only before the first take
  (port frames 1-88, forced blank). The draw reads WRAM; a lit picture before the first take
  stops. No shadow of the call's argument.
- **Request M:** `$09` and `$29` differ only in BG2's tile size (bit 5), so M does not touch BG1,
  BG3 or the backdrop; BG2 is a hidden copy only with 8x8 tiles. Guard (listed for the veto, a
  stop, not a mode choice): the first band line must lie within 109-188, the range lit pictures
  showed over the three races (the register-only `$29` is written for no band or one at line
  220 or lower: never lit). Left for the road slice.
- **Request T, the one real bend:** the HUD's 52 words are taken by call pattern: a run of
  exactly 52 `video_write_vram_word` calls, confirmed by the strip upload that follows it
  (`$7E:4E61` or `$83:A5A4`, `$1A` bytes), placed by `race_hud_draw`'s fixed order (a table of
  52 map cells; 8 words are BG3 tile `$15C`'s pixels); the markers' run of 4 `$0157` words
  likewise. Any other word run in the race stops. The VRAM addresses are ignored at run time.
- **Also for the veto (the shop's two analogues):** (1) constants taken from the game's fixed
  upload destinations, as facts of its routines, not read at run time: the track's tiles at BG
  tile `$2FE` (`tiles_planar_upload_5fe0`), the font at `$158` and the minimap at `$0F4`, the 52
  HUD cells, the strips' and the markers' cells; (2) the HUD's 32x28 word array built at each
  draw (the load's map, then the markers', the last HUD draw's words and strips): the closest
  this screen comes to a video-memory-shaped structure (it holds only that map's named words).
- **Also for the veto, a bend in the shipped path until the cars slice:** `race_screen_draw` reads
  the sprite table the NMI sent (`$81:1576`/`$1796`, in its 544-byte layout: 9-bit x, the size
  bit) only to mark where objects cover the picture as not drawn (the tests' mask). It draws
  nothing from it; it goes when the cars and scenery are drawn by name.
- **Test only, listed for the veto: `--wram-at F FILE`** hands the bench's memory to the port at
  refresh F (before its NMI; a recording, like `--wram`), and **`--compare F PNG`** counts the
  differing pixels against a bench picture where the screen drew (the whole picture for other
  screens). The hand-over memories and the six pictures are local files under
  `notes/race/handover/`: the bench's WRAM (decomp `17dab56`, default DSP-4,
  `autopilot_60k.txt`) at the first instruction of `$82:8656` in bench steps 8115, 9631, 9668 and
  9907 (loop steps 2, 600, 612, 700; port frames 89, 1604, 1641, 1880), and the bench's
  `frame_N.png` pictures of the same run (`notes/race/sync/bench_dumps.sh` and `bench_pics.sh`
  make them).
- **Tests:** `game_picture_race_{fade,lit,lit_later,hill,sky_long,crest}` (skipped where the
  local files are absent); `game_race_runs*` run the whole race with the draw (no stop). The
  whole-game movies draw their first race and stop where they did before (`race1` 19137,
  `explore_buy` 16809, `autopilot_60k` 20214); `autopilot_60k`'s first race from its wrapper runs
  to its end at 2 and 3 refreshes a step and with the recorded counts.
- **Open:** the road (slice 2) and the cars and objects (slice 3); M for the road; T until the
  decomp names the words; the pause and a lit `$29` never measured (they stop); a sky of exactly
  127 rows (no continuation entry) is drawn but was not met at a compared frame; measured on
  track `$00` only.

## Steps (2026-11-24)

1. **Skeleton (current):** CMake build, SDL3 dependency fetch, SHA-1 ROM verification, empty 768x672 SDL3 window, CI for Linux and Windows.
2. **Platform layer proposal:** design the game-data boundary above `platform.h` and get owner sign-off.
3. **Sound interface:** native music and SFX playback interface.
4. **Menus drawn natively:** first playable/navigable screens drawn natively from RAM data.
