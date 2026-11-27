#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-late
#
# Regenerate the seven local 131072-byte WRAM snapshots used by the ctest picture tests:
#   - wram_before_tick.bin         (notes/tick/wram_before_tick.bin) -> ticker_screen
#       autopilot_60k.txt, WRAM at the end of step 15857
#   - wram_before_res.bin          (notes/res/wram_before_res.bin)   -> game_picture_results
#       autopilot_60k.txt, WRAM at the end of step 16489
#   - wram_before_title_screen.bin (notes/title/wram_before_title_screen.bin) -> title_screen_run
#       autopilot_60k.txt, WRAM at the end of step 16662
#   - wram_before_pw.bin           (notes/pw/wram_before_pw.bin)    -> game_picture_password_show
#       autopilot_60k.txt, WRAM at the end of step 41070
#   - wram_before_opt.bin          (notes/opt/wram_before_opt.bin)  -> game_picture_options
#       explore_options.txt, WRAM at the end of step 2955
#   - wram_before_title.bin        (notes/opt/wram_before_title.bin)-> game_picture_vs
#       explore_options.txt, WRAM at the end of step 367
#   - wram_before_race.bin         (notes/race/wram_before_race.bin) -> game_race_load_runs
#       autopilot_60k.txt, WRAM at the first instruction of race_wrapper ($82:8342, step 7903,
#       line 10). No step end lands at a clean point there (the end of step 7902 is between the
#       shop's last NMI and race_wrapper: $0005, $0008, $0024 and $03E0 still change), so the
#       bench is patched for one run (a WRAM write at that PC), then restored with git checkout
#       and rebuilt. Needs a clean tools/bench in the bench tree.
# With DSP4_FIRMWARE=<path to the DSP-4 firmware> also (notes/race/, see record_race_pacing):
#   - race_refreshes.txt  -> --race-refresh-replay (game_race_load_runs_refresh_replay,
#                            game_race_wait_replay)
#   - wram_race_wait.bin  -> --screen race_wait (game_race_wait_*)
# Always (notes/race/, see record_race_tape; the bench's default DSP-4, no firmware):
#   - race_tape.txt           -> --dsp4-tape (TEST ONLY: the race's $09/$06 port calls, recorded)
#   - race_refreshes_tape.txt -> --race-refresh-replay, the same run's refresh counts
#   - race_tape_marks.txt     -> the same run's NMIs, loop heads, waits, rests (bench steps)
#   - wram_race_loop.bin      -> --screen race_loop (WRAM at the race loop's first head)
#   - tape_anchors/           -> WRAM at three taking NMIs, the bench's pictures there
#
# WARNING: The generated files contain game memory data and MUST stay out of git.
#
# Usage:
#   [DSP4_FIRMWARE=input/dsp4.rom] tools/make_reference_memory.sh <tg3000-decomp-dir> <rom-path> <output-dir>
# (the files go into <output-dir>; the ctests read them from notes/<screen>/, as listed above)

set -euo pipefail

if [ "$#" -ne 3 ]; then
  echo "Usage: $0 <tg3000-decomp-dir> <rom-path> <output-dir>" >&2
  exit 2
fi

DECOMP_DIR="$(cd "$1" && pwd)"
ROM_PATH="$(cd "$(dirname "$2")" && pwd)/$(basename "$2")"
mkdir -p "$3"
OUT_DIR="$(cd "$3" && pwd)"

BENCH="$DECOMP_DIR/build/bench/bench"
# bsnes v087's core (the firmware's runner) needs C++14 with older GCCs (notes/race/probe.sh)
BENCH_LLE_CXX="g++ -std=gnu++14 -include vector -include algorithm -include type_traits -include utility -include stdint.h -include stddef.h -include string.h -include limits.h -O2 -w -Inecdsp_shim -I../../ext/bizhawk-1.9.4/libsnes/bsnes"
MOVIES_DIR="$DECOMP_DIR/tools/bench/movies"

if [ ! -x "$BENCH" ]; then
  echo "Error: bench binary not found or not executable: $BENCH" >&2
  exit 1
