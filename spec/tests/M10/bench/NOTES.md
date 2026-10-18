# M10 denoise / progressive bench notes

**Status: FINAL gates; speed tip pending TommyLaptop CLEAN re-bench.** Do not
invent Windows timings. Cite M9 undenoised floor only as baseline context.

Linux cloud agent (this PR): `ctest -L spec` after the speed tip. `m9_006_rt_png_tol`
soft-skipped (no Vulkan RT device). Size script must stay PASS. See `SIZE.md`.

## Speed tip (one lever pass)

ffdabf3 CLEAN FAIL on TommyLaptop (PE PASS, wall-clock FAIL): stills 64+denoise
median **9508 ms** (CPU 5×5×5 A-trous ≈ **+5.2 s** vs 4295 ms OFF); progressive
advance(8) first **2969 ms** / step **1192 ms**.

This tip (single coherent pass; do not stack more levers if stills stay
**>3000 ms** or progressive first **>500** / step **>200** — PM decides continue
vs relax-AC; do not open OQ):

1. **Kill CPU denoise tax:** Vulkan compute A-trous (3 iters, 16×16) on the RT
   path when `FARM_ENABLE_DENOISE` is linked. `g_ssbo[5]` stays noisy sums;
   framebuffer is the filtered means. CPU fallback is a 2-iter plus-kernel
   (5 taps), not 5×5×5.
2. **Stills:** bench **16 spp** (spec ≤64) + `PATH_DENOISE_SPATIAL`. Artwork
   geometry/materials unchanged.
3. **Progressive:** `beginPathPreview` warms AS/persist at 1 spp then
   `resetAccumulation` (preview spp stays 0). Dispatch at **¼ res** when
   ≥640×360 (`preview_div=4`); nearest-upsample to the framebuffer. First
   `advancePathPreview` should be reuse + quarter-res.

PE/DCE: denoise SPIR-V is compiled into `farm_vk_rt.c` only with
`FARM_ENABLE_DENOISE` (RT-only and hello/glass/compute budgets unchanged).

## Stills denoise (OQ-M10-03 A)

- Scene: `examples/artwork_silver_still.fm` (composition locked)
- 1920×1080 / **16 spp** (≤64) / `PATH_DENOISE_SPATIAL` / prefer `PATH_BACKEND_VULKAN_RT`
- Gate: median ≤ **1500 ms** after 1 warm-up on TommyLaptop
- Visual: `# png_tol: 8` vs 256-spp undenoised reference
- Undenoised baseline (M9): `eaeb0c6` / OQ-M9-08 class **4881 ms** RT median

## Progressive interactive (OQ-M10-04 A)

- First useful frame ≤ **250 ms** (≤8–16 spp class) — time first `advancePathPreview`
- Subsequent step ≤ **100 ms** median
- No OS window / swapchain (OQ-M10-01 A)
- Implementation: quarter-res preview dispatch; AS prepare on `beginPathPreview`

## Soft-skip

Linux CI / hosts without HW RT: soft-skip GPU/RT wall-clock rows; API fixtures
still run on CPU where applicable.
