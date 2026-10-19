#!/usr/bin/env bash
# Progressive preview timing (no OS window). First useful = first advance ≤16 spp.
# Usage: run_preview_bench.sh <farmc> [out-dir]
set -euo pipefail
FARMC="${1:?farmc path}"
ROOT="$(cd "$(dirname "$BASH_SOURCE[0]")/../../../.." && pwd)"
OUT="${2:-$(pwd)/m10_preview_bench}"
mkdir -p "$OUT"
FARMC="$(cd "$(dirname "$FARMC")" && pwd)/$(basename "$FARMC")"
if [ -z "${FARM_RUNTIME:-}" ] && [ -f "$(dirname "$FARMC")/runtime/farm_rt.c" ]; then
  export FARM_RUNTIME="$(dirname "$FARMC")/runtime"
fi
if [ -z "${FARMOS_THREADS:-}" ]; then
  export FARMOS_THREADS="$(nproc 2>/dev/null || echo 1)"
fi

cat >"$OUT/preview.fm" <<'FM'
import { Scene, PerspectiveCamera, Renderer,
         PATH_BACKEND_VULKAN, PATH_BACKEND_VULKAN_RT } from "farmos:scene";
import { Color } from "farmos:math";

function main(): int {
  const scene: Scene = new Scene();
  scene.setBackground(new Color(0x4488ff));
  const camera: PerspectiveCamera = new PerspectiveCamera(60, 1920.0 / 1080.0, 0.1, 100);
  camera.position.z = 5;
  const renderer: Renderer = new Renderer(1920, 1080);
  renderer.setSamples(16);
  renderer.setMaxBounces(4);
  if (renderer.isVulkanRayTracingAvailable()) {
    renderer.setPathBackend(PATH_BACKEND_VULKAN_RT);
    println("vulkan_rt");
  } else if (renderer.isVulkanPathAvailable()) {
    renderer.setPathBackend(PATH_BACKEND_VULKAN);
    println("vulkan");
  } else {
    println("cpu");
  }
  renderer.beginPathPreview(scene, camera);
  renderer.advancePathPreview(8);
  renderer.savePNG("preview_first.png");
  println(renderer.getPathPreviewSpp());
  renderer.advancePathPreview(8);
  renderer.savePNG("preview_step.png");
  println(renderer.getPathPreviewSpp());
  renderer.finishPathPreview();
  return 0;
}
FM

echo "host=$(uname -n)  $(uname -srm)"
echo "farmc=$FARMC threads=$FARMOS_THREADS"
"$FARMC" build "$OUT/preview.fm" -o "$OUT/preview"
echo "warmup..."
(cd "$OUT" && ./preview >"$OUT/warmup.stdout")
echo "NOTE: AC-M10-06 first useful ≤250 ms / step ≤100 ms is TommyLaptop HW RT; time per-advance there."
echo "Linux CPU progressive is API correctness only."
