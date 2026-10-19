#!/usr/bin/env bash
# Linux twin of scripts/render_artwork_silver_orbit.ps1.
# TommyLaptop should use the PowerShell script with build/Release/farmc.exe.
#
#   bash scripts/render_artwork_silver_orbit.sh [farmc]
#   FARMOS_START=0 FARMOS_FRAMES=72 SKIP_ENCODE=0 bash scripts/render_artwork_silver_orbit.sh build/farmc
set -eu
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
FARMC="${1:-}"
if [ -z "$FARMC" ]; then
  if [ -x "$ROOT/build/farmc" ]; then
    FARMC="$ROOT/build/farmc"
  elif [ -x "$ROOT/build/Release/farmc.exe" ]; then
    FARMC="$ROOT/build/Release/farmc.exe"
  else
    echo "farmc not found; pass path as argv[1]" >&2
    exit 1
  fi
fi
if [ -z "${FARM_RUNTIME:-}" ]; then
  if [ -f "$(dirname "$FARMC")/runtime/farm_rt.c" ]; then
    export FARM_RUNTIME="$(cd "$(dirname "$FARMC")/runtime" && pwd)"
  elif [ -f "$ROOT/runtime/farm_rt.c" ]; then
    export FARM_RUNTIME="$ROOT/runtime"
  fi
fi
SRC="$ROOT/examples/artwork_silver_orbit.fm"
OUTDIR="$ROOT/out/artwork_silver_orbit"
EXE="$ROOT/out/artwork_silver_orbit.bin"
MP4="$ROOT/out/artwork_silver_orbit.mp4"
START="${FARMOS_START:-0}"
FRAMES="${FARMOS_FRAMES:-72}"
mkdir -p "$OUTDIR"
if [ "${SKIP_BUILD:-0}" != "1" ]; then
  echo "farmc=$FARMC"
  echo "building $SRC"
  "$FARMC" build "$SRC" -o "$EXE"
fi
end=$((START + FRAMES - 1))
echo "frames $START..$end  cwd=$OUTDIR"
i="$START"
while [ "$i" -le "$end" ]; do
  printf "frame %04d\n" "$i"
  (cd "$OUTDIR" && FARMOS_FRAME="$i" "$EXE")
  png=$(printf "%s/frame_%04d.png" "$OUTDIR" "$i")
  if [ ! -f "$png" ]; then
    echo "missing $png" >&2
    exit 1
  fi
  i=$((i + 1))
done
if [ "${SKIP_ENCODE:-0}" = "1" ]; then
  echo "skip encode  frames in $OUTDIR"
  exit 0
fi
if ! command -v ffmpeg >/dev/null 2>&1; then
  echo "ffmpeg not found" >&2
  exit 1
fi
echo "ffmpeg $OUTDIR/frame_%04d.png -> $MP4"
ffmpeg -y -framerate 24 -i "$OUTDIR/frame_%04d.png" -c:v libx264 -pix_fmt yuv420p -r 24 "$MP4"
echo "wrote $MP4"
