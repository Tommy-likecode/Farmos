# Farmos M12 native Vulkan upscale test fixtures

Sources use the M1 TypeScript-style surface, M2 math, M3/M4 scene + path API,
M7/M8/M9 backends, M10 denoise, M11 glossy, and M12 upscale controls. See
`spec/M12-native-upscale.md` (**FINAL** — OQ-M12-01..13 all A; PM/user
2026-10-23: OQ-12 A + OQ-13 A; **2026-10-25 clarification** clear-m12-backlog).
Binding AC-M12-04 / AC-M12-05 under OQ-13 A / OQ-12 A: SHOULD ≥ **1.05×**;
ROI **[80, 200]** + |ΔL| ≤ 5.0; XeSS wall strictly < native; png_tol /
max_abs / mean_abs_dL report-only. Gates unchanged by clarification.
Do not edit M1–M11 milestone bodies to absorb these rules; amendments live
in `M12-native-upscale.md`.

This file documents **M12-only** notes. It does not replace `spec/tests/README.md`
or prior milestone READMEs. Harness kinds `run`, `runtime_trap`, `run_png` keep
their M1/M3/M4/M7 meanings. Prior headers `# requires_vulkan:`,
`# requires_vulkan_rt:`, `# png_tol:`, `# env:`, `# path_mode:` remain valid.
M12 MAY add `# requires_xess:` and `# requires_fsr:`.

## Layout

```
spec/tests/M12/
  README.md
  NNN_name.fm
  NNN_name.expected          (placeholders OK until poteto regenerates SHAs)
  bench/                     (Arc quality/timing notes; optional)
  _ref/
    README.md
```

CPU compat goldens (upscale OFF) SHOULD reuse M4 SHA values (copy from M4/M7/M8/M9/M10/M11
twins). Upscale-ON fixtures use `# png_tol:` / smoke-only until goldens exist.

## Working directory

The harness MUST run each M12 program with CWD = `spec/tests/M12`.
`savePNG("out.png")` writes `spec/tests/M12/out.png` (display resolution when upscale ON).

## Upscale modes (OQ-M12-02 A binding; milestone Status FINAL)

| Id | Constant | Notes |
|----|----------|-------|
| 0 | `PATH_UPSCALE_OFF` | Default; M4 SHA on CPU |
| 1 | `PATH_UPSCALE_XESS` | Native Intel XeSS (Vulkan); primary on Arc |
| 2 | `PATH_UPSCALE_FSR` | Native AMD FSR2 (Vulkan); optional |
| other | — | trap **122** at `setPathUpscale` |

## Soft-skip

| Header | When to soft-skip |
|--------|-------------------|
| `# requires_vulkan: true` | No usable Vulkan **compute** device (M7) |
| `# requires_vulkan_rt: true` | No usable Vulkan **hardware RT** (M9) |
| `# requires_xess: true` | Intel XeSS SDK / device support unavailable |
| `# requires_fsr: true` | AMD FSR2 SDK / device support unavailable |

Soft-skip: do **not** fail; log skip; runner exit 0 for that case.

Linux CI without GPU/SDK: keep upscale-OFF CPU twins and API/trap fixtures green;
soft-skip XeSS/FSR smokes and Arc benches.

## Environment variables

| Variable | Notes |
|----------|--------|
| `FARMOS_PATH_UPSCALE_STRICT` | `1` → upscale failure traps (exit 122, stderr `path upscale failed`) |
| `FARMOS_PATH_UPSCALE_FORCE_FAIL` | Recommended test hook: treat upscale as failed |
| `FARMOS_PATH_UPSCALE` | Optional draft hook: `1` → XeSS if setter never called; method wins |
| `FARMOS_PATH_GPU_STRICT` | M7/M9 STRICT (unchanged) |
| `FARMOS_PATH_DENOISE_STRICT` | M10 (unchanged) |
| `FARMOS_VULKAN_FORCE_FAIL` / `FARMOS_VULKAN_RT_FORCE_FAIL` | M7/M9 hooks (unchanged) |

## Stub pack counts

Stub pack: **4** `.fm` fixtures (001–004) + README + `_ref` + bench note
(spec **FINAL**; OQ-01..13 all A).
Eggtooth size/DCE and Arc quality/timing are script ACs (see
`spec/M12-native-upscale.md` AC-M12-04, AC-M12-05, AC-M12-07 — **04/05
binding** under OQ-13 A / OQ-12 A).

