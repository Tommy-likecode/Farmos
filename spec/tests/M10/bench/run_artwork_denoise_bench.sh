#!/usr/bin/env bash
# Time a 64-spp + PATH_DENOISE_SPATIAL still derived from artwork_silver_still.fm.
# Does not edit examples/artwork_silver_still.fm (composition locked).
# Usage: run_artwork_denoise_bench.sh <farmc> [out-dir]
set -euo pipefail
FARMC="${1:?farmc path}"
ROOT="$(cd "$(dirname "$BASH_SOURCE[0]")/../../../.." && pwd)"
OUT="${2:-$(pwd)/m10_artwork_denoise_bench}"
SRC="$ROOT/examples/artwork_silver_still.fm"
mkdir -p "$OUT"
FARMC="$(cd "$(dirname "$FARMC")" && pwd)/$(basename "$FARMC")"
if [ -z "${FARM_RUNTIME:-}" ] && [ -f "$(dirname "$FARMC")/runtime/farm_rt.c" ]; then
  export FARM_RUNTIME="$(dirname "$FARMC")/runtime"
fi
if [ -z "${FARMOS_THREADS:-}" ]; then
  export FARMOS_THREADS="$(nproc 2>/dev/null || echo 1)"
fi

# Rewrite only samples + denoise opt-in. Geometry/materials stay byte-identical.
python3 - "$SRC" "$OUT/still_denoise.fm" <<'PY'
import sys
src, dst = sys.argv[1], sys.argv[2]
text = open(src, encoding="utf-8").read()
text = text.replace(
    "RectAreaLight, Renderer, PATH_BACKEND_VULKAN, PATH_BACKEND_VULKAN_RT } from \"farmos:scene\";",
    "RectAreaLight, Renderer, PATH_BACKEND_VULKAN, PATH_BACKEND_VULKAN_RT, PATH_DENOISE_SPATIAL } from \"farmos:scene\";",
)
text = text.replace("renderer.setSamples(256);", "renderer.setSamples(64);\n  renderer.setPathDenoise(PATH_DENOISE_SPATIAL);")
text = text.replace("println(256);", "println(64);")
open(dst, "w", encoding="utf-8").write(text)
PY

echo "host=$(uname -n)  $(uname -srm)"
echo "farmc=$FARMC"
echo "threads=$FARMOS_THREADS"
echo "spp=64 denoise=PATH_DENOISE_SPATIAL"
echo "building..."
"$FARMC" build "$OUT/still_denoise.fm" -o "$OUT/artwork_denoise"
echo "warmup..."
t0=$(date +%s%N)
(cd "$OUT" && ./artwork_denoise >"$OUT/warmup.stdout")
t1=$(date +%s%N)
warmup_ms=$(( (t1 - t0) / 1000000 ))
echo "  warmup ${warmup_ms} ms  stdout=$(head -n1 "$OUT/warmup.stdout")"
times=()
for i in 1 2 3; do
  echo "timed run $i..."
  t0=$(date +%s%N)
  (cd "$OUT" && ./artwork_denoise >"$OUT/run$i.stdout")
  t1=$(date +%s%N)
  ms=$(( (t1 - t0) / 1000000 ))
  times+=("$ms")
  echo "  ${ms} ms  stdout=$(head -n1 "$OUT/run$i.stdout")"
done
IFS=$'\n' sorted=($(sort -n <<<"${times[*]}")); unset IFS
median="${sorted[1]}"
echo "median_ms=$median  runs=${times[*]}  warmup_ms=$warmup_ms"
echo "png=$OUT/artwork_silver_still.png"
echo "$median" > "$OUT/median_ms.txt"
echo "NOTE: AC-M10-05 (≤1500 ms @ ≤64 spp + denoise) is a TommyLaptop HW RT gate; Linux results are not that AC."
echo "M9 undenoised 256-spp floor to beat: 4881 ms (eaeb0c6)."
