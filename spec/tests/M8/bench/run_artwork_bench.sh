#!/usr/bin/env bash
# Time examples/artwork_silver_still.fm (1920x1080 / 256 spp).
# Usage: run_artwork_bench.sh <farmc> [out-dir]
# Env: FARMOS_PATH_ROULETTE, FARMOS_PATH_SAMPLING, FARMOS_THREADS, FARMOS_VULKAN_FORCE_FAIL
set -euo pipefail
FARMC="${1:?farmc path}"
ROOT="$(cd "$(dirname "$BASH_SOURCE[0]")/../../../.." && pwd)"
OUT="${2:-$(pwd)/m8_artwork_bench}"
SRC="$ROOT/examples/artwork_silver_still.fm"
mkdir -p "$OUT"
FARMC="$(cd "$(dirname "$FARMC")" && pwd)/$(basename "$FARMC")"
if [ -z "${FARM_RUNTIME:-}" ] && [ -f "$(dirname "$FARMC")/runtime/farm_rt.c" ]; then
  export FARM_RUNTIME="$(dirname "$FARMC")/runtime"
fi
if [ -z "${FARMOS_THREADS:-}" ]; then
  export FARMOS_THREADS="$(nproc 2>/dev/null || echo 1)"
fi
echo "host=$(uname -n)  $(uname -srm)"
echo "farmc=$FARMC"
echo "threads=$FARMOS_THREADS"
echo "roulette=${FARMOS_PATH_ROULETTE:-0}  sampling=${FARMOS_PATH_SAMPLING:-0}"
echo "building..."
"$FARMC" build "$SRC" -o "$OUT/artwork"
echo "warmup..."
(cd "$OUT" && ./artwork >/dev/null)
times=()
for i in 1 2 3; do
  echo "timed run $i..."
  t0=$(date +%s%N)
  (cd "$OUT" && ./artwork >"$OUT/run$i.stdout")
  t1=$(date +%s%N)
  ms=$(( (t1 - t0) / 1000000 ))
  times+=("$ms")
  echo "  ${ms} ms  stdout=$(head -n1 "$OUT/run$i.stdout")"
done
IFS=$'\n' sorted=($(sort -n <<<"${times[*]}")); unset IFS
median="${sorted[1]}"
echo "median_ms=$median  runs=${times[*]}"
echo "png=$OUT/artwork_silver_still.png"
echo "$median" > "$OUT/median_ms.txt"
echo "host=$(uname -n) threads=$FARMOS_THREADS roulette=${FARMOS_PATH_ROULETTE:-0} sampling=${FARMOS_PATH_SAMPLING:-0} median_ms=$median runs=${times[*]}" >> "$OUT/log.txt"
