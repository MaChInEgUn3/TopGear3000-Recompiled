# Requests from the port to the decomp

The port (this repository) consumes [tg3000-decomp](https://github.com/MaChInEgUn3/tg3000-decomp)
read-only. What it needs from the decomp is listed here, by priority. This file is the source
of truth for the requests (an earlier copy went to a claude-mem list the decomp session did not
read). Updated 2026-11-28; the port pins the decomp at the commit recorded in `docs/DECISIONS.md`.

## High

- **A. The DSP-4 race commands as native C.** `src/misc.c` still talks to the chip through about
  270 `dsp4_port_write16`/`dsp4_port_read16` calls; `src/dsp4.c` is native only for `$00`
  (multiply), `$0A` (edges), `$11` (row), `$03`/`$0B` (sprite). Needed: `$01`
  (`road_rows_project`), `$07` (`road_rows_project2`), `$0D` (the VS race's road, four sites `$85:A59F`/`$85:A8E8`/`$85:AC53`/`$85:AF9B`;
  corrected by the decomp), `$09` (`object_sprites_stream`, `sprite_put_rows_*`,
  `car_sprite_head`, `sprite_part_put`), `$06` (`oam_high_load`). The port's `dsp4_port_*` stop
  with a message, so no race runs without them. **Shape (2026-11-26):** the race's C must call
  native command functions directly with arguments and results (as for `$0A`/`$11`/`$03`/`$0B`).
  A byte-protocol model of the chip (`Dsp4Chip`, decomp `13e8350`) behind `dsp4_port_*` would be
  a DSP-4 model in the shipped game, which the rule forbids; it stays a bench tool.
  **Progress (2026-11-26):** `$01` native (`dsp4_road_begin`/`dsp4_road_row`, decomp
  `b6a29ec`/`daf012f`, exact on every gate movie after a `$0A`); being wired into
  `road_rows_project`. Still needed before a race runs in the port: `$07`
  (`road_rows_project2`, `$85:B962`), `$08` (`road_rows_stream`, `$81:861D`), `$09`, `$06`, and
  `$0D` for VS. `$07` native too (decomp `0e2a6af`). The port stops at every
  `dsp4_port_*` call, products included; the decomp agreed to route the products through the
  native multiply. Count of `dsp4_port_*` calls left in `src/`: **155** after `$08` (dsp4-08g; was 191), all in
  `misc.c` (sprite streams, `$06`, `$08`, the VS road, small product/row helpers).
  **Measured in the port (2026-11-26, `--screen race` from the original's memory at
  `race_wrapper`):** the one-player setup runs through; the first stop is `object_sprites_stream`
  (`$09`) in the first `race_frame_draw`. Left for the one-player race, by function (calls):
  `rival_car_sprites` 21, `player_car_sprites` 14, `car_sprite_head` 10, `sprite_parts_list` 8,
  `sprite_parts_list_at` 8, `sprite_bar_put` 8, `sprite_parts_at` 7, `sprite_part_put` 6,
  `object_sprites_stream` 6, `sprite_parts_drain` 4, `oam_high_load` 2 (94 in all); VS only:
  `vs_sprites` 31, `vs_proj_pass` 22 with `road_port_product32` 5, `oam_high_load2` 2,
  `vs_proj_tail` 1. Please keep the results in the sprite tables in WRAM as now (`$1576`/`$1796`,
  high table at `$1776` + `D+$8E`): the port draws from them.
  **One-player race DONE (decomp `e152ffc`, adopted 2026-11-28):** its C no longer calls
  `dsp4_port_*` (`$09`/`$06` through `dsp4_list_*`, `dsp4_objects_*`, `dsp4_high_table`); the
  port runs the whole race and the test tape is gone. **Left: the VS race** (82 lines in
  `misc.c`: the `_vs` copies, `vs_sprites`, `oam_high_load2`, `vs_proj_pass`/`vs_proj_tail`/
  `road_port_product32`; VS opens its lists with `$000E`), and the power-on rounding of the
  first frames (next in the decomp).
- **B (request 4) — steps 1-3 (driver start-up, set loader, its race/VS/menu callers) DONE in decomp `e966da4`/`2d72646`/`b67f44e`, adopted; the intro's call $80:89E7 waits on C.** Sound loading in C on three new `platform.h` calls** (design:
  `docs/proposals/sound.md`, Part A): `sound_driver_boot(src)` for the IPL upload `$86:E096`;
  `sound_upload_block(spc_dest, src, size)` once per `[size][dest][data]` block walked by
  `$86:E0F0`; `sound_upload_sample(slot, spc_dest, src, size, loop)` once per sample sent by
  `$86:E2B4` (slot and loop are in hand at `$86:DF2A`; the bench can check them against the
  directory sent after). Then `$86:DDEF` (the set loader) and its helpers in C. Names and shape
  may change if the bench needs it.
