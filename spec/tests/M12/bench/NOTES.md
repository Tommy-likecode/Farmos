# M12 Arc notes (TommyLaptop tip `9b4a11f`)

CLAIM recommendation: **PENDING** (PE PASS; AC-M12-04/05 blocked on missing `libxess.dll`).

| Item | Status |
|------|--------|
| XeSS SDK obtain / license | **BLOCKED** — missing `libxess.dll`. `FARMOS_XESS_LIB` unset; `runtime/third_party/xess/` absent; searched Intel / Downloads / `D:\Tommy` / System32 — none. Fetch script remains `scripts/fetch_upscale_sdk.sh`; DLLs **external**. |
| Headless VkImage usages | glue creates color/depth/MV/output images (no swapchain) |
| AC-M12-04 quality | **PENDING / BLOCKED** — missing `libxess.dll`; no 960×540→1080p smoke |
| AC-M12-05 timing | **PENDING / BLOCKED** — same; SHOULD ≥1.5× after first CLEAN tip with SDK |
| AC-M12-07 Windows PE | **PASS** (Release MSVC `check_size_budgets.py`): hello 13824; glass 66048; GPU 183296; RT 288256; denoise 59392; **B-11 73216 / 716800** |
| `ctest -C Release -R m12_` | 8/8 Passed. 004/006 soft-skipped (`requires_vulkan` harness quirk; Arc has 1 Vulkan device). Would still skip `# requires_xess` without DLL. |
| FSR2 optional | selectable; `isPathUpscaleAvailable` follows lib presence; execute soft-fails until official ffx dispatch symbols resolve |
| Enabler / Swapper / OptiScaler | **OUT** — `DLSS-Enabler-0.9.4.zip` in Downloads was **NOT USED**. Do not use if SDK interop blocks; report blocker. |
| Env | Dirty: ACE-Tray, DSATray, Steam++, Intel Graphics services. Watt Toolkit: git fetch OK. |
| Linux CI without SDK | OFF twins + trap + roundtrip + CPU soft-fail + STRICT FORCE_FAIL; XeSS/FSR smokes soft-skip |

Constructor size is the path-trace resolution. For the 960×540→1080p Arc smoke **after** official `libxess.dll` is present: construct `new Renderer(960, 540)` then `setPathUpscaleResolution(960, 540, 1920, 1080)`.