## Arc bench (AC-04/05 binding under OQ-13 A / OQ-12 A)

- Prefer TommyLaptop Arc + `PATH_BACKEND_VULKAN_RT` + `PATH_UPSCALE_XESS`.
- Native: 1920×1080 upscale OFF.
- Upscale: render 960×540 → display 1920×1080.
- Headless savePNG only under OQ-M12-01 A (no window; M10-01 stands).
- Scripts under `spec/tests/M12/bench/` or `scripts/` as poteto prefers after assign.
- Do **not** use DLSS-Enabler / Swapper / OptiScaler.

## Binding silver ROI (AC-04; align M11 CLAIM / M11 §8.2)

CLAIM / AUDIT MUST use the **same** box (do **not** float center±80 or centroid):

| Field | Binding |
|-------|---------|
| Display | **1920×1080** |
| Size | **40×40** |
| Box (CLAIM notation) | **`x=940:980,y=520:560`** (half-open; inclusive start, exclusive end) |
| Lanes | Same box for native and XeSS after warm-up |

## Metric naming (CLAIM / AUDIT)

| Symbol | Meaning | Gate |
|--------|---------|------|
| **L** | Mean luminance over pinned ROI (same L as M11 CLAIM harness) | both lanes ∈ **[80, 200]** |
| **\|ΔL\|** | **\|L_xess − L_native\|** of the two ROI **means** | binding ≤ **5.0** |
| **mean_abs** | Channel / full-frame mean abs vs native (tip ~0.465) | report-only |
| **mean_abs_dL** | ROI per-pixel abs mean of L diffs (tip **3.932** was mislabeled \|ΔL\|) | report-only — MUST NOT label as \|ΔL\| |
| max_abs / `# png_tol: 12` | unchanged | report-only |

Tip `7f0a080` / product `fd31e46`: L **187.228** / **187.877**; binding \|ΔL\| ≈ **0.650** PASS; **3.932** = mean_abs_dL.

## Binding ACs (OQ-01..13 A FINAL)

| Gate | Binding |
|------|---------|
| Present | Headless buffer + `savePNG`; **no** OS window (OQ-01 A; M10-01 stands) |
| Stack | XeSS primary; FSR2 optional; DLSS/Enabler/Swapper/OptiScaler OUT (OQ-02/09 A) |
| Aux | Camera MV + depth; object MV deferred (OQ-03 A) |
| API | `setPathUpscale` / `PATH_UPSCALE_*`; default OFF; trap **122** (OQ-04 A) |
| Order | Denoise then upscale when both ON (OQ-05 A) |
| Backends | vk + vk_rt MUST; CPU soft; STRICT 122-family (OQ-06 A) |
| Size | Upscale-glue PE ≤ **700 KiB**; vendor DLLs external (OQ-07 A) |
| Smoke | Arc 960×540→1920×1080; Linux `# requires_xess` / `# requires_fsr` soft-skip (OQ-08 A) |
| Quality (AC-04) | **Binding OQ-M12-13 A:** pinned ROI `x=940:980,y=520:560` @1080p (40×40); L **[80, 200]** + **|ΔL| = |L_xess−L_native|** ≤ 5.0. **Current pinned CLAIM** (`d23acfb3`+): L **105.810 / 105.801**, \|ΔL\| of means **0.009**, mean_abs_dL **0.440** report-only. Lock-time `7f0a080` / `fd31e46` L ~**187** / \|ΔL\| ≈ **0.650** / mean_abs_dL **3.932** are **historical** (see metric table above) — do **not** cite them as the current pinned CLAIM. png_tol / max_abs report-only; historical [80,140] (OQ-11 A) superseded for absolute band only |
| Speedup (AC-05) | **Binding OQ-M12-12 A:** SHOULD ≥ **1.05×**; XeSS wall **strictly <** native; tip `7f0a080` / product `fd31e46` Pass1 **1.052** / Pass2 **1.162**; historical tip `edeb845` **1.237**; historical SHOULD ≥1.2× (OQ-10 A) superseded for SHOULD floor only |

## PLACEHOLDER goldens

```
# sha256: 0000000000000000000000000000000000000000000000000000000000000000
```

Replace with real M4 twin SHAs for 001 when wiring eggtooth.