- **C (request 1) — boot, `game_main`, the top loop and its ends DONE (decomp `b284d72`…`ecb9653`), adopted. Left: named traps one level down (`$8A:D017` ending, `$8A:96AD` two-player last screen, `$8A:C379` options choice 3, `$80:8A04`, `$03DC` bits 15/13), ported when a run reaches them.** Boot and top-level dispatcher in C: the reset path (`$80:8002-$80:809B`),
  `$80:89DB`, the top loop `$80:8A8C`/`$80:8AA4`.

## Medium

- **D (request 7) — DONE in decomp `506cc7d` (ticker, shop, championship as two tasks), adopted.** The two-task kernel's tails in C:** `nmi_menu_frame` (`$80:B97B`) and
  `irq_menu_frame` (`$80:BC9B`) stop at their stack switches (`$80:BC54`/`$80:BDC1`) and the
  callbacks `JSR ($01EC)`/`($02EC)`. Championship, shop and ticker cannot run. A C-level way
  to run task 1's frame then task 2's frame (the tasks share no WRAM, per the decomp's notes),
  or guidance on how the decomp wants it composed.
- **E (request 8) — DONE in decomp `92d4d5a`, adopted by the port. NMI target `$80:FC14` (results screen) as `nmi_target_80FC14()`:** the port
  carries a marked three-line hand translation, unverified, until then.
- **F (request 2) — DONE in decomp `92d4d5a`, adopted by the port. `tg3000_unported` should call a platform hook first** (e.g.
  `void platform_unported(const char* what);`), so a player sees what was hit (24 traps today).
- **G (request 5) — DONE in decomp `9bc4a9a` (bench `--voice-report`: every S-DSP write per voice).** A bench report of the sound driver's output per voice** (per tick: key on/off,
  sample, pitch, volume, envelope settings), the reference for the port's native sequencer.
- **H (request 9) — DONE in decomp `436b6d7`/`d92e6e6` (the C keeps its own copies; both calls left platform.h), adopted. Earlier: measured in decomp `0cbbece` (values are the game's own); the port still needs the C to keep WRAM shadows instead (a VRAM strip copy or a register shadow in the port would break the rule). Chip state read back:** `video_read_vram_word` (`vs_hud_draw`) and
  `dma_hdma_get_table` (`irq_vs_split`) need state a non-emulating port does not keep. If
  possible, have the C keep its own WRAM copy of what it reads back, or name the value.

- **K. Name the pre-race meter's words** (`draw_meter`, `hud.c`): before its 10
  `video_write_vram_word` calls, a note like `video_note_vs_panels`, e.g.
  `video_note_meter(vram_word, value)` or the words' WRAM source, so the port stops recognising
  them by call pattern (recorded as a bend in DECISIONS, pre-race screen).

- **L — ANSWERED by the decomp (2026-11-26): only the 65816's time slices (the decompression spans ~10 slices per task; task 1's share is shortened by the NMI's DMA); no head start, no new contract rule. The port leaves the 1 frame.** Question (not a change request): the championship tasks' phase after the load**
  (2026-11-26; DECISIONS, "The championship's halves, each as its interrupt sends it"). During
  the championship's load (`champ_task`: five `queue_unpacked_upload`, then `champ_slot_wait`),
  the first four uploads come out at the same bench steps for both tasks (3315, 3316, 3321,
  3323, `autopilot_60k.txt`), but task 2 queues the `$98:8000` one at step 3334 and task 1 at
  3335, so after the load task 2 runs one `$0024` ahead of task 1 for the whole opening (fade,
  bar, slide). The port's tasks run in step under the agreed order, so its bottom half is one
  frame behind the original's. Is this only the 65816's time slices (task 1's share of the frame
  shortened by the NMI's work), or does the original give task 2 a head start somewhere (the IRQ
  trampoline at `$0005`, the first `nmi_menu_frame`, `split_screen_setup`) that the
  `platform_task_split` contract should state?

- **M. The race NMI's BG mode as game state** (2026-11-26). `race_bands_start` (in
  `nmi_race_full`, and `vs_bands_start`) writes BG mode `$09` or `$29` straight to the register,
  chosen from the first band line (`$0486` + `$00BE`, `$002E`); nothing in WRAM records the
  choice. A WRAM copy (or a named note) would let the race's drawing read it instead of
  re-deriving the game's test.
  **Measured (2026-11-27, `notes/probe/RACE.md`):** `$09` at all three race anchors (bench steps
  8171, 8271, 8624); `$29` only at the loop's frames 0-1, under forced blank.