fi
if [ ! -f "$ROM_PATH" ]; then
  echo "Error: ROM not found: $ROM_PATH" >&2
  exit 1
fi

extract_chunks() {
  local movie="$1"
  local steps="$2"
  shift 2
  # Remaining args are pairs: <chunk_index> <out_file>

  local pipe
  pipe="$(mktemp -u /tmp/tg3000_wram_pipe.XXXXXX)"
  mkfifo "$pipe"
  trap 'rm -f "$pipe"' RETURN

  python3 - "$pipe" "$@" <<'PY' &
import sys

CHUNK = 131072
pipe_path = sys.argv[1]
args = sys.argv[2:]
targets = {}
for k in range(0, len(args), 2):
    targets[int(args[k])] = args[k + 1]

saved = set()
with open(pipe_path, "rb") as f:
    i = 0
    while True:
        buf = f.read(CHUNK)
        if not buf:
            break
        if len(buf) != CHUNK:
            raise RuntimeError(f"Short read at chunk {i}: {len(buf)} bytes")
        if i in targets:
            with open(targets[i], "wb") as out:
                out.write(buf)
            saved.add(i)
        i += 1

missing = set(targets.keys()) - saved
if missing:
    raise RuntimeError(f"Did not reach target chunks: {sorted(missing)} (total={i})")
PY
  local rpid=$!

  "$BENCH" "$ROM_PATH" "$steps" --movie "$movie" --wram-dump "$pipe" >/dev/null
  wait "$rpid"
}

# WRAM at the first execution of a PC (bank:address as 6 hex digits), from a one-run patch of the
# bench (restored afterwards). Args: movie steps pc out
dump_at_pc() {
  local movie="$1" steps="$2" pc="$3" out="$4"
  if [ -n "$(git -C "$DECOMP_DIR" status --porcelain -- tools/bench)" ]; then
    echo "Error: $DECOMP_DIR/tools/bench has local changes; refusing to patch it" >&2
    exit 1
  fi
  python3 - "$DECOMP_DIR/tools/bench/bench.c" <<'PY'
import sys
p = sys.argv[1]
s = open(p).read()
hook = r'''static void benchTrace(Cpu* cpu) {
  {
    static int done;
    const char* want = getenv("TG3000_DUMP_PC");
    if(!done && want && (((uint32_t)cpu->k << 16) | cpu->pc) == (uint32_t)strtoul(want, NULL, 16)) {
      FILE* f = fopen(getenv("TG3000_DUMP_FILE"), "wb");
      if(f) { fwrite(snes->ram, 1, sizeof snes->ram, f); fclose(f); }
      done = 1;
    }
  }
'''
for old, new in [
    ("static void benchTrace(Cpu* cpu) {\n", hook),
    ("  if(coverageOn || difftestOn || censusOn || hdmaOn || apuOn) cpu_traceHook = benchTrace;\n",
     "  cpu_traceHook = benchTrace;\n"),
]:
    if old not in s:
        raise SystemExit("bench.c has changed; update dump_at_pc in make_reference_memory.sh")
    s = s.replace(old, new, 1)
open(p, "w").write(s)
PY
  rm -f "$out"
  make -C "$DECOMP_DIR/tools/bench" LLE_CXX="$BENCH_LLE_CXX" >/dev/null
  TG3000_DUMP_PC="$pc" TG3000_DUMP_FILE="$out" "$BENCH" "$ROM_PATH" "$steps" --movie "$movie" >/dev/null || true
  git -C "$DECOMP_DIR" checkout -- tools/bench/bench.c
  make -C "$DECOMP_DIR/tools/bench" LLE_CXX="$BENCH_LLE_CXX" >/dev/null
  if [ ! -s "$out" ]; then
    echo "Error: PC $pc was not reached in $steps steps of $movie" >&2
    exit 1
  fi
}

