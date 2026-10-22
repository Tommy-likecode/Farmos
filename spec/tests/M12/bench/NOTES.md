# M12 Arc notes

CLAIM recommendation: **FAIL Arc** at tip `3edb3f0` (timing + png_tol); **PENDING** poteto re-smoke on TommyLaptop after `fix-fill-aux-vk`.

| Item | Status |
|------|--------|
| XeSS SDK | Official 2.1.1 on TommyLaptop (`FARMOS_XESS_LIB`). Linux CI has no SDK. |
| Headless VkImage | Persistent color/depth/MV/output images (no swapchain); prefer path-tracer device |
| Depth / MV | Path-tracer first-hit `t` + cheap camera MV (static skip). CPU `fill_aux` ray×tris **removed**. |
| AC-M12-04 quality | Prior FAIL png_tol:12 (max_abs 206); silver ROI L ~191 PASS. Re-smoke pending. |
| AC-M12-05 timing | Prior FAIL ratio 0.330. Binding: XeSS wall strictly < native. SHOULD ≥1.5×. Re-smoke pending. |
| AC-M12-07 Windows PE | **PASS** at `9b4a11f` (B-11 73216 / 716800). Re-verify on laptop if glue grows. |
| FSR2 optional | selectable; execute soft-fails until official ffx dispatch symbols resolve |
| Enabler / Swapper / OptiScaler | **OUT** — not used. |
| Arc helpers | `ac04_*.fm` / `ac05_*.fm` / `run_arc_ac.py` with `PATH_GLOSSY_VNDF`, 64 spp, denoise spatial, fixed positions |

Constructor size is the path-trace resolution. For 960×540→1080p: `new Renderer(960, 540)` then `setPathUpscaleResolution(960, 540, 1920, 1080)`.

Remaining Arc risks: XeSS rejecting the borrowed RT device (fallback creates a second device); first-frame `xessVKInit` cold-start on AC-05 process-per-frame; png_tol outliers independent of the timing tax.
