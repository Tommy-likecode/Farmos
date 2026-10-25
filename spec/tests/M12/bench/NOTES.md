# M12 Arc notes

CLAIM recommendation: **PASS** at docs `f6c2285` / product `d23acfb` under FINAL OQ-M12-12 A + OQ-M12-13 A. Glossy protocol, dirty host (chrome/msedge present). **No merge** from this CloudAgent run. Do not invite eggtooth.

| Item | Status |
|------|--------|
| Spec clarification | 2026-10-25: ROI pinned `x=940:980,y=520:560` @1080p (40×40). \|ΔL\| = \|L_xess − L_native\| of means. mean_abs_dL report-only. OQ-12/13 A gates unchanged. |
| XeSS SDK | Official only (`FARMOS_XESS_LIB`). Linux CI has no SDK. Enabler **OUT**. |
| Headless VkImage | Persistent color/depth/MV/output images (no swapchain). Prefer dedicated XeSS VkDevice; borrow fallback. |
| Depth / MV | Path-tracer first-hit `t` + cheap camera MV (static skip). |
| AC-M12-04 quality | **PASS** (OQ-M12-13 A): pinned ROI; L **105.810 / 105.801** ∈ [80,200]; binding \|ΔL\| of means **0.009** ≤ 5.0. mean_abs_dL **0.440** report-only. max_abs **212** report-only. |
| AC-M12-05 timing | **PASS** (OQ-M12-12 A): Pass1 **1.264** (4.441 / 3.512), Pass2 **1.348** (4.712 / 3.497). Binding XeSS < native both. SHOULD ≥ **1.05×**. Prefer ≥1.08–1.10 **YES**. |
| AC-M12-07 | Windows PE B-11 **72704** / 716800. Linux ELF B-11 **68088**. |
| Harness | `Test-HostVulkanDevice=True`. M7 `008_cornell_vulkan_tol` **PASS** (ran; no false soft-skip). |
| Enabler / Swapper / OptiScaler | **OUT** — not used. |

Constructor size is the path-trace resolution. For 960×540→1080p: `new Renderer(960, 540)` then `setPathUpscaleResolution(960, 540, 1920, 1080)`.
