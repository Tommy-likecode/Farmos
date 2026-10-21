# M12 CLAIM — Native Vulkan Upscale (XeSS / FSR)

**Verdict:** **PASS (Linux CI)** / **PENDING (TommyLaptop Arc + Windows PE)**  
**Do not merge** until eggtooth AUDIT PASS via Projects Manager.

| Field | Value |
|-------|--------|
| PR | https://github.com/Tommy-likecode/Farmos/pull/20 |
| Branch | `cursor/m12-native-upscale-f17d` |
| Base | `4a661e59` (M11 merged) |
| Implementation tip | `2025473` (`M12: native Vulkan upscale (XeSS / FSR)`) |
| Spec | `spec/M12-native-upscale.md` FINAL (OQ-M12-01..09 all A) |
| Date | 2026-10-21 |

## Linux verification (this agent)

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

**Windows stripped PE sizes were not measured on this host** (eggtooth / TommyLaptop).

## AC-M12-01..12

| ID | Status | Evidence |
|----|--------|----------|
| AC-M12-01 | **PASS** | `001_upscale_off_cpu_twin` SHA = M4-001 |
| AC-M12-02 | **PASS** | `002_upscale_mode_roundtrip` |
| AC-M12-03 | **PASS** | `003_trap_invalid_upscale` exit 122 `invalid path upscale mode` |
| AC-M12-04 | **PENDING** | Arc 960×540→1080p XeSS smoke; Linux 004 skipped (no Vulkan/SDK) |
| AC-M12-05 | **PENDING** | Arc timing vs native 1080p; SHOULD ≥1.5× after first CLEAN tip |
| AC-M12-06 | **PASS (Linux skip policy)** | `006_denoise_then_upscale` present; denoise-then-upscale order in `farm_Renderer_renderPath`; soft-skip without SDK |
| AC-M12-07 | **PASS (ELF + DCE)** / **PENDING (Windows PE)** | hello 14 KiB; B-11 71864 ≪ 700 KiB; unused DCE |
| AC-M12-08 | **PASS** | No Enabler/Swapper/OptiScaler/DLSS/NGX/OptiX/frame-gen in runtime; artwork composition untouched |
| AC-M12-09 | **PASS** | `# requires_xess` / `# requires_vulkan` skip; OFF twins + traps + roundtrip green |
| AC-M12-10 | **PASS** | M9 STRICT / M10 denoise suites unchanged; upscale STRICT is 122-family (`008`) |
| AC-M12-11 | **PASS** | M4 CPU SHA suite; jitter identity when OFF / on CPU |
| AC-M12-12 | **PASS** | `005_trap_invalid_resolution` exit 122 `invalid path upscale resolution` |

## Known gaps (not Enabler workarounds)

1. **Arc XeSS interop** (AC-M12-04/05) needs TommyLaptop + official `libxess.dll` via `scripts/fetch_upscale_sdk.sh` / `FARMOS_XESS_LIB`. If SDK init fails, report the XeSS/Vulkan blocker — do **not** ship DLSS-Enabler / Swapper / OptiScaler.
2. **FSR2 execute** is optional: setter + `isPathUpscaleAvailable` work; dispatch soft-fails until official ffx Vulkan symbols resolve. XeSS is the primary PASS path.
3. **Windows PE B-11** not measured here.

## Product rules held

- Default OFF; M4/M10/M11 goldens under OFF.
- Headless buffer + `savePNG` only (M10 OQ-M10-01 stands).
- Vendor SDK DLLs external (not in PE cap).
- Frame-gen OUT.