- **N — CONFIRMED by the decomp (2026-11-27): mode 1 runs with the firmware DSP-4 (its coverage map came from the bench's own chip, whose answers take another path); likely a demo race mode (bit 13 of `$03DC`); being ported.** The race wrapper's mode 1 runs in `autopilot_60k.txt` (observation, 2026-11-26; firmware
  bench). `race_wrapper` is entered with `$03DC & 3` = 1 (`$03DC` = `$2001`, `$047C` bit 15) at
  step 44736 (track `$14`, wrapper tail at 52506) and step 57099 (track `$13`). The C has
  `tg3000_unported("$82:89B5 ...")` there ("never ran"). Inferred from `$03DC` at the wrapper's
  entry (`race_wrapper_head` does not write it); `$82:89B5` itself was not hooked. The port's
  whole-game run would stop there. **Checked at the switch (2026-11-27):** WRAM dumped at the
  mode call `$82:83B8` on every wrapper entry (firmware bench, `autopilot_60k.txt`): `$03DC & 3`
  = 1 (`$03DC` = `$2001`, `$047C` = `$FF00`) at 44736 (track `$14`) and 57099 (track `$13`), so
  `$82:89B5` runs there; mode 0 at 7903/23259/33079, mode 2 at 20362/39876.

- **O — ANSWERED by the port (2026-11-27): not a C issue.** On the firmware bench `$045F` = `$01`
  at `race_track_build` (`$82:856D`, step 8093) and `$FF` at `race_track_start` (`$82:85D6`,
  8110), with 18 race NMIs (`nmi_race_tail` → `sound_frame` → `sound_send_message`, which writes
  `$FF` after each message) in between: a message sent during the console's load frames. The
  port has no frame inside `race_track_build`, so the same message goes out at
  `race_track_start_head`'s frame wait (port frame 87, `$FF` from 88). Nothing to change.
  **Question: `$045F` after the race's load** (2026-11-26). From the same wrapper memory, the
  port's WRAM at `race_track_start` equals the bench's except pads/counters and, on track 0,
  `$045F` (port `$01` as `race_sound` writes it, bench `$FF`). Is `$FF` written by something on
  the console side (the sound handshake) that the C leaves to the platform?

- **Q — DONE in decomp `6c6bd22` (`shop_item_screen` returns when down/up leave), adopted; test `game_shop_leave_runs`. Was: CONFIRMED (2026-11-27): the fix (`if (shop_item_down(dp)) return;`, the same for up) comes as its next unit; `shop_buy_dispatch` has no leave path (every buy handler checked). Leaving a shop item screen does not return in the C** (2026-11-27; port movies
  `notes/movies/shop_buy_*.txt`, DECISIONS "Closing the unmeasured stops"). Observed: the bench
  goes back to the list after Down on an item, the port never does. Inferred from the C (not
  traced): `shop_item_screen`
  (`$80:8EB4`) loops for good and drops `shop_item_up`/`shop_item_down`'s result; their leave
  (`shop_item_leave`, "the original drops its return address and returns from its caller")
  returns true, but nothing ends the loop. In the port, Down at row 0 (or Up/Right at the last
  row) blanks the panel, `shop_panel_clear` waits, and the item screen stays on (every frame the
  loop's `shop_item_frame_tail` queues the panel again, so the wait ends and the loop goes on);
  the original goes back to the list (bench: `$014A` = `$E800`, the list slides in). Ordinary play
  cannot leave an item for the list in the port, so the movies see one item each. Please end
  `shop_item_screen` when the leave runs (as the original's stack drop does).

- **R. The item views of lists 9 and 10 are missing** (2026-11-27). `shop_item_dispatch_view`
  has no case for `$80:AFE1` (list 9, Infra-Red) and `$80:B048` (list 10, 'P'), the words the
  table `$81:A897` holds for them (`default: break`, commented "no other view for lists 0-8").
  List 9 opens in ordinary play (difficulty variant 2, from track `$2A` on: `result_tally`), and
  its item screen in the port then has no picture objects, no price, no view state (port 9200
  against bench 9358 in `shop_buy_infrared.txt`: D+`$3E`/`$40`, the sprite table differ; 1,180
  pixels in the picture area). The port draws what the C leaves, so list 9 is out of reach until
  `$80:AFE1` is in C; list 10 never opens (its count stays 0 in every variant).

- **S. The two-player championship's menu: task 2's state differs from the original**
  (2026-11-27; port movie `notes/movies/two_probe.txt`: 2 PLAYERS, offset 150). The opening
  matches (port 3248..3332, both halves); from port 3338 / bench ~3514 the menus' text differs.
  At port 3350 against bench 3526: task 2's sprite table holds NAME's four letters at the
  bench's places with drawings `$A6 $99 $A5 $9D` and attribute 0 where the bench has `$127 $11A
  $126 $11E`, attribute 3 (palette 1, the drawings' ninth bit); task 2's D+`$14`/`$16` are
  `$0100`/`$0000` (bench `$7FF0`/`$B5A0`); D+`$1E` (`$01A0`/`$06C0`) and `$0270`-`$0278` differ
  too. One-player runs never drew task 2's menu text. Is a path of player 2's menu reading or
  writing task 1's direct page (or a fixed `$01xx`)?

- **V. The race wrapper's NMI `$83:8623` in C** (2026-11-28; DECISIONS, "After the race: the
  whole-game movies' next stops"). `race_wrapper_head` points the NMI at `$83:8623`; `race_load`
  replaces it before any wait, but the VS race's load waits under it (`vs_race_load` to
  `vs_race_start`: 97 frames on the firmware bench, `race1.txt` steps 19472..19569). It is only
  in `asm/bank_03.asm` (`sound_frame`, `pads_read`, `$0024`++, `$00BA`++, no `$0008`
  dispatch). `race1.txt` and `autopilot_60k.txt` reach it after their first race, where the
  title's demo is a VS race (`$03DC` = `$2002`; bench steps 19472 and 20362); `explore_buy.txt`
  goes to a championship instead. The port stops there by name (port frame 19137 of `race1`;
  20093 of `autopilot_60k` run as the console ran its race). Please give it a C function (e.g.
  `nmi_race_wrapper()`), as `nmi_race_tail` is.

- **W. The VS race loop's way out** (2026-11-28). `race_mode_vs` loops for good and
  `vs_race_frame_body` traps at `$82:9642` ("the VS race loop's way out"); the demo's end on a
  button in `vs_race_frame_tail` is marked never run. **Observed** (firmware bench,
  `autopilot_60k.txt`, WRAM per step): its VS demo race (`race_wrapper` at step 20362, the VS
  NMI `$9777` from 20454) is left at 20465 (NMI target back to `$8934`, then `$03DC` = `$0002`
  at 20466) and the title (`$FC5F`) is back at 20491; the autopilot holds buttons there.
  **Inferred** (not hooked): the demo's end on a button and `$82:9642` run there. Needed after
  V and A's VS part for any VS demo that ends, and for the VS game's race end.

## Low (enhancements)

- **I (request 3) — DONE in decomp `d0eaf01` (`video_note_source`), adopted (ignored for now; content recognition kept).** Name the ROM source of decompressed uploads (e.g.
  `video_note_source(rom_bus, vram_word)`, ignored by the bench's comparison).
- **J (request 6) — DONE in decomp `f22a687` (`video_note_vs_panels`), adopted: the port builds the VS panels from the 14 rows the game names; the write-pattern capture is gone.** Hand over the VS panels' arrangement by name (`vs_graphics_load` writes it
  word by word today; the port recognises the write pattern).

- **P — WITHDRAWN (2026-11-28): the test entry that needed it went with the DSP-4 tape.** The one-player race loop as its own function (2026-11-27). The port's test entry
  `--screen race_loop` (DSP-4 tape, tests only; DECISIONS, "The DSP-4 tape (tests only)") starts at
  the loop's first head and retypes `race_mode_one`'s four-line loop and `race_end` in `frame.c`.
  A named `race_mode_one_loop()` (the `do { head; wait; body } while (!rest); race_end();` part)
  called by `race_mode_one` would let the port call the game's own code there.

- **T. Name the race HUD's words** (2026-11-27). The HUD (BG3) is redrawn only by the race's
  non-taking NMIs: `race_hud_draw` writes 52 words a HUD NMI, and `hud_markers_put` writes its
  own, through `video_write_vram_word` with no WRAM home. WRAM runs ahead of the picture (at
  bench step 8624 the clock in WRAM reads 1"32, the picture 1"27). A note per run, like
  `video_note_vs_panels` (e.g. `video_note_race_hud(index, value)`, or the words' WRAM source),
  would let the port draw them by name instead of capturing the 52-word run by call pattern (a
  bend, as for the pre-race meter, request K).

- **U. The race's scroll table in effect as game state** (2026-11-27). At non-taking refreshes
  WRAM `$1E46`/`$1E48` already name the next BG1/BG2 scroll table (7 of 7 frames tested), while
  the picture still shows the one the last `dma_hdma_set_table` call gave. A WRAM copy of the
  table in effect (or a named note) would spare the port a shadow of that call's argument
  (decision H: no register shadows in the port).
