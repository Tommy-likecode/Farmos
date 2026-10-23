# M12 Arc CLAIM — OQ-M12-12/13 A (COMPLETE)

**Verdict:** **CLAIM PASS** under FINAL OQ-M12-12 A + OQ-M12-13 A  
**Do not merge.** Await eggtooth AUDIT PASS via PM. Enabler NOT used.

| Field | Value |
|-------|--------|
| PR | #20 (draft) https://github.com/Tommy-likecode/Farmos/pull/20 |
| Branch | `cursor/m12-native-upscale-f17d` |
| Docs HEAD (measured) | `7f0a080330e306ec9190a605c3b5e9d1ad47eebe` |
| Product SHA | `fd31e46a3cd48bf335fad580f0621ddaf4a115ec` (unchanged; no rebuild) |
| Host | TommyLaptop, Windows x64, Intel Arc, `PATH_BACKEND_VULKAN_RT` |
| Protocol | **VALID glossy** — in-repo `spec/tests/M12/bench/{ac04_native_1080,ac04_xess_960,ac05_native_1f,ac05_xess_1f}.fm` with `PATH_GLOSSY_VNDF` |
| Host hygiene | chrome/msedge **absent**; PSO cleared before Pass1 |
| Spec | `spec/M12-native-upscale.md` **FINAL** (OQ-M12-01..09 A; OQ-10/11 A historical; **OQ-M12-12 A + OQ-M12-13 A** binding) |
| SDK | Official XeSS 2.1.1 (`libxess.dll`) |
| Fixtures | In-repo `spec/tests/M12/bench/` (`PATH_GLOSSY_VNDF` ON) |

## Gates (binding OQ-M12-12 A / OQ-M12-13 A)

| Gate | Rule | Measured | Result |
|------|------|----------|--------|
| AC-M12-05 binding | XeSS wall strictly < native | Pass1 7.694 < 8.095; Pass2 7.118 < 8.272 | **PASS** both |
| AC-M12-05 SHOULD | ≥ **1.05×** (OQ-M12-12 A; historical ≥1.2× **superseded**) | Pass1 **1.052**; Pass2 **1.162** | **PASS** both |
| AC-M12-04 primary | ROI L both lanes ∈ **[80, 200]**; \|ΔL\| ≤ 5.0 (OQ-M12-13 A) | L **187.228 / 187.877**; \|ΔL\| **3.932** | **PASS** |
| AC-M12-04 mean_abs | report | **0.467** | report |
| AC-M12-04 png_tol / max_abs | **report-only** | max_abs **212** | report-only |
| AC-M12-07 PE | B-11 ≤ 700 KiB | **72704** / 716800 | **PASS** |

Historical OQ-M12-10 A SHOULD ≥ **1.2×** and OQ-M12-11 A band **[80, 140]** are **superseded** (documented; not binding). `should_1_2=FAIL` / `png_tol_12=FAIL` labels in helper logs are expected under superseded gates.

## AC-M12-04 numbers (glossy; tip `7f0a080` / product `fd31e46`)

| Lane | wall_s |
|------|--------|
| Native 1920×1080 OFF (12 in-process) | **45.242** |
| XeSS 960→1080 (12 in-process) | **38.077** |

Silver ROI L native/xess **187.228 / 187.877**. \|ΔL\| **3.932**. mean_abs **0.467**. max_abs **212** (report-only).

## AC-M12-05 numbers (glossy; process-per-frame 8 warmup + 9 timed)

| | Pass1 (PSO cleared → AC-04 warm → AC-05) | Pass2 (warm PSO, AC-05 only) |
|--|------------------------------------------|------------------------------|
| Native median | **8.095** s | **8.272** s |
| XeSS median | **7.694** s | **7.118** s |
| ratio native/xess | **1.052** | **1.162** |
| SHOULD ≥1.05× | **PASS** | **PASS** |
| XeSS < native | **PASS** | **PASS** |

PSO after Pass1: `%TEMP%/farmos_xess_pso_1920x1080.bin` **323625** B.

## Linux verification (product `fd31e46`; no Vulkan/SDK on this host)

| Suite | Result |
|-------|--------|
| `spec/tests/M12` | 8/8 green (004/006 soft-skip `requires_vulkan`) |
| `spec/tests/M4` | 24/24 PASS |
| `spec/tests/M11` | 4/4 PASS |
| `spec/tests/M7` | 9/9 PASS (008 soft-skip `requires_vulkan`) |
| `scripts/check_size_budgets.py` | PASS |

ELF B-11 **67792** / 716800. Hello 14464. DCE: unused upscale omits `farm_upscale.c`.

## AC-M12-01..12

| ID | Status | Evidence |
|----|--------|----------|
| AC-M12-01 | **PASS** | Linux `001_upscale_off_cpu_twin` |
| AC-M12-02 | **PASS** | Linux `002_upscale_mode_roundtrip` |
| AC-M12-03 | **PASS** | Linux `003_trap_invalid_upscale` exit 122 |
| AC-M12-04 | **PASS** | ROI L **187.228 / 187.877** ∈ [80,200]; \|ΔL\| **3.932** ≤ 5.0; mean_abs **0.467** report; max_abs **212** report-only |
| AC-M12-05 | **PASS** | Pass1 **1.052**, Pass2 **1.162**; XeSS < native both; SHOULD ≥ **1.05×** both |
| AC-M12-06 | **PASS (skip policy)** | Linux `006_denoise_then_upscale` |
| AC-M12-07 | **PASS** | PE B-11 **72704** / 716800; Linux ELF 67792 |
| AC-M12-08 | **PASS** | Official XeSS 2.1.1 only; Enabler/Swapper/OptiScaler NOT USED |
| AC-M12-09 | **PASS** | `# requires_xess` / `# requires_vulkan` skip on Linux |
| AC-M12-10 | **PASS** | M11 trap 121 + m12_008 STRICT |
| AC-M12-11 | **PASS** | M4 CPU SHA; jitter identity when OFF |
| AC-M12-12 | **PASS** | Linux `005_trap_invalid_resolution` exit 122 |

## Product rules

- Official Intel XeSS only. **Enabler / Swapper / OptiScaler NOT USED.**
- Default `PATH_UPSCALE_OFF` untouched.
- Protocol VALID glossy; host clean (chrome/msedge absent).
- **No merge** until eggtooth AUDIT PASS via Projects Manager.

## Artifacts

- `/workspace/m12_arc/resmoke_7f0a080.log` (glossy Pass1)
- `/workspace/m12_arc/resmoke_7f0a080_ac05_second.log` (glossy Pass2)
- `/workspace/m12_arc/pe_budgets_7f0a080.log`
- `/workspace/m12_arc/ac04_metrics.txt`, `ac05_timing.txt`, `ac05_second_pass.txt`
- `/workspace/m12_arc/{native,xess}_1080.png`
