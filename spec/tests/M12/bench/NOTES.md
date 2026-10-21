# M12 Arc notes (fill on TommyLaptop)

Pending until native XeSS Vulkan interop is smoke-tested on Arc.

| Item | Status |
|------|--------|
| XeSS SDK obtain / license | fetch script `scripts/fetch_upscale_sdk.sh`; DLLs **external** |
| Headless VkImage usages | glue creates color/depth/MV/output images (no swapchain) |
| AC-M12-04 quality | **pending** Arc |
| AC-M12-05 timing | **pending** Arc (SHOULD ≥1.5× reported, not binding until measured) |
| FSR2 optional | selectable; `isPathUpscaleAvailable` follows lib presence; execute soft-fails until official ffx dispatch symbols resolve |
| Enabler / Swapper / OptiScaler | **OUT** — do not use if SDK interop blocks; report blocker |
| Linux CI without SDK | OFF twins + trap + roundtrip + CPU soft-fail + STRICT FORCE_FAIL; XeSS/FSR smokes soft-skip |

Constructor size is the path-trace resolution. For the 960×540→1080p Arc smoke,
construct `new Renderer(960, 540)` then `setPathUpscaleResolution(960, 540, 1920, 1080)`.
