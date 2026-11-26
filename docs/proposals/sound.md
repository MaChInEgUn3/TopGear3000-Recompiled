# Proposal: sound

**Status: ACCEPTED (2026-11-24), the mixer question settled for now** (see `docs/DECISIONS.md`,
"Sound: the owner's questions"). Part A is asked of the decomp as request 4. Part B goes ahead with
the rule kept: conversions at load, our own mixer, tuned against recordings of the original.
Part A is done (decomp `e966da4`, `2d72646`). Part B's first target (set 1 and the three menu
effects) is built: `docs/DECISIONS.md`, "The native sound player (set 1)".

Facts below come from reading the decomp's disassembly (bank `$86`) and from throwaway
measurement tools run on the user's ROM (kept out of git). Statements about what the sound
driver does with the data come from a quick static reading of the driver's code and are marked
*(reading)*: hypotheses to confirm, not facts. No table of the driver or of the game's sound data
is reproduced here or anywhere in the repository; the port reads them from the cartridge.

## What was measured

- **The sound driver is the game's own** (no strings, and neither Nintendo's driver nor any
  driver a public tool supports; music by Neil Biggin). Its format has to be learned from the
  game itself.
- **Start-up:** the game unpacks the driver from the ROM and sends it with the console's standard
  loader: three blocks (tables, code, an empty song area).
- **Eight sound sets.** At each scene change the game loads one set: the intro (set 0), the menus,
  shop and results (set 1), one per group of race tracks (sets 3-6, by track), and a quiet race
  variant (set 7). Set 2 has no caller in the code mapped so far. Each set is:
  - one block of **songs** (two songs: a silent one and the set's music);
  - one block of **instruments and settings**;
  - **9 to 21 sound samples**, sent one by one, each with a loop point;
  - the **sample directory**, built by the game from the samples it just sent.
- **Music is chosen by the set.** The game's "music" byte only ever says 0 (silence), 1 (the
  set's song) or "keep playing". The seven songs of the public soundtrack are the songs of sets
  0-6.
- **Every frame** the game sends its 16-byte message (already in C): master, music and effects
  volume, the music byte, the engine sound's pitch, and up to two effect requests.
- **What the game uses of the sound chip:** up to 8 voices, effects taking voices from the music;
  no noise generator; no pitch modulation; a short echo (no feedback) only in sets 2-7, none in
  the menus; per instrument, the chip's envelope plus the driver's own software envelopes for
  volume and pitch *(reading)*.
- **The menus' three effects** (move, error, confirm) are not sequences: each plays one
  instrument of the set with the driver's software envelopes *(reading)*.

## Part A: the loading interface (request 4 to the decomp)

Today the loading routines are assembly, so **no screen of the port loads any sound yet**. The
decomp parked them "until a port designs the sound interface". The proposal, at the level of what
each routine already has in hand (so the decomp's bench can check every call against the
original's port traffic):

| Call (new in `platform.h`) | Made by | What the bench checks | What the port does |
|---|---|---|---|
| `sound_driver_boot(src)` | the start-up loader (`$86:E096`), once | the standard loader's blocks | nothing (there is no driver to run) |
| `sound_upload_block(spc_dest, src, size)` | `$86:E0F0`, once per `[size][dest][data]` block it walks | destination and bytes | the destination names the block: the song area means "songs of this set", the instrument area "instruments and settings of this set", the directory "ignored"; anything else stops with a message |
| `sound_upload_sample(slot, spc_dest, src, size, loop)` | `$86:E2B4`, once per sample | destination and bytes; slot and loop checked against the directory the game sends after | sample *slot* of this set, converted once into ordinary audio from the cartridge bytes |

`sound_send_frame` and `sound_is_ready` stay as they are. The decomp would port `$86:DDEF` (the
set loader) and its helpers to C on top of these calls. The race set-up waits for this too.

## Part B: the native player

1. **Samples** become ordinary audio (16-bit PCM) when a set loads, converted from the cartridge
   bytes the game names. This is the sound equivalent of converting graphics, already allowed.
2. **The sequencer is a native rewrite** of what the driver does with a song, written against the
   song format with named state (channels, instruments, durations, tempo), not a translation of
   the driver's program over an array of its memory. It understands only this game's format.
3. **The message** is turned into actions in C: volumes, music on/off, the two effect requests,
   the engine sound. *(Built: volumes, music, the first effect request. The second request and
   the engine sound stop with a message until a set uses them.)*
4. **The mixer** has its own interface (start a sound, let it go, cut it, set pitch and volume)
   and only what this game uses: no noise, no modulation, one short echo. Eight voices addressed
   by chip registers would be the chip, and is not done. *(Built without echo, which sets 0-1
   do not need: `port/sound/mixer.c`.)*
5. **Driver tables the player needs** (balance, volume steps) are read from the cartridge at
   start-up, never copied into the source. *(The pitch octave is computed instead: it equals
   `round(16384*2^(r/192))`, measured.)*
6. **First target:** the menus' set (set 1: its song, six instruments, no echo) and the three menu
   effects.
7. **Check:** the original's driver output measured on the decomp's bench (which note, pitch and
   volume each voice gets, tick by tick), compared event by event with the native sequencer; then
   by ear against recordings of the original made on the bench (kept outside the repository).
   The bench's `--voice-report` (decomp `d0eaf01`) gives this. *(Done for sets 0 and 1: every
   key-on, cut, let-go and pitch change identical; the listening comparison is next.)*

## Question for the owner: how close may the mixer follow the chip?

The sequencer decides which sample plays, at which pitch and volume, with which envelope. How
those become sound is where the line is closest:

- **(a) Our own sampler.** Our interpolation, envelopes shaped from the instruments' settings with
  our own curves, our own short echo. Sounds close, not identical.
- **(b) The chip's timing for envelopes, the rest ours.** Envelopes attack, decay and release
  with the chip's speeds (that is what makes notes last as long as on the console);
  interpolation and echo stay ours. Recommended: the musical timing is right, and the chip's
  sound path (interpolation filter, echo filter) is not reproduced. *(What was built for set 1,
  per the owner's rule: the envelope times converted at load, our own smooth curves through
  them, cubic Hermite interpolation.)*
- **(c) The chip's exact curves.** The chip's interpolation filter, envelope steps and echo
  filter, reproduced as math inside our mixer (still no registers). Sounds identical, and is
  the closest to a model of the sound chip.
