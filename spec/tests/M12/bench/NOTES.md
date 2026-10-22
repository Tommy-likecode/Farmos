# M12 Arc notes

CLAIM recommendation: Linux **PASS** at product `4f3ef56`. Arc two-pass AC-05 **pending poteto** (last Arc `edeb845`: 1.237 then 1.131; AUDIT FAIL r1).

| Item | Status |
|------|--------|
| XeSS SDK | Official 2.1.1 on TommyLaptop (`FARMOS_XESS_LIB`). Linux CI has no SDK. |
| Headless VkImage | Persistent color/depth/MV/output images (no swapchain); prefer path-tracer device |
| Depth / MV | Path-tracer first-hit `t` + cheap camera MV (static skip). CPU `fill_aux` ray×tris **removed**. |
| more-headroom | Skip warm XeSS PSO rewrite; defer `libxess` map until GPU wait; hardcoded XeSS 2.x device extras so borrow does not `LoadLibrary` at device-create. |
| AC-M12-04 quality | Last Arc **PASS** at `edeb845`: ROI L **105.810 / 105.805** ∈ [80,140], \|ΔL\| **0.005** ≤ 5.0 (OQ-M12-11 A). mean_abs **0.465** report. max_abs **211** **report-only**. Not re-run on this tip. |
| AC-M12-05 timing | Last Arc primary **1.237** / second **1.131**. Binding XeSS < native both passes. SHOULD ≥ **1.2×** not repeatable at `edeb845`. This tip not Arc-smoked. |
| AC-M12-07 | Linux ELF B-11 **71888** / 716800 at `4f3ef56`. Windows PE **72704** at `edeb845` (pending remeasure). |
| FSR2 optional | selectable; execute soft-fails until official ffx dispatch symbols resolve |
| Enabler / Swapper / OptiScaler | **OUT** — not used. |
| Arc helpers | `ac04_*.fm` / `ac05_*.fm` / `run_arc_ac.py` with `PATH_GLOSSY_VNDF`, 64 spp, denoise spatial, fixed positions |

Constructor size is the path-trace resolution. For 960×540→1080p: `new Renderer(960, 540)` then `setPathUpscaleResolution(960, 540, 1920, 1080)`.
