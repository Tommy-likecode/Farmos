# Farmos M13 performance / more speed test fixtures

Sources use the M1 TypeScript-style surface, M2 math, M3/M4 scene + path API,
M7/M8/M9 backends, M10 denoise, M11 glossy, M12 upscale, and M13 performance
controls (env / internal hooks only; OQ-M13-04 A). See
`spec/M13-performance.md` (**FINAL** — OQ-M13-01..08 all **accepted A**).

**Status:** **FINAL** (PM/user 2026-10-26: OQ-M13-01..08 A). Binding ACs in
`M13-performance.md` §5. poteto **HOLD** until Projects Manager assigns coding.

M12 regression floors (cite; do not rewrite M12 fixtures here):

- OQ-M12-12 A: SHOULD ≥ **1.05×**; XeSS wall strictly < native (MUST)
- OQ-M12-13 A: ROI L **[80, 200]** + **|ΔL| of means ≤ 5.0**
- Pinned ROI: **`x=940:980,y=520:560`** @1920×1080 (**40×40**)
- mean_abs_dL / png_tol / max_abs: report-only
- Baseline Arc CLAIM product `d23acfb3`: Pass1 **1.264** / Pass2 **1.348** (**process-per-frame**; M12 floor meter — not the M13 SHOULD meter)
- M13 SHOULD meter: in-process `FARMOS_PATH_FRAME_MS` (CLAIM product `945c13ae` Pass1 **1.455** / Pass2 **1.607**)
- Master tip (clear-m12-backlog): `affc63ea`
- Pinned ROI CLAIM (current): L **105.810 / 105.801**, \|ΔL\| of means **0.009**, mean_abs_dL **0.440** report-only — not L~187 / 3.932

Binding M13 SHOULD (OQ-M13-01 A / OQ-M13-02 A): ≥ **1.20×** Pass1 **and** Pass2
after warm-up under in-process `FARMOS_PATH_FRAME_MS` (reclaims historical
M12 OQ-10 A ≥1.2× as an M13 gate; does not weaken the M12 ≥1.05× floor).
Do **not** cross-compare inproc ms to `d23acfb3` process-per-frame.

Do not edit M1–M12 milestone bodies to absorb these rules; amendments live
in `M13-performance.md`.

This file documents **M13-only** outline notes. It does not replace
`spec/tests/README.md` or prior milestone READMEs. Harness kinds `run`,
`runtime_trap`, `run_png` keep their M1/M3/M4/M7 meanings. Prior headers
`# requires_vulkan:`, `# requires_vulkan_rt:`, `# requires_xess:`,
`# png_tol:`, `# env:` remain valid.

## Layout

```
spec/tests/M13/
  README.md                 (this file — FINAL outline)
  bench/                    (optional Arc quality/timing notes)
  NNN_name.fm               (optional cheap stubs — MAY add if harness needs)
  NNN_name.expected
```

Prefer **citing M12 regression fixtures** (`spec/tests/M12/001_*` OFF twin,
trap, roundtrip, XeSS smoke) over duplicating them. New M13 `.fm` stubs MAY
appear after FINAL if poteto needs harness-local programs.

## Working directory

If M13-local programs exist, the harness MUST run them with
CWD = `spec/tests/M13`. Until then, Arc benches MAY reuse the M12 harness
with an M13 product tip SHA.

## Soft-skip (OQ-M13-08 A — binding)

| Header | When to soft-skip |
|--------|-------------------|
| `# requires_vulkan: true` | No usable Vulkan compute device |
| `# requires_vulkan_rt: true` | No usable Vulkan hardware RT |
| `# requires_xess: true` | Intel XeSS SDK / device support unavailable |

Linux CI without GPU/SDK: keep OFF CPU twins and API/trap fixtures green;
soft-skip XeSS Arc benches.

## Arc bench notes (outline)

- Primary machine: TommyLaptop Intel Arc (Vulkan RT + official XeSS).
- Scene: M12 bench continuity — **960×540 → 1920×1080**, glossy ON as M12 CLAIM.
- Binding primary meter: **in-process** `FARMOS_PATH_FRAME_MS` (warmup then
  median of timed samples).
- Report: native median, XeSS median, ratio Pass1 / Pass2, ROI L both lanes,
  binding |ΔL|, mean_abs_dL (report-only); process-per-frame M12 floor
  separately — **do not cross-compare meters**.
- Documented M12 process-per-frame baseline: product **`d23acfb3`** Pass1
  **1.264** / Pass2 **1.348**. M13 SHOULD CLAIM: `945c13ae` **1.455 / 1.607**
  inproc.
- Do not invent absolute ms caps until measured on a comparable harness.

## poteto HOLD

Coding, CLAIM sync, and fixture goldens for M13 MUST wait until Projects
Manager assigns after `spec/M13-performance.md` Status **FINAL** (OQ-M13-01..08 A locked 2026-10-26).
