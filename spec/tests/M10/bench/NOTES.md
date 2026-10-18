# M10 denoise / progressive bench notes

**Status: DRAFT.** No TommyLaptop numbers yet — do not invent Windows timings.
Cite M9 undenoised floor only as baseline context.

## Stills denoise (OQ-M10-03 A provisional)

- Scene: `examples/artwork_silver_still.fm` (composition locked)
- 1920×1080 / ≤ **64 spp** / `PATH_DENOISE_SPATIAL` / prefer `PATH_BACKEND_VULKAN_RT`
- Gate: median ≤ **1500 ms** after 1 warm-up on TommyLaptop
- Visual: `# png_tol: 8` vs 256-spp undenoised reference
- Undenoised baseline (M9): `eaeb0c6` / OQ-M9-08 class **4881 ms** RT median

## Progressive interactive (OQ-M10-04 A provisional)

- First useful frame ≤ **250 ms** (≤8–16 spp class)
- Subsequent step ≤ **100 ms** median
- No OS window / swapchain (OQ-M10-01 A)

## Soft-skip

Linux CI / hosts without HW RT: soft-skip GPU/RT wall-clock rows; API fixtures
still run on CPU where applicable.
