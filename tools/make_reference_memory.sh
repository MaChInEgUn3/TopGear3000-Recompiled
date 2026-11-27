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
#
# WARNING: The generated .bin files contain game memory data and MUST stay out of git.
#
# Usage:
#   tools/make_reference_memory.sh <tg3000-decomp-dir> <rom-path> <output-dir>
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
  make -C "$DECOMP_DIR/tools/bench" >/dev/null
  TG3000_DUMP_PC="$pc" TG3000_DUMP_FILE="$out" "$BENCH" "$ROM_PATH" "$steps" --movie "$movie" >/dev/null || true
  git -C "$DECOMP_DIR" checkout -- tools/bench/bench.c
  make -C "$DECOMP_DIR/tools/bench" >/dev/null
  if [ ! -s "$out" ]; then
    echo "Error: PC $pc was not reached in $steps steps of $movie" >&2
    exit 1
  fi
}

dump_at_pc "$MOVIES_DIR/autopilot_60k.txt" 7904 828342 "$OUT_DIR/wram_before_race.bin"

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
