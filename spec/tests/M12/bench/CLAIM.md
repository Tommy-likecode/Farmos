# M12 Arc CLAIM — OQ-M12-12/13 A (COMPLETE)

**Verdict:** **CLAIM PASS** under FINAL OQ-M12-12 A + OQ-M12-13 A  
**Do not merge.** Do not invite eggtooth. Enabler NOT used.

| Field | Value |
|-------|--------|
| PR | #21 (draft) https://github.com/Tommy-likecode/Farmos/pull/21 |
| Branch | `cursor/m12-clear-backlog-4ef8` |
| Docs HEAD (measured) | `f6c228528a59121951fb815ab56964ec85beae1b` |
| Product SHA | `d23acfb3d8efde48698f210a9f7666c377bfb9f2` (unchanged; no rebuild) |
| Host | TommyLaptop, Windows x64, Intel Arc, `PATH_BACKEND_VULKAN_RT` |
| Protocol | **VALID glossy** — in-repo `spec/tests/M12/bench/{ac04_native_1080,ac04_xess_960,ac05_native_1f,ac05_xess_1f}.fm` with `PATH_GLOSSY_VNDF` |
| Host hygiene | chrome/msedge **present** (dirty) |
| Spec | `spec/M12-native-upscale.md` **FINAL** (OQ-M12-01..09 A; OQ-10/11 A historical; **OQ-M12-12 A + OQ-M12-13 A** binding; 2026-10-25 ROI pin + \|ΔL\| vs mean_abs_dL) |
| SDK | Official XeSS only (`libxess.dll`). Enabler / Swapper / OptiScaler **OUT**. |
| Fixtures | In-repo `spec/tests/M12/bench/` (`PATH_GLOSSY_VNDF` ON) |

## Gates (binding OQ-M12-12 A / OQ-M12-13 A)

| Gate | Rule | Measured | Result |
|------|------|----------|--------|
| AC-M12-05 binding | XeSS wall strictly < native | Pass1 3.512 < 4.441; Pass2 3.497 < 4.712 | **PASS** both |
| AC-M12-05 SHOULD | ≥ **1.05×** (OQ-M12-12 A; historical ≥1.2× **superseded**) | Pass1 **1.264**; Pass2 **1.348** | **PASS** both; prefer ≥1.08–1.10 **YES** |
| AC-M12-04 primary | pinned ROI `x=940:980,y=520:560` @1080p (40×40); L both lanes ∈ **[80, 200]**; **\|ΔL\| = \|L_xess − L_native\| of means** ≤ 5.0 | L **105.810 / 105.801**; \|ΔL\| **0.009** | **PASS** |
| AC-M12-04 mean_abs_dL | report-only | **0.440** | report-only — not \|ΔL\| |
| AC-M12-04 png_tol / max_abs | report-only | max_abs **212** | report-only |
| AC-M12-07 PE | B-11 ≤ 700 KiB | **72704** / 716800 | **PASS** |

Historical OQ-M12-10 A SHOULD ≥ **1.2×** and OQ-M12-11 A band **[80, 140]** are **superseded** (documented; not binding). Helper logs must **not** print `should_1_2=FAIL` / `png_tol_12=FAIL` as if those were live gates.

Prior merged-glossy lock inputs (`7f0a080` / `fd31e46`, clean host): Pass1 **1.052** / Pass2 **1.162**; L **187.228 / 187.877**; \|ΔL\| of means ≈ **0.650**; mean_abs_dL **3.932** (was mislabeled \|ΔL\|). Historical only.

## AC-M12-04 numbers (glossy; tip `f6c2285` / product `d23acfb`)

Pinned ROI `x=940:980,y=520:560` @1920×1080 (40×40). Silver ROI L native/xess **105.810 / 105.801**. Binding \|ΔL\| of means **0.009**. mean_abs_dL **0.440** (report-only). max_abs **212** (report-only).

## AC-M12-05 numbers (glossy; process-per-frame)

| | Pass1 (PSO cleared) | Pass2 (warm PSO) |
|--|---------------------|------------------|
| Native median | **4.441** s | **4.712** s |
| XeSS median | **3.512** s | **3.497** s |
| ratio native/xess | **1.264** | **1.348** |
| SHOULD ≥1.05× | **PASS** | **PASS** |
| prefer ≥1.08–1.10 | **YES** | **YES** |
| XeSS < native | **PASS** | **PASS** |

## Windows harness

`Test-HostVulkanDevice=True`. M7 `008_cornell_vulkan_tol` **PASS** (ran; no false soft-skip).

## Linux verification (product `d23acfb`; no Vulkan/SDK on Linux host)

| Command | Result |
|---------|--------|
| `bash scripts/run_suite_linux.sh build/farmc spec/tests/M12` | 8/8 (004/006 soft-skip `requires_vulkan`) |
| `bash scripts/run_suite_linux.sh build/farmc spec/tests/M4` | 24/24 PASS |
| `bash scripts/run_suite_linux.sh build/farmc spec/tests/M11` | 4/4 PASS |
| `bash scripts/run_suite_linux.sh build/farmc spec/tests/M7` | 9/9 (008 soft-skip `requires_vulkan`) |
| `python3 scripts/check_size_budgets.py build/farmc .` | PASS — ELF B-11 **68088** / 716800 |

## AC-M12-01..12

| ID | Status | Evidence |
|----|--------|----------|
| AC-M12-01 | **PASS** | Linux `001_upscale_off_cpu_twin` |
| AC-M12-02 | **PASS** | Linux `002_upscale_mode_roundtrip` |
| AC-M12-03 | **PASS** | Linux `003_trap_invalid_upscale` exit 122 |
| AC-M12-04 | **PASS** | ROI L **105.810 / 105.801** ∈ [80,200]; \|ΔL\| of means **0.009** ≤ 5.0; mean_abs_dL **0.440** report; max_abs **212** report-only |
| AC-M12-05 | **PASS** | Pass1 **1.264**, Pass2 **1.348**; XeSS < native both; SHOULD ≥ **1.05×** both; prefer ≥1.08–1.10 **YES** |
| AC-M12-06 | **PASS (skip policy)** | Linux `006_denoise_then_upscale` |
| AC-M12-07 | **PASS** | PE B-11 **72704** / 716800; Linux ELF 68088 |
| AC-M12-08 | **PASS** | Official XeSS only; Enabler/Swapper/OptiScaler NOT USED |
| AC-M12-09 | **PASS** | `# requires_xess` / `# requires_vulkan` skip on Linux |
| AC-M12-10 | **PASS** | M11 trap 121 + m12_008 STRICT |
| AC-M12-11 | **PASS** | M4 CPU SHA; jitter identity when OFF |
| AC-M12-12 | **PASS** | Linux `005_trap_invalid_resolution` exit 122 |

## Product rules

- Official Intel XeSS only. **Enabler / Swapper / OptiScaler NOT USED.**
- Default `PATH_UPSCALE_OFF` untouched.
- Protocol VALID glossy; host dirty (chrome/msedge present).
- Dedicated XeSS VkDevice for init (borrow fallback). Product SHA **unchanged** for this docs sync.
- **No merge** from this CloudAgent run.
