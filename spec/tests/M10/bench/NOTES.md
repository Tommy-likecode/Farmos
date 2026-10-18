# M10 denoise / progressive bench notes

**Status: FINAL gates; second speed tip pending TommyLaptop CLEAN re-bench.**
Do not invent Windows timings. Cite M9 undenoised floor only as baseline.

Linux cloud agent (this PR): `ctest -L spec` after this tip. See `SIZE.md`.

## Floor on `b8a36a0` (TommyLaptop DIRTY_DELTAFORCE — not gospel)

- Stills 16 spp + denoise HW RT median **9118 ms** (OFF control **8161**; denoise tax ~1 s)
- Progressive first **1273** / step **1281** ms; begin-warm ~5569 ms
- PE PASS. Must not regress those medians.

## Speed tip 2 (new levers — not A-trous restack)

`b8a36a0` first-advance ≈ step at ¼-res/8 spp, so path/host after persist
dominates. Stills 16 spp OFF ~8 s is process-level path/RT (device + AS +
dispatch), not denoise.

This pass (one coherent bundle):

1. **Disk PSO cache** (`TEMP/farmos_vk_rt_pso_<spv>.bin`) so the warm-up
   process fills a pipeline cache the timed processes reuse.
2. **AS `PREFER_FAST_BUILD`** (was FAST_TRACE) — stills/begin are
   build-once-per-process.
3. **Device-local + host-visible** SSBOs when the type exists (Arc iGPU
   unified); fallback unchanged.
4. **Stills bench 8 spp** (spec ≤64). Artwork geometry/materials unchanged.
5. **Preview 1/16 res** (`preview_div=16` when ≥640×360), **skip denoise**,
   **≤3 bounces + M4 sampling** in the GPU params only, **framebuffer
   nearest-upsample** without 1080p float64 accum. Advance stays 8 spp class.
6. **Stronger skip-collect** (persist + size; no scene-pointer requirement)
   and skip geometry SSBO memcpy on AS reuse.

Do not restack the shipped A-trous. Default denoise OFF. No OQ.

Paper-class vs floor (not a CLEAN claim):

- Progressive: 1/16 vs 1/4 is 16× fewer pixels; plus cheaper preview
  shader/resolve. If 1273 was GPU-bound at ¼-res → ~80 ms class (under
  250/100). If it was 1080p host resolve → cheap upsample should drop it
  under 500/200.
- Stills: 8 vs 16 spp cuts GPU ~2×; PSO cache + FAST_BUILD target
  process-start. If init+AS still dominate (~5 s dirty begin), stills may
  stay **>3000 ms** (under the 9118 floor). poteto measures; hard-stop if
  >2× over the new floor.

## Stills denoise (OQ-M10-03 A)

- Scene: `examples/artwork_silver_still.fm` (composition locked)
- 1920×1080 / **8 spp** (≤64) / `PATH_DENOISE_SPATIAL` / prefer `PATH_BACKEND_VULKAN_RT`
- Gate: median ≤ **1500 ms** after 1 warm-up on TommyLaptop
- Visual: `# png_tol: 8` vs 256-spp undenoised reference
- Undenoised baseline (M9): `eaeb0c6` / OQ-M9-08 class **4881 ms** RT median

## Progressive interactive (OQ-M10-04 A)

- First useful frame ≤ **250 ms** (≤8–16 spp class) — time first `advancePathPreview`
- Subsequent step ≤ **100 ms** median
- No OS window / swapchain (OQ-M10-01 A)
- Implementation: 1/16-res preview dispatch; AS prepare on `beginPathPreview`

## Soft-skip

Linux CI / hosts without HW RT: soft-skip GPU/RT wall-clock rows; API fixtures
still run on CPU where applicable.
