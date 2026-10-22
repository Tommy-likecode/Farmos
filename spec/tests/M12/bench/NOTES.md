# M12 Arc notes

CLAIM recommendation: **PASS** at tip `edeb845` under FINAL OQ-M12-10 A + OQ-M12-11 A.

| Item | Status |
|------|--------|
| XeSS SDK | Official 2.1.1 on TommyLaptop (`FARMOS_XESS_LIB`). Linux CI has no SDK. |
| Headless VkImage | Persistent color/depth/MV/output images (no swapchain); prefer path-tracer device |
| Depth / MV | Path-tracer first-hit `t` + cheap camera MV (static skip). CPU `fill_aux` ray×tris **removed**. |
| AC-M12-04 quality | **PASS** primary: ROI L **105.810 / 105.805** ∈ [80,140], \|ΔL\| **0.005** ≤ 5.0 (OQ-M12-11 A). mean_abs **0.465** report. max_abs **211** **report-only**. Historical hard `# png_tol: 12` **superseded**. |
| AC-M12-05 timing | **PASS**: native **4.302 s** / XeSS **3.477 s** → ratio **1.237**. Binding XeSS wall < native. SHOULD ≥ **1.2×** (OQ-M12-10 A). Historical SHOULD ≥ **1.5×** **superseded**. |
| AC-M12-07 Windows PE | **PASS** at `edeb845` (B-11 **72704** / 716800). |
| FSR2 optional | selectable; execute soft-fails until official ffx dispatch symbols resolve |
| Enabler / Swapper / OptiScaler | **OUT** — not used. |
| Arc helpers | `ac04_*.fm` / `ac05_*.fm` / `run_arc_ac.py` with `PATH_GLOSSY_VNDF`, 64 spp, denoise spatial, fixed positions |

Constructor size is the path-trace resolution. For 960×540→1080p: `new Renderer(960, 540)` then `setPathUpscaleResolution(960, 540, 1920, 1080)`.
