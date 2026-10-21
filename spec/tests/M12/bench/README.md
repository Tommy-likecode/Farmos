# M12 Arc bench (TommyLaptop)

Prefer `PATH_BACKEND_VULKAN_RT` on Intel Arc.

| Lane | Setup |
|------|--------|
| Native | 1920×1080, `PATH_UPSCALE_OFF`, documented spp, same backend |
| Upscale | render 960×540 → display 1920×1080, `PATH_UPSCALE_XESS`, same spp at render-res |

Record median wall-clock after warm-up (≥8 frames). Visual: `# png_tol: 12` and/or
silver ball-center ROI. Do **not** alter silver-still composition.

AC-M12-04 / AC-M12-05 are eggtooth / TommyLaptop gates. Linux without SDK does
not run this bench.
