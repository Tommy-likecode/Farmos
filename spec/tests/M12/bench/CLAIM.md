# M12 Arc CLAIM — OQ-M12-10/11 A re-gate (COMPLETE)

**Verdict:** **PASS** under FINAL OQ-M12-10 A + OQ-M12-11 A (PM/user lock 2026-10-22 oq-relax-1.2-roi)  
**Do not merge. Await eggtooth via PM. Enabler NOT used.**

| Field | Value |
|-------|--------|
| PR | #20 (draft) https://github.com/Tommy-likecode/Farmos/pull/20 |
| Branch | `cursor/m12-native-upscale-f17d` |
| Tip SHA | `edeb84531de709226a2dd9aa23d8d7c169253558` |
| Host | TommyLaptop, Windows x64, Intel Arc, `PATH_BACKEND_VULKAN_RT` |
| Date (Asia/Shanghai) | 2026-10-22 ~20:10 CST (smoke); claim re-gate ~20:26 CST |
| Spec | `spec/M12-native-upscale.md` **FINAL** (OQ-M12-01..09 A; OQ-M12-10 A + OQ-M12-11 A) |
| SDK | Official XeSS 2.1.1 (`FARMOS_XESS_LIB`=`D:\Tommy\Farmos\runtime\third_party\xess\libxess.dll`) |
| Fixtures | In-repo `spec/tests/M12/bench/` (`PATH_GLOSSY_VNDF` ON) |

## Gates (FINAL)

| Gate | Rule | Measured | Result |
|------|------|----------|--------|
| AC-M12-05 binding | XeSS wall < native | 3.477 < 4.302 | **PASS** |
| AC-M12-05 SHOULD | ≥ **1.2×** (OQ-M12-10 A; historical ≥1.5 **superseded**) | ratio **1.237** | **PASS** |
| AC-M12-04 primary | ROI L both lanes ∈ [80,140]; \|ΔL\| ≤ 5.0 (OQ-M12-11 A) | L **105.810 / 105.805**; \|ΔL\| **0.005** | **PASS** |
| AC-M12-04 mean_abs | report | **0.465** | report |
| AC-M12-04 png_tol / max_abs | **report-only** (OQ-M12-11 A; historical hard `# png_tol: 12` **superseded**) | max_abs **211** | report-only (not FAIL) |
| AC-M12-07 PE | B-11 ≤ 700 KiB | **72704** / 716800 | **PASS** |

## AC-M12-04 numbers (unchanged tip `edeb845`)

| Lane | wall_s |
|------|--------|
| Native 1920×1080 OFF | **31.880** |
| XeSS 960→1080 | **20.529** |

Silver ROI L native/xess **105.810 / 105.805**. mean_abs **0.465**. max_abs **211** (report-only).

## AC-M12-05 numbers (unchanged tip `edeb845`)

| | median wall_s |
|--|---------------|
| Native | **4.302** |
| XeSS | **3.477** |
| ratio native/xess | **1.237** |

## Linux verification (tip `edeb845`; no Vulkan/SDK on this host)

| Suite | Result |
|-------|--------|
| `spec/tests/M12` | 8/8 green (004/006 soft-skip `requires_vulkan`) |
| `spec/tests/M4` | 24/24 PASS |
| `spec/tests/M11` | 4/4 PASS |
| `spec/tests/M7` | 9/9 PASS (008 soft-skip `requires_vulkan`) |
| `scripts/check_size_budgets.py` | PASS |

ELF B-11 **67768** / 716800. Hello 14464. DCE: unused upscale omits `farm_upscale.c`.

## AC-M12-01..12

| ID | Status | Evidence |
|----|--------|----------|
| AC-M12-01 | **PASS** | `001_upscale_off_cpu_twin` SHA = M4-001 |
| AC-M12-02 | **PASS** | `002_upscale_mode_roundtrip` |
| AC-M12-03 | **PASS** | `003_trap_invalid_upscale` exit 122 |
| AC-M12-04 | **PASS** | ROI L **105.810 / 105.805** ∈ [80,140]; \|ΔL\| **0.005** ≤ 5.0; mean_abs **0.465** report; max_abs **211** report-only |
| AC-M12-05 | **PASS** | XeSS 3.477 < native 4.302; ratio **1.237** ≥ 1.2 |
| AC-M12-06 | **PASS (skip policy)** | `006_denoise_then_upscale` |
| AC-M12-07 | **PASS** | PE B-11 **72704** / 716800; Linux ELF 67768 |
| AC-M12-08 | **PASS** | Official XeSS 2.1.1 only; Enabler/Swapper/OptiScaler NOT USED |
| AC-M12-09 | **PASS** | `# requires_xess` / `# requires_vulkan` skip on Linux |
| AC-M12-10 | **PASS** | M9/M10 STRICT unchanged |
| AC-M12-11 | **PASS** | M4 CPU SHA; jitter identity when OFF |
| AC-M12-12 | **PASS** | `005_trap_invalid_resolution` exit 122 |

## Product rules

- Official Intel XeSS only. **Enabler / Swapper / OptiScaler NOT USED.**
- Default `PATH_UPSCALE_OFF` untouched.
- **No merge** until eggtooth AUDIT PASS (PM routes eggtooth).
- Prior MIXED under historical SHOULD ≥1.5× / hard png_tol:12 is **superseded** by OQ-10/11 A; measured numbers at tip `edeb845` kept. Do not invent new smoke numbers.

## Artifacts

- `/workspace/m12_arc/resmoke_edeb845.log`
- `/workspace/m12_arc/{native,xess}_1080.png`
- `/workspace/m12_arc/pe_budgets_edeb845.log`
