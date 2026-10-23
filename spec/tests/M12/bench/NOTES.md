# M12 Arc notes

CLAIM recommendation: **PASS** at docs `7f0a080` / product `fd31e46` under FINAL OQ-M12-12 A + OQ-M12-13 A. Glossy protocol, clean host. **No merge** until eggtooth AUDIT PASS via PM.

| Item | Status |
|------|--------|
| XeSS SDK | Official 2.1.1 on TommyLaptop (`FARMOS_XESS_LIB`). Linux CI has no SDK. |
| Headless VkImage | Persistent color/depth/MV/output images (no swapchain); prefer path-tracer device |
| Depth / MV | Path-tracer first-hit `t` + cheap camera MV (static skip). CPU `fill_aux` ray×tris **removed**. |
| more-optimize | Restore official `xessVKGetRequired*` extras. CreateContext+init during path GPU wait. File-present availability + skip-rewrite of warm PSO. |
| AC-M12-04 quality | **PASS** (OQ-M12-13 A): ROI L **187.228 / 187.877** ∈ [80,200], \|ΔL\| **3.932** ≤ 5.0. mean_abs **0.467** report. max_abs **212** report-only. Historical [80,140] **superseded**. |
| AC-M12-05 timing | **PASS** (OQ-M12-12 A): Pass1 **1.052**, Pass2 **1.162**. Binding XeSS < native both. SHOULD ≥ **1.05×**. Historical ≥1.2× **superseded**. Host clean (chrome/msedge absent). |
| AC-M12-07 | Windows PE B-11 **72704** / 716800. Linux ELF B-11 **67792**. |
| FSR2 optional | selectable; execute soft-fails until official ffx dispatch symbols resolve |
| Enabler / Swapper / OptiScaler | **OUT** — not used. |
| Arc helpers | `ac04_*.fm` / `ac05_*.fm` / `run_arc_ac.py` with `PATH_GLOSSY_VNDF`, 64 spp, denoise spatial, fixed positions |

Constructor size is the path-trace resolution. For 960×540→1080p: `new Renderer(960, 540)` then `setPathUpscaleResolution(960, 540, 1920, 1080)`.
