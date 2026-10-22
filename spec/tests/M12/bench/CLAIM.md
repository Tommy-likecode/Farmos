# M12 CLAIM — Native Vulkan Upscale (XeSS / FSR)

**Verdict:** **FAIL Arc (measured tip `6dec6de`)** / **PENDING re-smoke** after lever `more-optimize`  
**Do not merge** until eggtooth AUDIT PASS via Projects Manager.

| Field | Value |
|-------|--------|
| PR | https://github.com/Tommy-likecode/Farmos/pull/20 |
| Branch | `cursor/m12-native-upscale-f17d` |
| Base | `4a661e59` (M11 merged) |
| Implementation tip | `2025473` (`M12: native Vulkan upscale (XeSS / FSR)`) |
| Prior Arc FAIL tip | `3edb3f0` |
| Measured Arc tip | `6dec6de` (`fix-fill-aux-vk`) |
| Lever | `more-optimize` (XeSS disk PSO + borrowed RT device + first-sample depth) |
| Spec | `spec/M12-native-upscale.md` FINAL (OQ-M12-01..09 all A) |
| Date | 2026-10-22 |

## Linux verification (this lever)

Host: Linux cloud agent, no Vulkan device, no XeSS/FSR SDK.

| Suite | Result |
|-------|--------|
| `spec/tests/M12` | 8/8 green (004/006 soft-skip `requires_vulkan`) |
| `spec/tests/M4` | 24/24 PASS (AC-M12-11 jitter OFF) |
| `spec/tests/M11` | 4/4 PASS |
| `spec/tests/M7` | 9/9 PASS (008 soft-skip `requires_vulkan`) |
| `scripts/check_size_budgets.py` | PASS |

### ELF sizes (Linux, farmc `-s`; PE budgets not enforced here)

| ID | Artifact | Bytes | Budget |
|----|----------|-------|--------|
| B-01 | hello | 14464 | 20480 |
| B-03 | glass+mirror | 63616 | 307200 |
| B-08 | GPU-linked cornell | 182400 | 409600 |
| B-09 | RT-linked cornell | 284840 | 512000 |
| B-10 | denoise-linked | 59520 | 614400 |
| B-11 | upscale-glue-linked (path+denoise+upscale) | **67768** | **716800** (700 KiB) |

DCE: hello / unused-math / glass / GPU cornell omit `farm_upscale.c`; upscale-glue demo links `farm_upscale.c` + `-DFARM_ENABLE_UPSCALE`. Glue unchanged vs `6dec6de` (67768). Native AC-05 (`PATH_UPSCALE_OFF`) does **not** load `libxess` at RT device create.

## Measured TommyLaptop Arc (tip `6dec6de`, official XeSS 2.1.1)

Host: Windows x64, Intel Arc, `PATH_BACKEND_VULKAN_RT`. SDK:
`FARMOS_XESS_LIB=D:\Tommy\Farmos\runtime\third_party\xess\libxess.dll`.
**Enabler / Swapper / OptiScaler NOT USED.** Glossy VNDF ON.

### AC-M12-07 Windows PE — **PASS** (tip `6dec6de`; PE not re-run this lever)

Release MSVC `check_size_budgets.py`: B-11 **71680** / 716800. Hello 13824.

### AC-M12-04 / AC-M12-05 at `6dec6de` — binding PASS / SHOULD FAIL

| Gate | Result |
|------|--------|
| AC-M12-04 silver L~100+ | **PASS** (~105.8 both lanes) |
| AC-M12-04 `# png_tol: 12` | **FAIL** (max_abs **212**) — secondary |
| AC-M12-05 binding XeSS < native | **PASS** (3.735 s vs 4.287 s) |
| AC-M12-05 SHOULD ≥1.5× | **FAIL** (ratio **1.148**; need XeSS median ≤ ~2.858 s) |
| In-process AC-04 walls | native ~64.9 s / XeSS ~64.6 s (first-frame cold included) |
| Implied warm in-process | ~2.5 s native vs ~0.94 s XeSS (~2.66×) — protocol-limited on AC-05 |

