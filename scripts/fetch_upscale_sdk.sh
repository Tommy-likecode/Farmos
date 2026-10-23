#!/usr/bin/env bash
# Place official XeSS / FSR2 shared libraries next to the Farmos runtime.
# Vendor blobs stay EXTERNAL (OQ-M12-07 A) and are not counted in the PE cap.
#
# This script does not download proprietary redistributables by default.
# Intel XeSS headers are MIT (https://github.com/intel/xess); ship the matching
# libxess shared object / DLL from the vendor SDK per that license.
# AMD FidelityFX FSR2 follows AMD's SDK license — copy only what you may
# redistribute. Do NOT fetch DLSS-Enabler / Swapper / OptiScaler / NGX.
#
# Layout (also searched at runtime):
#   $FARM_RUNTIME/third_party/xess/libxess.dll   (Windows)
#   $FARM_RUNTIME/third_party/xess/libxess.so    (Linux)
#   $FARM_RUNTIME/third_party/fsr/ffx_fsr2_vk.dll
#   $FARM_RUNTIME/third_party/fsr/libffx_fsr2_vk.so
#
# Overrides: FARMOS_XESS_LIB, FARMOS_FSR_LIB (absolute paths).
# Optional: XESS_SDK_ROOT / FSR_SDK_ROOT — copy from an already-extracted SDK.
set -euo pipefail
ROOT="$(cd "$(dirname "$BASH_SOURCE[0]")/.." && pwd)"
RT="${FARM_RUNTIME:-$ROOT/runtime}"
XESS_DST="$RT/third_party/xess"
FSR_DST="$RT/third_party/fsr"
mkdir -p "$XESS_DST" "$FSR_DST"

copy_if() {
  local src="$1" dst="$2"
  if [[ -f "$src" ]]; then
    cp -f "$src" "$dst"
    echo "copied $src -> $dst"
    return 0
  fi
  return 1
}

copied=0
if [[ -n "${XESS_SDK_ROOT:-}" ]]; then
  for n in libxess.dll libxess.so libxess.so.2 libxess_vk.dll; do
    if copy_if "$XESS_SDK_ROOT/$n" "$XESS_DST/$n" || \
       copy_if "$XESS_SDK_ROOT/bin/$n" "$XESS_DST/$n" || \
       copy_if "$XESS_SDK_ROOT/lib/$n" "$XESS_DST/$n"; then
      copied=1
    fi
  done
fi
if [[ -n "${FSR_SDK_ROOT:-}" ]]; then
  for n in ffx_fsr2_vk.dll libffx_fsr2_vk.so libffx_fsr2_api_vk.so; do
    if copy_if "$FSR_SDK_ROOT/$n" "$FSR_DST/$n" || \
       copy_if "$FSR_SDK_ROOT/bin/$n" "$FSR_DST/$n" || \
       copy_if "$FSR_SDK_ROOT/lib/$n" "$FSR_DST/$n"; then
      copied=1
    fi
  done
fi

echo "XeSS dest: $XESS_DST"
echo "FSR dest:  $FSR_DST"
if [[ "$copied" -eq 0 ]]; then
  echo "No SDK files copied. Extract the vendor SDK and rerun with XESS_SDK_ROOT=... and/or FSR_SDK_ROOT=..."
  echo "Linux CI without SDK is expected to soft-skip # requires_xess / # requires_fsr."
fi
exit 0
