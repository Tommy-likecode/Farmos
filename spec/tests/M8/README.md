# Farmos M8 path-tracer engine speedup test fixtures

Sources use the M1 TypeScript-style surface, M2 math, M3/M4 scene + path API,
M7 backend opt-in, and M8 speed controls (`setPathRoulette`, `setPathSampling`,
adaptive spp helpers). See `spec/M8-path-speed.md` (**FINAL**). Do not edit M1–M7
milestone bodies to absorb these rules; amendments live in `M8-path-speed.md`.

This file documents **M8-only** notes. It does not replace `spec/tests/README.md`,
the M4 README, or the M7 README. Harness kinds `run`, `runtime_trap`, `run_png`
keep their M1/M3/M4/M7 meanings. M7 headers `# requires_vulkan:`, `# png_tol:`,
`# env:` remain valid where used.

## Layout

```
spec/tests/M8/
  README.md
  NNN_name.fm
  NNN_name.expected
  bench/                     (wall-clock notes / scripts)
  _ref/
    README.md
    goldens_sha256.txt
```

CPU compat goldens reuse M4 SHA values (copied from M4/M7 twins). Speed-mode
fixtures do not claim M4 bit-identity.

## Working directory

The harness MUST run each M8 program with CWD = `spec/tests/M8`.
`savePNG("out.png")` writes `spec/tests/M8/out.png`.

## Compat vs speed

| Lane | RR | Sampling | Adaptive | Image contract |
|------|----|----------|----------|----------------|
| Compat (default) | off | PATH_SAMPLING_M4 | off | CPU SHA = M4; GPU `# png_tol: 2` |
| Speed | on and/or MIS | non-M4 | optional | Separate golden / tol / smoke only |

## Stub pack counts

**6** `.fm` fixtures (001–005, 007) + README + bench notes. Eggtooth size/DCE
and silver-still wall-clock benches are script ACs (see `spec/M8-path-speed.md`
AC-M8-10, AC-M8-12..14).

## Artwork bench

Normative program: `examples/artwork_silver_still.fm` @ 1920×1080 / 256 spp.
Composition is locked (`out/artwork_silver_still.png`). Do not change meshes,
lights, camera, or materials for speed. Bench scripts under
`spec/tests/M8/bench/` enable RR/MIS via env (`FARMOS_PATH_ROULETTE=1`,
`FARMOS_PATH_SAMPLING=1`) without editing the artwork.

## Environment variables

| Variable | Notes |
|----------|--------|
| `FARMOS_PATH_ROULETTE` | Optional `1` → RR on if program never called `setPathRoulette` |
| `FARMOS_PATH_SAMPLING` | Optional `1`/`2` → sampling mode if program never called `setPathSampling` (bench hook) |
| `FARMOS_PATH_GPU_STRICT` | M7 STRICT (unchanged) |
| `FARMOS_VULKAN_FORCE_FAIL` | M7 test hook (unchanged) |
| `FARMOS_THREADS` | CPU tile pool only (M4/M7) |
| `FARMOS_VULKAN_WORKGROUP` | Non-normative `WxH` (default baked SPIR-V is **8×8**) |

## Index

| Fixture | Kind | Notes |
|---------|------|-------|
| 001_compat_m4_background | run_png | M4-001 / M7-001 twin, real SHA |
| 002_roulette_off_golden | run_png | Explicit RR off; same SHA as 001 |
| 003_sampling_mode_roundtrip | run | 0 → 1 → 0 |
| 004_adaptive_api_nobreak | run | Adaptive OFF; two `renderPath`; flags |
| 005_trap_invalid_sampling | runtime_trap | `setPathSampling(99)` → 119 |
| 007_roulette_on_smoke | run | RR on; exit 0; no M4 SHA |
