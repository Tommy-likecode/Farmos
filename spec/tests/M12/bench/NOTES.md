# M12 Arc notes

CLAIM recommendation: Linux **PASS** at product `fd31e46`. Arc two-pass AC-05 **pending poteto** (last Arc `4f3ef56`: 1.149 / 1.196 FAIL).

| Item | Status |
|------|--------|
| XeSS SDK | Official 2.1.1 on TommyLaptop (`FARMOS_XESS_LIB`). Linux CI has no SDK. |
| Headless VkImage | Persistent color/depth/MV/output images (no swapchain); prefer path-tracer device |
| Depth / MV | Path-tracer first-hit `t` + cheap camera MV (static skip). CPU `fill_aux` ray×tris **removed**. |
| more-optimize | Restore official `xessVKGetRequired*` extras (hardcoded Features2 likely broke borrow). CreateContext+init during path GPU wait. Keep file-present availability + skip-rewrite of warm PSO. |
| AC-M12-04 quality | Last Arc at `4f3ef56`: ROI L **190.98 / 191.14** band FAIL; center ~106. `edeb845` was **105.81**. Not re-run on this tip. |
| AC-M12-05 timing | Last Arc `4f3ef56` **1.149 / 1.196** FAIL (dirty chrome/msedge, ~3× `edeb845` walls). This tip not Arc-smoked. |
| AC-M12-07 | Linux ELF B-11 **67792** / 716800 at `fd31e46`. Windows PE **73216** at `4f3ef56`. |
| FSR2 optional | selectable; execute soft-fails until official ffx dispatch symbols resolve |
| Enabler / Swapper / OptiScaler | **OUT** — not used. |
| Arc helpers | `ac04_*.fm` / `ac05_*.fm` / `run_arc_ac.py` with `PATH_GLOSSY_VNDF`, 64 spp, denoise spatial, fixed positions |

Constructor size is the path-trace resolution. For 960×540→1080p: `new Renderer(960, 540)` then `setPathUpscaleResolution(960, 540, 1920, 1080)`.
