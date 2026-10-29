# Farmos M14 performance / more speed test fixtures

Sources use the M1 TypeScript-style surface, M2 math, M3/M4 scene + path API,
M7/M8/M9 backends, M10 denoise, M11 glossy, M12 upscale, M13 in-process
`FARMOS_PATH_FRAME_MS` host amort, and M14 performance controls (env /
internal hooks only; OQ-M14-04 A). See
`spec/M14-performance.md` (**FINAL** — OQ-M14-01..08 all **accepted A**).

**Status:** **FINAL** (PM/user 2026-10-29: OQ-M14-01..08 A). Binding ACs in
`M14-performance.md` §5. poteto **HOLD** until Projects Manager assigns coding.

M12 / M13 regression floors (cite; do not rewrite those fixtures here):

- OQ-M12-12 A: process-per-frame SHOULD ≥ **1.05×**; XeSS wall strictly < native (MUST)
- OQ-M12-13 A: ROI L **[80, 200]** + **|ΔL| of means ≤ 5.0**
- Pinned ROI: **`x=940:980,y=520:560`** @1920×1080 (**40×40**)
- mean_abs_dL / png_tol / max_abs: report-only
- M13 SHOULD floor: in-process `FARMOS_PATH_FRAME_MS` ≥ **1.20×** Pass1 **and** Pass2
- Product baseline to beat: `945c13ae` Pass1 **1.455** / Pass2 **1.607** (inproc)
- Master tip (M14 squash PR #23): `218f6ef1` (product `4d7523ec`; docs lineage `6d1d9c81`)
- Inproc `FRAME_MS` is GPU-pipeline (H2: XeSS 1080p host readback deferred to `savePNG`). Do not claim equal-work vs `945c13ae`.

Binding M14 SHOULD (OQ-M14-01 A / OQ-M14-02 A): ≥ **1.50×**
Pass1 **and** Pass2 after warm-up under the same inproc meter (does not
weaken M13 ≥1.20× or M12 ≥1.05× floors).

Do not edit M1–M13 milestone bodies to absorb these rules; amendments live
in `M14-performance.md`.

This file documents **M14-only** outline notes. It does not replace
`spec/tests/README.md` or prior milestone READMEs. Harness kinds `run`,
`runtime_trap`, `run_png` keep their M1/M3/M4/M7 meanings. Prior headers
`# requires_vulkan:`, `# requires_vulkan_rt:`, `# requires_xess:`,
`# png_tol:`, `# env:` remain valid.

## Layout

```
spec/tests/M14/
  README.md                 (this file — FINAL outline)
  bench/                    (optional Arc quality/timing notes)
  NNN_name.fm               (optional cheap stubs — MAY add after FINAL)
  NNN_name.expected
```

Prefer **citing M12/M13 regression fixtures and the M13 inproc harness**
over duplicating them. New M14 `.fm` stubs MAY appear after FINAL if
poteto needs harness-local programs.

## Working directory

If M14-local programs exist, the harness MUST run them with
CWD = `spec/tests/M14`. Until then, Arc benches MAY reuse the M13 harness
with an M14 product tip SHA.

## Soft-skip (OQ-M14-08 A — binding)

| Header | When to soft-skip |
|--------|-------------------|
| `# requires_vulkan: true` | No usable Vulkan compute device |
| `# requires_vulkan_rt: true` | No usable Vulkan hardware RT |
| `# requires_xess: true` | Intel XeSS SDK / device support unavailable |

Linux CI without GPU/SDK: keep OFF CPU twins and API/trap fixtures green;
soft-skip XeSS Arc benches.

## Arc bench notes (outline)

- Primary machine: TommyLaptop Intel Arc (Vulkan RT + official XeSS).
- Scene: M12/M13 bench continuity — **960×540 → 1920×1080**, glossy ON.
- Binding primary meter: **in-process** `FARMOS_PATH_FRAME_MS` (warmup then
  median of timed samples; GPU-pipeline after H2 — XeSS host RGB flush is
  on `savePNG`, not inside timed `renderPath`).
- Report: native median, XeSS median, ratio Pass1 / Pass2, ROI L both lanes,
  binding |ΔL|, mean_abs_dL (report-only); also report process-per-frame
  M12 floor separately — **do not cross-compare meters**. Do **not** claim
  equal-work vs `945c13ae` (that CLAIM included XeSS host readback in
  `FRAME_MS`). No flush-every-frame secondary inproc meter (proc AC-05
  already includes `savePNG`).
- Baseline tip to document: product **`945c13ae`** Pass1 **1.455** /
  Pass2 **1.607** (equal-work M13 inproc). Measured M14 inproc **3.184 /
  3.913** includes H2 meter-scope change.
- Binding SHOULD (OQ-01/02 A): ≥ **1.50×** both passes. MUST NOT weaken.
- Process-per-frame Pass2 **1.139** (~8.5% over 1.05): monitor only.
- Dirty chrome+msedge during CLAIM: informational.
- Do not invent absolute ms caps until measured on a comparable harness.

## poteto HOLD

Arc CLAIM **PASS** at measured tip `d409152e` / product `4d7523ec`.
This file tracks post-squash backlog (H2 meter note, `m14_` ctest, Pass2
proc headroom, dirty-host). Docs+CMake only; product **unchanged**. Do not
merge from this CloudAgent run. No new Arc CLAIM.