# The race's pacing as the original ran it (needs the DSP-4 firmware, the race's timing depends on
# it), from a one-run patch of the bench (restored afterwards):
#   - race_refreshes.txt: the refresh count each loop step of the first race took, the word the
#     game itself keeps in $1E9E (race_frame_rest, from the NMI count $00BA): read at every loop
#     head ($82:8645) after the first (the first head sees the 1 race_track_start_tail wrote) and
#     at race_end ($82:8776, the last step's). One number per line, step 1 first.
#   - wram_race_wait.bin: WRAM at the race loop's wait ($82:8656) the first time it is entered
#     with $00B8 bit 0 set (the last frame not yet taken) from loop step 300 on.
# Args: movie steps firmware out_dir
record_race_pacing() {
  local movie="$1" steps="$2" fw="$3" out="$4"
  if [ -n "$(git -C "$DECOMP_DIR" status --porcelain -- tools/bench)" ]; then
    echo "Error: $DECOMP_DIR/tools/bench has local changes; refusing to patch it" >&2
    exit 1
  fi
  python3 - "$DECOMP_DIR/tools/bench/bench.c" <<'PY'
import sys
p = sys.argv[1]
s = open(p).read()
hook = r'''static void benchTrace(Cpu* cpu) {
  {
    static FILE* rf;
    static int heads, done, dumped;
    const char* want = getenv("TG3000_RACE_REFRESHES");
    if(!done && want) {
      uint32_t pc = ((uint32_t)cpu->k << 16) | cpu->pc;
      unsigned n = snes->ram[0x1E9E] | snes->ram[0x1E9F] << 8;
      if(pc == 0x828645 || pc == 0x828776) {
        if(!rf) rf = fopen(want, "w");
        if(pc == 0x828776 || heads > 0) fprintf(rf, "%u\n", n);
        if(pc == 0x828645) heads++;
        if(pc == 0x828776) { fclose(rf); done = 1; }
      }
      if(!dumped && pc == 0x828656 && heads >= 300 && (snes->ram[0x00B8] & 1)) {
        FILE* f = fopen(getenv("TG3000_RACE_WAIT_FILE"), "wb");
        if(f) { fwrite(snes->ram, 1, sizeof snes->ram, f); fclose(f); }
        dumped = 1;
      }
    }
  }
'''
for old, new in [
    ("static void benchTrace(Cpu* cpu) {\n", hook),
    ("  if(coverageOn || difftestOn || censusOn || hdmaOn || apuOn) cpu_traceHook = benchTrace;\n",
     "  cpu_traceHook = benchTrace;\n"),
]:
    if old not in s:
        raise SystemExit("bench.c has changed; update record_race_pacing in make_reference_memory.sh")
    s = s.replace(old, new, 1)
open(p, "w").write(s)
PY
  local lle="$BENCH_LLE_CXX"
  rm -f "$out/race_refreshes.raw" "$out/wram_race_wait.bin"
  make -C "$DECOMP_DIR/tools/bench" LLE_CXX="$lle" >/dev/null
  TG3000_RACE_REFRESHES="$out/race_refreshes.raw" TG3000_RACE_WAIT_FILE="$out/wram_race_wait.bin" \
    "$BENCH" "$ROM_PATH" "$steps" --movie "$movie" --dsp4-firmware "$fw" >/dev/null || true
  git -C "$DECOMP_DIR" checkout -- tools/bench/bench.c
  make -C "$DECOMP_DIR/tools/bench" LLE_CXX="$lle" >/dev/null
  if [ ! -s "$out/race_refreshes.raw" ] || [ ! -s "$out/wram_race_wait.bin" ]; then
    echo "Error: the race loop was not reached in $steps steps of $movie" >&2
    exit 1
  fi
  {
    echo "# Refreshes (NMIs) per race loop step, as the original kept them in \$1E9E: $(basename "$movie"),"
    echo "# its first race (track \$00), on the decomp's bench with the DSP-4 firmware."
    echo "# One line per loop step, step 1 first. Made by tools/make_reference_memory.sh."
    cat "$out/race_refreshes.raw"
  } > "$out/race_refreshes.txt"
  rm -f "$out/race_refreshes.raw"
}

