# Proposal: the port's video and frame layer

**Status: PROPOSED (2026-11-24), waiting for the owner's approval.** No port code is written
for it until then. Facts are measured on the decomp at `f84afeb` with a throwaway recorder
(every `platform.h` function logging its calls, not committed).

## What was measured

- **The game's C runs on the PC by itself.** From zeroed memory plus
  `interrupt_vectors_init()`, `title_card_show()` advanced 240 frames, uploaded its graphics and
  palettes, and returned on its own, as on the console. `password_show()`, `options_screens(0)`
  and `vs_screen()` ran 300 frames without a crash, each waiting for input.
- **Screens are blocking functions.** Each loops on `wait_frames()` / `platform_wait_vblank()`
  until it is done. No C dispatcher between screens exists yet (decomp request 1).
- **Graphics arrive as whole blocks.** On the measured screens, every tile upload is either a
  whole block just decompressed from the ROM into the scratch area `$7F:0000`, or a direct ROM
  address. The scratch area is never decompressed into while it holds the live tile map.
- **The rest is game RAM.** The text screens build their tile map at `$7F:0000` (uploaded every
  frame), their 128 objects at `$0500-$08FF` (turned into the sprite table at `$0900` by
  `oam_from_objects`), and palettes come from the ROM. `$0500-$08FF` is the object table on
  these screens and the palette shadow on the two-task screens.
- **Per-line effects:** title card, password display and VS screen use none. The options menu
  uses two (a horizontal scroll per line for one layer and a fixed-colour gradient).

## 1. The game owns the loop

The port does not call the game once per frame. The game runs on the main thread and the port
lives inside `platform_wait_vblank()`, the frame boundary:

1. **Run the game's frame code** (its NMI, already C) when the game has it enabled (the last
   `platform_set_interrupts` value, bit 7): `nmi_prologue()`, then dispatch on the word the game
   keeps at `$0008` through a fixed table of the known targets. An unknown target stops with its
   value. Line interrupts are not run in the first target (see "Open").
2. **Draw** the active screen with that screen's own code (section 3).
3. **Present** the 256x224 picture scaled into the window, **pump events** (input for the next
   `input_pad1()`), **pace** at 60 frames per second.
4. **Quit** by a long jump back to `main` (the game's stack is abandoned; nothing to save yet;
   save data comes with the screens that write it).

`platform_set_interrupts` is therefore timing, not housekeeping.

## 2. What happens to each platform call

Rule: **ignore by default.** A value is kept only if the screen code cannot read it from game
RAM when it draws, and then it is kept under a name, never as a copy of a chip register or of
video memory indexed by address.

| Group | Functions | What the port does |
|---|---|---|
| Ignored (the screen code reads RAM or knows its own layout) | all `video_set_*` screen registers (modes, maps, tiles, layers, windows, colour math, scrolls, Mode 7, mosaic, display), `video_set_oam_address`, `video_upload_oam`, `video_set_vram_address`, `video_set_vram_increment`, `video_set_cgram_address`, `video_set_screen_init`, `platform_set_rom_speed`, `platform_set_wram_address`, tile-map uploads from `$7F:0000` | nothing |
| Named data events | `video_upload_vram`, `video_fill_vram`, `video_write_vram_word` of graphics; `video_upload_cgram`, `video_set_color` | "block *B* is loaded for this screen", "palette *P* is loaded at colour *n*": the block is identified by content (a hash table built at start-up from the user's ROM with the decomp's own `decompress`) or by its ROM address, and converted once into an ordinary image |
| Per-line (owner's grey zone) | `dma_hdma_*`, `video_hdma_enable`, `dma_stream_*`, `platform_set_irq_*` | ignored; where the owner allows it, a screen's code reads the table the game computed in RAM and draws that one effect |
| Timing, input, sound | `platform_wait_vblank`, `platform_set_interrupts`, `video_is_pal` (false), `input_*` (SDL3 keyboard/controller), `sound_*` | section 1; sound is step 3 |
| Cannot serve yet | `dsp4_port_read16/write16` (race only) | stop with a clear message |

## 3. Drawing: one function per kind of screen

- A **software picture** of 256x224 pixels, drawn in C, uploaded to one SDL texture and scaled
  (pixel-exact, no widescreen for now). No GPU tricks needed for these screens.
- Each screen kind has its own drawing function that knows its layout: for the text screens,
  "the background is the tile map at `$7F:0000`, 32 columns, its tiles come from blocks *X* and
  *Y*, then the 128 objects of `$0500-$08FF` on top". This is per-screen knowledge written by
  hand from the decomp, checked against what the game uploads.
- Which screen is active comes from the game's own state (the frame-code target and the screen
  routine running), not from register values.

## 4. First target and order

1. **Title card** (`title_card_show`): no per-line effects, returns by itself.
2. **Password display**, then the **VS screen**: no per-line effects, need input.
3. **Options menu**: needs the owner's ruling on its two per-line effects.

Each is checked by eye against reference pictures of the original from the decomp's bench (a
measuring tool, never shipped; pictures stay outside the repo).

## 5. Test channel (later, from the YGO FM port's practice)

Debug builds only: a local TCP command channel (ping, frame number, press buttons, read game
RAM, save a picture to a folder outside the repo) so scripts and agents can drive and check the
running port; screens recognised by picture, not by frame counts; in-memory ring buffers
instead of log files.

## Open

- **Two-task screens** (championship, shop): two blocking tasks that the console switches
  between with interrupts. Not in the first target; no claim yet on how to run them.
- **The race:** needs the native DSP-4 commands (`$01/$06/$07/$08/$09`) still being written in
  the decomp, and its per-line road.
- **The whole game from power-on:** decomp request 1.

## Owner's decisions asked

1. **Per-line effects:** may a screen's code read a table the game computed (the options menu's
   gradient and per-line scroll) and draw that effect, while the per-line platform calls stay
   ignored?
2. **The per-screen graphics table** ("this screen's background uses blocks X and Y with these
   palettes"): this is the point closest to the line. Is it on the right side?
3. **Software picture at the original 256x224, scaled**, as the start (widescreen or higher
   resolution only later, if ever)?