RCA: fill_aux CPU ray×tris is gone. Remaining AC-05 tax is process-per-frame cold `xessVKInit` / possible second VkDevice (~0.88 s to cut). In-process persist does not help AC-05 timed medians (new process each sample). Warm-up 8 fills disk caches for the 9 timed samples.

## This lever (`more-optimize`) — product change, Arc re-smoke PENDING

Code on this PR (after `6dec6de`):

1. **Disk XeSS pipeline cache.** `xess_vk_init_params_t.pipelineCache` is a `VkPipelineCache` loaded/saved at `%TEMP%/farmos_xess_pso_<dw>x<dh>.bin` (same idea as `farmos_vk_rt_pso_*.bin`). First warmup process compiles; later AC-05 processes should skip XeSS PSO compile inside `xessVKInit`. Saved after init and once after first execute.
2. **Borrow the path-tracer device.** When `setPathUpscale(XESS|FSR)` ran before RT/compute probe, `farm_vk_rt` / `farm_vk` merge XeSS instance + device extensions and feature `pNext` onto the path-tracer `vkCreateDevice`. `xessVKCreateContext` can keep the single borrowed device instead of `abandon_borrowed_vk` → second device. Native `PATH_UPSCALE_OFF` does not query libxess. Retry RT-only if the merged create fails.
3. **First-sample depth, no extra closest-hit.** GPU shaders write first-bounce `t` from sample 0 inside `trace_radiance` (same ray as color). Drops one extra primary per pixel on the XeSS path only. SPIR-V regenerated. Cheap quality alignment; not the 880 ms gap.
4. **Fewer image barriers.** Color/vel/depth layout transitions batched (3-image `vkCmdPipelineBarrier`). Host RGB→RGBA pack + one `vkQueueWaitIdle` for display readback remain (savePNG needs host pixels).
5. Official Intel XeSS only. Default `PATH_UPSCALE_OFF` untouched. Glue ≤700 KiB. `PATH_GLOSSY_VNDF` still on Arc AC fixtures.

**Expected Arc impact (not measured here):** AC-05 **cold** (first warmup) similar or slightly better if borrow avoids a second device; AC-05 **warm** timed medians should drop if XeSS PSO hits disk (primary path to 1.5×). AC-04 in-process already warm after frame 0; borrow + no extra closest-hit help per-frame tax. png_tol unchanged unless first-sample `t` trims outliers.

**Arc AC-M12-04/05 re-smoke is owned by poteto on TommyLaptop after this push.** This agent has no Arc hardware. Until that re-smoke, CLAIM stays **FAIL Arc / no merge**.

## AC-M12-01..12

| ID | Status | Evidence |
|----|--------|----------|
| AC-M12-01 | **PASS** | `001_upscale_off_cpu_twin` SHA = M4-001 |
| AC-M12-02 | **PASS** | `002_upscale_mode_roundtrip` |
| AC-M12-03 | **PASS** | `003_trap_invalid_upscale` exit 122 |
| AC-M12-04 | **FAIL** then **PENDING re-smoke** | `6dec6de` png_tol FAIL max 212; silver L PASS |
| AC-M12-05 | **FAIL** then **PENDING re-smoke** | `6dec6de` ratio 1.148 binding PASS / SHOULD FAIL |
| AC-M12-06 | **PASS (skip policy)** | `006_denoise_then_upscale`; denoise-then-upscale order |
| AC-M12-07 | **PASS** | Linux B-11 **67768** / 716800; prior PE B-11 71680 |
| AC-M12-08 | **PASS** | No Enabler/Swapper/OptiScaler/DLSS/NGX; official SDK only |
| AC-M12-09 | **PASS** | `# requires_xess` / `# requires_vulkan` skip on Linux |
| AC-M12-10 | **PASS** | M9/M10 STRICT unchanged |
| AC-M12-11 | **PASS** | M4 CPU SHA; jitter identity when OFF |
| AC-M12-12 | **PASS** | `005_trap_invalid_resolution` exit 122 |

## Product rules held

- Default OFF; M4/M10/M11 goldens under OFF.
- Headless buffer + `savePNG` only.
- Vendor SDK DLLs external (not in PE cap).
- Frame-gen OUT. Enabler zip on TommyLaptop was not used.
- No merge. No eggtooth this turn.
