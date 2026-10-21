# M12 CLAIM — Native Vulkan Upscale (XeSS / FSR)

**Verdict:** **PASS (Linux CI + Windows PE)** / **PENDING (Arc AC-M12-04/05 — missing libxess.dll)**  
**Do not merge** until eggtooth AUDIT PASS via Projects Manager.

| Field | Value |
|-------|--------|
| PR | https://github.com/Tommy-likecode/Farmos/pull/20 |
| Branch | `cursor/m12-native-upscale-f17d` |
| Base | `4a661e59` (M11 merged) |
| Implementation tip | `2025473` (`M12: native Vulkan upscale (XeSS / FSR)`) |
| TommyLaptop tip | `9b4a11f` (this CLAIM's previous PR tip; PE + Arc search) |
| Spec | `spec/M12-native-upscale.md` FINAL (OQ-M12-01..09 all A) |
| Date | 2026-10-21 |

## Linux verification (cloud agent)

Host: Linux cloud agent, no Vulkan device, no XeSS/FSR SDK.

| Suite | Result |
|-------|--------|
| `spec/tests/M12` | 8/8 green (004/006 soft-skip `requires_vulkan`) |
| `spec/tests/M4` | 24/24 PASS (AC-M12-11 jitter OFF) |
| `spec/tests/M7` | 9/9 green (GPU skip) |
| `spec/tests/M9` | 6/6 green (RT skip) |
| `spec/tests/M10` | 6/6 PASS |
| `spec/tests/M11` | 4/4 PASS |
| `scripts/check_size_budgets.py` | PASS |

### ELF sizes (Linux, farmc `-s`; PE budgets not enforced here)

| ID | Artifact | Bytes | Budget |
|----|----------|-------|--------|
| B-01 | hello | 14464 | 20480 |
| B-03 | glass+mirror | 63616 | 307200 |
| B-08 | GPU-linked cornell | 182400 | 409600 |
| B-09 | RT-linked cornell | 284840 | 512000 |
| B-10 | denoise-linked | 59520 | 614400 |
| B-11 | upscale-glue-linked (path+denoise+upscale) | **71864** | **716800** (700 KiB) |

DCE: hello / unused-math / glass / GPU cornell omit `farm_upscale.c`; upscale-glue demo links `farm_upscale.c` + `-DFARM_ENABLE_UPSCALE`.

## TommyLaptop (tip `9b4a11f`)

Host: Windows x64, Intel Arc, 1 Vulkan device. Env dirty: ACE-Tray, DSATray, Steam++, Intel Graphics services. Watt Toolkit: git fetch OK.

### AC-M12-07 Windows PE — **PASS**

`scripts/check_size_budgets.py` Release MSVC:

| ID | Artifact | Bytes | Budget | Status |
|----|----------|-------|--------|--------|
| B-01 | hello | 13824 | 20480 | OK |
| B-03 | glass+mirror | 66048 | 307200 | OK |
| B-08 | GPU-linked | 183296 | 409600 | OK |
| B-09 | RT-linked | 288256 | 512000 | OK |
| B-10 | denoise-linked | 59392 | 614400 | OK |
| B-11 | upscale-glue-linked | **73216** | **716800** | OK |

### `ctest -C Release -R m12_`

8/8 Passed. 004/006 soft-skipped (`requires_vulkan` harness quirk; Arc has 1 Vulkan device). Would still skip for `# requires_xess` without DLL.

### AC-M12-04 / AC-M12-05 — **PENDING / BLOCKED**

**BLOCKER (verbatim): missing libxess.dll.**

- `FARMOS_XESS_LIB` unset
- `runtime/third_party/xess/` absent
- Searched Intel / Downloads / `D:\Tommy` / System32 — none
- `DLSS-Enabler-0.9.4.zip` in Downloads was **NOT USED** (OUT forever)

No Enabler / Swapper / OptiScaler workaround. XeSS smoke and timing cannot run until the official Intel `libxess.dll` is placed via `scripts/fetch_upscale_sdk.sh` or `FARMOS_XESS_LIB`.

## AC-M12-01..12

| ID | Status | Evidence |
|----|--------|----------|
| AC-M12-01 | **PASS** | `001_upscale_off_cpu_twin` SHA = M4-001 |
| AC-M12-02 | **PASS** | `002_upscale_mode_roundtrip` |
| AC-M12-03 | **PASS** | `003_trap_invalid_upscale` exit 122 `invalid path upscale mode` |
| AC-M12-04 | **PENDING / BLOCKED** | missing `libxess.dll` on TommyLaptop; Linux/Windows 004 skipped without SDK |
| AC-M12-05 | **PENDING / BLOCKED** | same blocker; SHOULD ≥1.5× after first CLEAN Arc tip with SDK |
| AC-M12-06 | **PASS (skip policy)** | `006_denoise_then_upscale` present; denoise-then-upscale order in `farm_Renderer_renderPath`; soft-skip without SDK |
| AC-M12-07 | **PASS** | Linux ELF + Windows PE B-11 **73216** / 716800; hello 13824; unused DCE |
| AC-M12-08 | **PASS** | No Enabler/Swapper/OptiScaler/DLSS/NGX/OptiX/frame-gen in runtime; Enabler zip on disk **not used**; artwork composition untouched |
| AC-M12-09 | **PASS** | `# requires_xess` / `# requires_vulkan` skip; OFF twins + traps + roundtrip green on Linux and Windows ctest |
| AC-M12-10 | **PASS** | M9 STRICT / M10 denoise suites unchanged; upscale STRICT is 122-family (`008`) |
| AC-M12-11 | **PASS** | M4 CPU SHA suite; jitter identity when OFF / on CPU |
| AC-M12-12 | **PASS** | `005_trap_invalid_resolution` exit 122 `invalid path upscale resolution` |

## Known gaps (not Enabler workarounds)

1. **missing libxess.dll** blocks AC-M12-04/05 on Arc. Obtain the official Intel XeSS SDK (`scripts/fetch_upscale_sdk.sh` / `FARMOS_XESS_LIB` / `runtime/third_party/xess/libxess.dll`). Do **not** ship DLSS-Enabler / Swapper / OptiScaler.
2. **FSR2 execute** is optional: setter + `isPathUpscaleAvailable` work; dispatch soft-fails until official ffx Vulkan symbols resolve. XeSS is the primary PASS path.
3. Windows `m12_004` / `m12_006` currently skip on `requires_vulkan` even with a device present (harness quirk). After `libxess.dll` lands they still need `# requires_xess` (and a vulkan probe fix if 004/006 should run on Arc).

## Product rules held

- Default OFF; M4/M10/M11 goldens under OFF.
- Headless buffer + `savePNG` only (M10 OQ-M10-01 stands).
- Vendor SDK DLLs external (not in PE cap). B-11 PE 73216 ≪ 700 KiB.
- Frame-gen OUT. Enabler zip on TommyLaptop was not used.
