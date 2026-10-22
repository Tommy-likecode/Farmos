# M12 CLAIM — Native Vulkan Upscale (XeSS / FSR)

**Verdict:** **FAIL Arc (prior tip `3edb3f0`)** / **PENDING re-smoke** after lever `fix-fill-aux-vk`  
**Do not merge** until eggtooth AUDIT PASS via Projects Manager.

| Field | Value |
|-------|--------|
| PR | https://github.com/Tommy-likecode/Farmos/pull/20 |
| Branch | `cursor/m12-native-upscale-f17d` |
| Base | `4a661e59` (M11 merged) |
| Implementation tip | `2025473` (`M12: native Vulkan upscale (XeSS / FSR)`) |
| Prior Arc FAIL tip | `3edb3f0` |
| Lever | `fix-fill-aux-vk` (path-tracer depth + persistent XeSS Vk) |
| Spec | `spec/M12-native-upscale.md` FINAL (OQ-M12-01..09 all A) |
| Date | 2026-10-22 |

## Linux verification (this lever)

Host: Linux cloud agent, no Vulkan device, no XeSS/FSR SDK. After fill_aux / persist-Vk:

| Suite | Result |
|-------|--------|
| `spec/tests/M12` | 8/8 green (004/006 soft-skip `requires_vulkan`) |
| `spec/tests/M4` | 24/24 PASS (AC-M12-11 jitter OFF) |
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

DCE: hello / unused-math / glass / GPU cornell omit `farm_upscale.c`; upscale-glue demo links `farm_upscale.c` + `-DFARM_ENABLE_UPSCALE`. Glue shrank vs 71864 after removing CPU ray×tris.

## Prior TommyLaptop evidence (tip `3edb3f0`, official XeSS 2.1.1)

Host: Windows x64, Intel Arc, `PATH_BACKEND_VULKAN_RT`. SDK:
`FARMOS_XESS_LIB=D:\Tommy\Farmos\runtime\third_party\xess\libxess.dll`.
**Enabler / Swapper / OptiScaler NOT USED.**

### AC-M12-07 Windows PE — **PASS** (tip `9b4a11f`; PE not re-run this lever)

Release MSVC `check_size_budgets.py`: B-11 **73216** / 716800. Hello 13824.

### AC-M12-04 / AC-M12-05 at `3edb3f0` — **FAIL**

Glossy VNDF ON (local dirty `out/m12_arc/` then); silver ROI L ~191 both lanes.

| Gate | Result |
|------|--------|
| AC-M12-04 silver L~100+ | **PASS** (~191) |
| AC-M12-04 `# png_tol: 12` | **FAIL** (max_abs 206) |
| AC-M12-05 SHOULD ≥1.5× | **FAIL** (ratio native/xess **0.330**; 7.646 s vs 23.136 s median) |
| In-process AC-04 walls | native ~37 s / XeSS ~191 s |

RCA (`INVESTIGATION_timing.md`): primary tax CPU `fill_aux` O(pixels×tris) after GPU color; secondary per-frame Vk create/destroy + staging upload/readback + second Vk device.

## This lever (`fix-fill-aux-vk`) — product change, Arc re-smoke PENDING

Code on this PR (after `3edb3f0`):

1. **Depth from the path tracer.** GPU compute/RT write first-hit `t` in the accum SSBO tail (`accum[3*npix + i]`); denoise still reads stride-3 RGB. CPU first sample records first-bounce `t` into `depthbuffer`. `fill_aux` is O(pixels): linearize `t` to 0–1 depth. Static camera **skips** MV (zeros). Moving camera unprojects with `t` — **no** CPU ray×tris.
2. **Persistent XeSS Vk.** Color/vel/depth/output images + staging buffers + command buffer live across frames. Prefer borrowing the path-tracer `VkDevice` (`farm_vk_rt_export_handles` / `farm_vk_export_handles`); if `xessVKCreateContext` rejects it, fall back to a dedicated upscale device.
3. Official Intel XeSS only. Default `PATH_UPSCALE_OFF` untouched. Glue budget still ≤700 KiB.
4. Arc AC helpers committed: `spec/tests/M12/bench/ac04_*.fm`, `ac05_*.fm`, `run_arc_ac.py` with `PATH_GLOSSY_VNDF`.

**Arc AC-M12-04/05 re-smoke is owned by poteto on TommyLaptop after this push.** This agent has no Arc hardware. Until that re-smoke, CLAIM stays **FAIL Arc / no merge**.

## AC-M12-01..12

| ID | Status | Evidence |
|----|--------|----------|
| AC-M12-01 | **PASS** | `001_upscale_off_cpu_twin` SHA = M4-001 |
| AC-M12-02 | **PASS** | `002_upscale_mode_roundtrip` |
| AC-M12-03 | **PASS** | `003_trap_invalid_upscale` exit 122 |
| AC-M12-04 | **FAIL** then **PENDING re-smoke** | `3edb3f0` png_tol FAIL; silver L PASS; re-smoke after this lever |
| AC-M12-05 | **FAIL** then **PENDING re-smoke** | `3edb3f0` ratio 0.330; binding wall < native after fill_aux fix |
| AC-M12-06 | **PASS (skip policy)** | `006_denoise_then_upscale`; denoise-then-upscale order |
| AC-M12-07 | **PASS** | Linux B-11 **67768** / 716800; prior PE B-11 73216 (re-verify on laptop if needed) |
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
