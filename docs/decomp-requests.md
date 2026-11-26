# Requests from the port to the decomp

The port (this repository) consumes [tg3000-decomp](https://github.com/MaChInEgUn3/tg3000-decomp)
read-only. What it needs from the decomp is listed here, by priority. This file is the source
of truth for the requests (an earlier copy went to a claude-mem list the decomp session did not
read). Updated 2026-11-26; the port pins the decomp at the commit recorded in `docs/DECISIONS.md`.

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
- **B (request 4) — steps 1-3 (driver start-up, set loader, its race/VS/menu callers) DONE in decomp `e966da4`/`2d72646`/`b67f44e`, adopted; the intro's call $80:89E7 waits on C.** Sound loading in C on three new `platform.h` calls** (design:
  `docs/proposals/sound.md`, Part A): `sound_driver_boot(src)` for the IPL upload `$86:E096`;
  `sound_upload_block(spc_dest, src, size)` once per `[size][dest][data]` block walked by
  `$86:E0F0`; `sound_upload_sample(slot, spc_dest, src, size, loop)` once per sample sent by
  `$86:E2B4` (slot and loop are in hand at `$86:DF2A`; the bench can check them against the
  directory sent after). Then `$86:DDEF` (the set loader) and its helpers in C. Names and shape
  may change if the bench needs it.
- **C (request 1). Boot and top-level dispatcher in C:** the reset path (`$80:8002-$80:809B`),
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

## Low (enhancements)

- **I (request 3).** Name the ROM source of decompressed uploads (e.g.
  `video_note_source(rom_bus, vram_word)`, ignored by the bench's comparison).
- **J (request 6).** Hand over the VS panels' arrangement by name (`vs_graphics_load` writes it
  word by word today; the port recognises the write pattern).
