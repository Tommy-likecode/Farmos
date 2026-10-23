# M12 Arc CLAIM — more-optimize product (Linux measured; Arc pending)

**Verdict:** Linux CI **PASS**. Arc two independent AC-05 warm medians **pending poteto**.  
**Do not merge. Do not invite eggtooth. Official XeSS only. Enabler NOT used.**

| Field | Value |
|-------|--------|
| PR | #20 (draft) https://github.com/Tommy-likecode/Farmos/pull/20 |
| Branch | `cursor/m12-native-upscale-f17d` |
| Product SHA | `fd31e46a3cd48bf335fad580f0621ddaf4a115ec` (more-optimize) |
| Last Arc SHA | `4f3ef560844da23f1e57843dda3adb9d4f0829f2` (more-headroom **FAIL**) |
| Host (Arc) | TommyLaptop, Windows x64, Intel Arc, `PATH_BACKEND_VULKAN_RT` |
| Spec | FINAL OQ-M12-10 A (≥1.2×) + OQ-M12-11 A (ROI primary) |
| SDK | Official XeSS 2.1.1 (`libxess.dll`) |

## Last Arc (`4f3ef56` more-headroom) — not this tip

| Gate | Measured | Result |
|------|----------|--------|
| AC-05 pass1 | 12.302 / 10.703 → **1.149** | SHOULD FAIL (binding XeSS < native PASS) |
| AC-05 pass2 | 11.854 / 9.908 → **1.196** | SHOULD FAIL |
| AC-04 ROI L | **190.980 / 191.136** (center RGB≈99,107,121 ≈ L~106) | band FAIL; \|ΔL\| 4.964 OK |
| PE B-11 | **73216** / 716800 | PASS |
| Absolute walls | ~3× `edeb845` (chrome/msedge dirty) | load-inflated |

Do not invent new Arc walls for `fd31e46`.

## Product change (why ratio should recover toward ≥1.25×)

`4f3ef56` hardcoded XeSS device extras (`VkPhysicalDeviceFeatures2` + `VK_EXT_mutable_descriptor_type`) so RT device-create would not `LoadLibrary` libxess. That list is not the SDK’s device-dependent chain. If `xessVKCreateContext` rejected the borrowed device, every AC-05 XeSS process paid a second VkInstance+Device — a XeSS-only tax that matches the drop from `edeb845` **1.237** to **1.149 / 1.196**.

This tip:

1. **Restore official `xessVKGetRequiredInstance/DeviceExtensions/Features`** at RT instance/device create (same borrow path as `edeb845`). `isPathUpscaleAvailable` stays file-present (no 75 MiB map at probe).
2. **Keep skip-rewrite** of a warm XeSS PSO blob (`vkGetPipelineCacheData` after execute is skipped when the disk cache already loaded).
3. **`farm_upscale_prepare_xess`** after path `vkQueueSubmit`: CreateContext + `xessVKInit` (disk PSO) + persistent images overlap `vkQueueWaitIdle`. No PSO dump during that wait.
4. Static-camera `fill_aux` skips MV zeroing; RGB→RGBA uses packed stores.

ROI L~191 with center still ~106 is **not** explained by XeSS extras (native AC-04 does not merge them). Likely specular in the 40×40 silver box and/or dirty-host variance vs `edeb845` L~105.8. Integrator/shader unchanged. Residual: poteto must re-check ROI ∈ [80,140] on a clean box.

## Linux verification (product `fd31e46`)

| Suite | Result |
|-------|--------|
| `spec/tests/M12` | 8/8 (004/006 skip `requires_vulkan`) |
| `spec/tests/M4` | 24/24 |
| `spec/tests/M11` | 4/4 |
| `spec/tests/M7` | 9/9 (008 skip) |
| `scripts/check_size_budgets.py` | PASS |

ELF B-11 **67792** / 716800. Hello 14464. DCE: unused upscale omits `farm_upscale.c`.

## Product rules

- Official Intel XeSS only. **Enabler / Swapper / OptiScaler NOT USED.**
- Default `PATH_UPSCALE_OFF` untouched. AC text / `SHOULD_RATIO` in `run_arc_ac.py` **not** weakened.
- **No merge.** Two independent Arc warm medians both ≥1.2× (prefer ≥1.25) are poteto’s job.