# TEST ONLY: the DSP-4 tape (docs/DECISIONS.md, "The DSP-4 tape (tests only)"). One run of the
# first race of the movie on the bench's DEFAULT DSP-4 (its $01/$07/$08 answered by src/dsp4.c, as
# in the port; $09/$06 by the bench's clean-room stand-in, not the console's firmware), with the
# bench's own --dsp4-tape-out (every data-port byte: PC label, value, read/write) and --dsp4-report
# (each PC label's command), and a one-run patch (restored afterwards) for the marks below. Kept:
# the bytes at the PC labels the report gives commands $09 and $06 (the race's C still sends those
# through dsp4_port_write16/read16), from race_wrapper's first instruction to race_end, paired
# into words (low byte first; both bytes of a word have the same label). Out:
#   - race_tape.txt: "W hhhh" / "R hhhh" one port call each, in order; "N S" = a race NMI the bench
#     ran there (bench step S), from loop step 1's head on (test only, --dsp4-tape-nmis)
#   - race_refreshes_tape.txt: $1E9E per loop step (as record_race_pacing), this run's
#   - race_tape_marks.txt: X/N/H/W/R/E lines (wrapper, NMI entry, loop head, first wait entry,
#     rest, race_end) with the bench step, $00B8, loop step, and the raw tape's record count
#   - wram_race_loop.bin: WRAM at the race loop's first head ($82:8645), --screen race_loop's entry
#   - tape_anchors/: wram_take_J.bin = WRAM at the entry of the J-th taking race NMI ($0008 =
#     $83CE, $00B8 bit 0 set; counted from loop step 1 on), the first from the A-th on (A in
#     TAPE_ANCHORS) that falls in the loop's wait, as the port's do (one taken during the head
#     catches the head half done); the bench's pictures of that step and the next
#     (frame_N.png); anchors.txt (J, bench step, loop step)
# Args: movie steps out_dir
TAPE_ANCHORS="25,70,220"
record_race_tape() {
  local movie="$1" steps="$2" out="$3"
  if [ -n "$(git -C "$DECOMP_DIR" status --porcelain -- tools/bench)" ]; then
    echo "Error: $DECOMP_DIR/tools/bench has local changes; refusing to patch it" >&2
    exit 1
  fi
  python3 - "$DECOMP_DIR/tools/bench/bench.c" <<'PY'
import sys
p = sys.argv[1]
s = open(p).read()
hook = r'''static int tapeStep, tapePng;
static void benchTrace(Cpu* cpu) {
  {
    static FILE *mf, *rf;
    static int on, done, heads, takes, waitSeen, nAnchors = -1, anchors[8];
    const uint32_t pc = ((uint32_t)cpu->k << 16) | cpu->pc;
#define TR16(a) (unsigned)(snes->ram[a] | snes->ram[(a) + 1] << 8)
    if(nAnchors < 0) {
      char buf[256];
      snprintf(buf, sizeof buf, "%s", getenv("TG3000_TAPE_ANCHORS"));
      nAnchors = 0;
      for(char* t = strtok(buf, ","); t && nAnchors < 8; t = strtok(NULL, ",")) anchors[nAnchors++] = atoi(t);
      mf = fopen(getenv("TG3000_TAPE_MARKS"), "w");
      rf = fopen(getenv("TG3000_TAPE_REFRESHES"), "w");
    }
    const long rec = dsp4TapeOut ? ftell(dsp4TapeOut) / 5 : -1;
    if(!on && pc == 0x828342) { on = 1; fprintf(mf, "X %d %ld\n", tapeStep, rec); }
    if(on && !done) {
      if(pc == 0x000000) {
        const unsigned tgt = snes->ram[1] | snes->ram[2] << 8 | snes->ram[3] << 16;
        const int take = heads >= 1 && tgt == 0x858377 && TR16(8) == 0x83CE && (TR16(0xB8) & 1);
        if(take) takes++;
        fprintf(mf, "N %d %06X %04X %04X %d %d %ld\n", tapeStep, tgt, TR16(8), TR16(0xB8), heads, take ? takes : 0, rec);
        for(int k = 0; take && waitSeen && k < nAnchors; k++) {  // taken in the loop's wait
          if(anchors[k] < 0 || anchors[k] > takes) continue;
          anchors[k] = -1;
          char name[512];
          snprintf(name, sizeof name, "%s/wram_take_%d.bin", getenv("TG3000_TAPE_DIR"), takes);
          FILE* f = fopen(name, "wb");
          if(f) { fwrite(snes->ram, 1, sizeof snes->ram, f); fclose(f); }
          fprintf(mf, "A %d %d %d\n", takes, tapeStep, heads);
          tapePng = 2;
        }
      } else if(pc == 0x828645) {
        if(heads > 0) fprintf(rf, "%u\n", TR16(0x1E9E));
        heads++;
        waitSeen = 0;
        fprintf(mf, "H %d %d %04X %ld\n", tapeStep, heads, TR16(0xB8), rec);
        if(heads == 1) {  // the race loop's first head: --screen race_loop's entry
          FILE* f = fopen(getenv("TG3000_TAPE_LOOP_FILE"), "wb");
          if(f) { fwrite(snes->ram, 1, sizeof snes->ram, f); fclose(f); }
        }
      } else if(pc == 0x828656 && !waitSeen) {
        waitSeen = 1;
        fprintf(mf, "W %d %d %04X %ld\n", tapeStep, heads, TR16(0xB8), rec);
      } else if(pc == 0x82867B) {
        fprintf(mf, "R %d %d %u %ld\n", tapeStep, heads, TR16(0xBA), rec);
      } else if(pc == 0x828776) {
        fprintf(rf, "%u\n", TR16(0x1E9E));
        fprintf(mf, "E %d %d %ld\n", tapeStep, heads, rec);
        fclose(rf);
        fclose(mf);
        done = 1;
      }
    }
#undef TR16
  }
'''
for old, new in [
    ("static void benchTrace(Cpu* cpu) {\n", hook),
    ("  if(coverageOn || difftestOn || censusOn || hdmaOn || apuOn) cpu_traceHook = benchTrace;\n",
     "  cpu_traceHook = benchTrace;\n"),
    ("  for(int i = 0; i < frames; i++) {\n    applyMovie(i);\n",
     "  for(int i = 0; i < frames; i++) {\n    tapeStep = i;\n    applyMovie(i);\n"),
    ("    if(dumpDir != NULL && (i + 1) % every == 0) dumpFrame(dumpDir);\n",
     "    if(dumpDir != NULL && ((i + 1) % every == 0 || (tapePng > 0 && tapePng--))) dumpFrame(dumpDir);\n"),
]:
    if old not in s:
        raise SystemExit("bench.c has changed; update record_race_tape in make_reference_memory.sh")
    s = s.replace(old, new, 1)
open(p, "w").write(s)
PY
  local tmp
  tmp="$(mktemp -d /tmp/tg3000_tape.XXXXXX)"
  mkdir -p "$out/tape_anchors"
  rm -f "$out"/tape_anchors/wram_take_*.bin "$out"/tape_anchors/frame_*.png
  make -C "$DECOMP_DIR/tools/bench" LLE_CXX="$BENCH_LLE_CXX" >/dev/null
  TG3000_TAPE_ANCHORS="$TAPE_ANCHORS" TG3000_TAPE_DIR="$out/tape_anchors" TG3000_TAPE_MARKS="$tmp/marks.txt" \
    TG3000_TAPE_REFRESHES="$tmp/refreshes.raw" TG3000_TAPE_LOOP_FILE="$out/wram_race_loop.bin" \
    "$BENCH" "$ROM_PATH" "$steps" --movie "$movie" --dsp4-tape-out "$tmp/tape.bin" --dsp4-report "$tmp/report.txt" \
    --dump "$out/tape_anchors" --every 1000000 >/dev/null || true
  git -C "$DECOMP_DIR" checkout -- tools/bench/bench.c
  make -C "$DECOMP_DIR/tools/bench" LLE_CXX="$BENCH_LLE_CXX" >/dev/null
  if ! grep -q '^E ' "$tmp/marks.txt"; then
    echo "Error: race_end was not reached in $steps steps of $movie" >&2
    exit 1
  fi
  python3 - "$tmp" "$out" "$(basename "$movie")" "$(git -C "$DECOMP_DIR" rev-parse --short HEAD)" <<'PY'
import sys
tmp, out, movie, rev = sys.argv[1:5]
OWNED = ("06", "09")  # the commands the race's C still sends through dsp4_port_write16/read16
owned, other = set(), {}
for line in open(f"{tmp}/report.txt"):
    f = line.split()
    if len(f) == 5 and f[0] == "site":
        pc = int(f[3].replace(":", ""), 16)
        if f[1] in OWNED: owned.add(pc)
        elif f[1] != "--": other.setdefault(pc, set()).add(f[1])
shared = sorted(pc for pc in owned if pc in other)
if shared:
    raise SystemExit("tape: PC labels shared with native commands: " + ", ".join(f"{p:06X}" for p in shared))
marks = [l.split() for l in open(f"{tmp}/marks.txt")]
start = int(next(m for m in marks if m[0] == "X")[2])
end = int(next(m for m in marks if m[0] == "E")[3])
# the race NMIs from loop step 1's head on: (record count at its entry, bench step, the record
# counts at its step's head and at its step's rest if the NMI came before that rest, else None)
nmis, head, pending = [], None, []
for m in marks:
    if m[0] == "H":
        head, pending = int(m[4]), []
    elif m[0] == "N" and int(m[5]) >= 1:
        nmis.append([int(m[7]), int(m[1]), head, None])
        pending.append(nmis[-1])
    elif m[0] == "R":
        for n in pending: n[3] = int(m[4])
        pending = []
raw = open(f"{tmp}/tape.bin", "rb").read()
nrec = len(raw) // 5
def rec(i):  # (PC label, value, read): 3 bytes of PC (low first), the byte, 1 = read
    b = raw[5 * i:5 * i + 5]
    return b[0] | b[1] << 8 | b[2] << 16, b[3], b[4]
words, ni, step, prev = [], 0, int(next(m for m in marks if m[0] == "X")[1]), -1
i = start
while i < end:
    pc, val, read = rec(i)
    if pc not in owned:
        i += 1
        continue
    while ni < len(nmis) and nmis[ni][0] <= i:  # the NMIs the bench ran before this call
        step = nmis[ni][1]
        # "P": after its step's last call and before its rest (the port runs it as the call
        # returns; at the next call it would count for the next step); "N": before this call
        late = nmis[ni][3] is not None and prev >= nmis[ni][2] and i >= nmis[ni][3]
        words.append((step, "P" if late else "N", 0))
        ni += 1
    prev = i
    pc2, val2, read2 = rec(i + 1)
    if pc2 != pc or read2 != read:
        raise SystemExit(f"tape: record {i} ({pc:06X}) does not pair with the next")
    words.append((step, "R" if read else "W", val | val2 << 8))
    i += 2
words += [(n[1], "N", 0) for n in nmis[ni:]]  # after the last call, up to race_end
# measure: steps whose kept count ($1E9E) differs from the NMIs between their rest and the last (an
# NMI between the rest's read of $00BA and its clear is counted by no step)
kept = [int(l) for l in open(f"{tmp}/refreshes.raw")]
count, k, uncounted = 0, 0, []
for m in marks:
    if m[0] == "N" and int(m[5]) >= 1: count += 1
    elif m[0] == "R":
        k += 1
        if k >= 2 and count != kept[k - 1]: uncounted.append(k)
        count = 0
# checks: every $09 stream opens with its fixed head; every $06 is one write, 16 reads
w = [(k, v) for _, k, v in words if k in "WR"]
n09 = n06 = 0
for j, (k, v) in enumerate(w):
    if k == "W" and v == 0x0009 and w[j:j + 7] == [("W", x) for x in (9, 0x80, 0xA9, 0, 0, 0x100, 4)]: n09 += 1
    if k == "W" and v == 0x0006 and all(kk == "R" for kk, _ in w[j + 1:j + 17]): n06 += 1
heads = sum(1 for m in marks if m[0] == "H")
with open(f"{out}/race_tape.txt", "w") as f:
    f.write(f"# TEST ONLY (docs/DECISIONS.md, 'The DSP-4 tape (tests only)'): the race's DSP-4 port calls\n"
            f"# ($09/$06; dsp4_port_write16 = W, dsp4_port_read16 = R), recorded on the decomp's bench ({rev},\n"
            f"# default DSP-4: $09/$06 answered by its clean-room stand-in), {movie}'s first race, from\n"
            f"# race_wrapper to race_end. 'N S' / 'P S': the bench ran a race NMI here, in bench step S\n"
            f"# (from loop step 1's head on; P: after its step's last call, before its rest; used only with\n"
            f"# --dsp4-tape-nmis). Made by tools/make_reference_memory.sh.\n"
            f"# {len(w)} calls, {len(words) - len(w)} NMIs, {n09} $09 streams, {n06} $06 loads, {heads} loop steps.\n")
    for step, k, v in words:
        f.write(f"{k} {step}\n" if k in "NP" else f"{k} {v:04X}\n")
with open(f"{out}/race_tape_marks.txt", "w") as f:
    f.write(f"# The DSP-4 tape's run ({movie}, bench {rev}, default DSP-4): X step rec | N step target $0008 $00B8\n"
            f"# loop_step take# rec | H step loop_step $00B8 rec | W (first wait entry) step loop_step $00B8 rec |\n"
            f"# R step loop_step $00BA rec | E step loop_step rec | A take# step loop_step (anchor). rec = raw tape records.\n")
    f.writelines(" ".join(m) + "\n" for m in marks)
with open(f"{out}/race_refreshes_tape.txt", "w") as f:
    f.write(f"# Refreshes (NMIs) per race loop step, as the original kept them in $1E9E: {movie},\n"
            f"# its first race (track $00), on the decomp's bench ({rev}) with its DEFAULT DSP-4 (the DSP-4 tape's run).\n"
            f"# One line per loop step, step 1 first. Made by tools/make_reference_memory.sh.\n")
    f.write(open(f"{tmp}/refreshes.raw").read())
with open(f"{out}/tape_anchors/anchors.txt", "w") as f:
    f.write("# take# bench_step loop_step: WRAM at the entry of the take#-th taking race NMI (wram_take_N.bin)\n")
    f.writelines(" ".join(m[1:]) + "\n" for m in marks if m[0] == "A")
print(f"race tape: {len(w)} calls, {len(words) - len(w)} NMIs ({n09} $09 streams, {n06} $06 loads), {heads} loop steps, "
      f"records {start}-{end} of {nrec}; steps whose NMIs between rests differ from their count: "
      f"{len(uncounted)} (first {uncounted[:3]})")
PY
  rm -rf "$tmp"
}

