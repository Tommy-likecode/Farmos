# M10 denoise / progressive bench notes

**Status: FINAL gates; CLEAN TommyLaptop numbers pending.** Do not invent
Windows timings. Cite M9 undenoised floor only as baseline context.

Linux cloud agent (this PR): `ctest -L spec` **354/354 PASS** (M10 6/6).
`m9_006_rt_png_tol` soft-skipped (no Vulkan RT device). Size script PASS; ELF
denoise-linked **47456 B** (budget 600 KiB). See `SIZE.md`.

## Stills denoise (OQ-M10-03 A)

- Scene: `examples/artwork_silver_still.fm` (composition locked)
- 1920×1080 / ≤ **64 spp** / `PATH_DENOISE_SPATIAL` / prefer `PATH_BACKEND_VULKAN_RT`
- Gate: median ≤ **1500 ms** after 1 warm-up on TommyLaptop
- Visual: `# png_tol: 8` vs 256-spp undenoised reference
- Undenoised baseline (M9): `eaeb0c6` / OQ-M9-08 class **4881 ms** RT median

## Progressive interactive (OQ-M10-04 A)

- First useful frame ≤ **250 ms** (≤8–16 spp class)
- Subsequent step ≤ **100 ms** median
- No OS window / swapchain (OQ-M10-01 A)

## Soft-skip

Linux CI / hosts without HW RT: soft-skip GPU/RT wall-clock rows; API fixtures
still run on CPU where applicable.
