# M12 Arc notes

CLAIM recommendation: **not claimed** on this tip. clear-m12-backlog after master `f5b87f8f`. Glossy Arc AC-04 / AC-05 will be re-measured on TommyLaptop (poteto Arc-smoke). **No merge** from this CloudAgent run.

| Item | Status |
|------|--------|
| Spec clarification | 2026-10-25: ROI pinned `x=940:980,y=520:560` @1080p (40×40). \|ΔL\| = \|L_xess − L_native\| of means (baseline ≈ **0.650**). **3.932** is mean_abs_dL report-only. OQ-12/13 A gates unchanged. |
| XeSS SDK | Official only (`FARMOS_XESS_LIB`). Linux CI has no SDK. Enabler **OUT**. |
| Headless VkImage | Persistent color/depth/MV/output images (no swapchain). |
| Depth / MV | Path-tracer first-hit `t` + cheap camera MV (static skip). |
| Pass1 headroom | Dedicated XeSS VkDevice for CreateContext/init/PSO so init cannot DeviceWaitIdle the path queue. Borrow fallback. Not Arc-measured here. |
| AC-M12-04 quality | Binding: pinned ROI; L ∈ [80, 200]; \|ΔL\| of means ≤ 5.0. png_tol / max_abs / mean_abs_dL report-only. |
| AC-M12-05 timing | Binding: SHOULD ≥ 1.05×; XeSS < native. Baseline Pass1 **1.052** / Pass2 **1.162** — target Pass1 ≥1.08–1.10. |
| Harness | Windows `Test-HostVulkanDevice`: temp-file ctypes probe + Process ExitCode (not `Start-Process -c`). Soft-skip only after a real "no device" probe; loader present + probe tooling fail → run / hard-fail. |
| Enabler / Swapper / OptiScaler | **OUT** — not used. |

Constructor size is the path-trace resolution. For 960×540→1080p: `new Renderer(960, 540)` then `setPathUpscaleResolution(960, 540, 1920, 1080)`.