dump_at_pc "$MOVIES_DIR/autopilot_60k.txt" 7904 828342 "$OUT_DIR/wram_before_race.bin"
record_race_tape "$MOVIES_DIR/autopilot_60k.txt" 20000 "$OUT_DIR"

if [ -n "${DSP4_FIRMWARE:-}" ]; then
  record_race_pacing "$MOVIES_DIR/autopilot_60k.txt" 16262 "$(cd "$(dirname "$DSP4_FIRMWARE")" && pwd)/$(basename "$DSP4_FIRMWARE")" "$OUT_DIR"
else
  echo "Note: DSP4_FIRMWARE not set; race_refreshes.txt and wram_race_wait.bin not made" >&2
fi

extract_chunks "$MOVIES_DIR/autopilot_60k.txt" 41071 \
  15857 "$OUT_DIR/wram_before_tick.bin" \
  16489 "$OUT_DIR/wram_before_res.bin" \
  16662 "$OUT_DIR/wram_before_title_screen.bin" \
  41070 "$OUT_DIR/wram_before_pw.bin"

extract_chunks "$MOVIES_DIR/explore_options.txt" 2956 \
  367 "$OUT_DIR/wram_before_title.bin" \
  2955 "$OUT_DIR/wram_before_opt.bin"

echo "Generated reference memories in $OUT_DIR:"
ls -la "$OUT_DIR/wram_before_race.bin" "$OUT_DIR/wram_before_tick.bin" "$OUT_DIR/wram_before_res.bin" "$OUT_DIR/wram_before_title_screen.bin" "$OUT_DIR/wram_before_pw.bin" "$OUT_DIR/wram_before_opt.bin" "$OUT_DIR/wram_before_title.bin"
