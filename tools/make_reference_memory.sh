#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-late
#
# Regenerate the four local 131072-byte WRAM snapshots used by the ctest picture tests:
#   - wram_before_res.bin   (notes/res/wram_before_res.bin)   -> game_picture_results
#       autopilot_60k.txt, WRAM at the end of step 16489
#   - wram_before_pw.bin    (notes/pw/wram_before_pw.bin)    -> game_picture_password_show
#       autopilot_60k.txt, WRAM at the end of step 41070
#   - wram_before_opt.bin   (notes/opt/wram_before_opt.bin)  -> game_picture_options
#       explore_options.txt, WRAM at the end of step 2955
#   - wram_before_title.bin (notes/opt/wram_before_title.bin)-> game_picture_vs
#       explore_options.txt, WRAM at the end of step 367
#
# WARNING: The generated .bin files contain game memory data and MUST stay out of git.
#
# Usage:
#   tools/make_reference_memory.sh <tg3000-decomp-dir> <rom-path> <output-dir>

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

extract_chunks "$MOVIES_DIR/autopilot_60k.txt" 41071 \
  16489 "$OUT_DIR/wram_before_res.bin" \
  41070 "$OUT_DIR/wram_before_pw.bin"

extract_chunks "$MOVIES_DIR/explore_options.txt" 2956 \
  367 "$OUT_DIR/wram_before_title.bin" \
  2955 "$OUT_DIR/wram_before_opt.bin"

echo "Generated reference memories in $OUT_DIR:"
ls -la "$OUT_DIR/wram_before_res.bin" "$OUT_DIR/wram_before_pw.bin" "$OUT_DIR/wram_before_opt.bin" "$OUT_DIR/wram_before_title.bin"
