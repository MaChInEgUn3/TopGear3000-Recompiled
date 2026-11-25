# Decisions & confirmed facts

Single source of truth for the port's scope and decisions. Dates are absolute.

## Scope (2026-11-24)

- **Separate repository from the decomp.** `extern/tg3000-decomp` is read-only here, pinned as a submodule at a named commit (currently `49b5583`; earlier `2ade543`, `f84afeb`, `8238cd1`, `75e8d14`) and bumped explicitly. Requests for changes in the decomp go to the list `tg3000-port-requests` in the claude-mem work state, and the owner is told; never direct edits in `extern/`.
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

## The OPTIONS submenu: drawn, but an animation phase differs (step 4, 2026-11-25)

- The options menu's OPTIONS submenu (1 PLAYER FULL, 1 PLAYER SPLIT, 2 PLAYERS, PASSWORD, EXIT;
  `explore_options.txt`, bench steps 3190/3191) is drawn by the options table as is: same
  blocks, colours and band tables. Background, text and colours match the original.
- **No frame of the port equals the original's picture there,** measured: the red box around
  "OPTIONS" (20 objects) slides up 4 px per frame and the title glows through the band table's
  32-step pulse. In the original the box stops sliding about 8 frames before the glow's peak; in
  the port 2 frames after it, about 10 frames of phase between the two animations. The best
  port frames are 264-280 pixels off, all in the box. It is a game-state difference, not a
  drawing one: the same known difference (frames pass on the console while it unpacks graphics,
  not in the port; hypothesis), here visible because two animations combine.
- Not forced (no simulated loading delay); no exact-picture test for this submenu.

## Steps (2026-11-24)

1. **Skeleton (current):** CMake build, SDL3 dependency fetch, SHA-1 ROM verification, empty 768x672 SDL3 window, CI for Linux and Windows.
2. **Platform layer proposal:** design the game-data boundary above `platform.h` and get owner sign-off.
3. **Sound interface:** native music and SFX playback interface.
4. **Menus drawn natively:** first playable/navigable screens drawn natively from RAM data.
