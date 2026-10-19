# Farmos M10 denoise / interactive path test fixtures

Sources use the M1 TypeScript-style surface, M2 math, M3/M4 scene + path API,
M7/M8/M9 backends, and M10 denoise + progressive preview controls. See
`spec/M10-denoise-interactive.md` (**FINAL** — OQ-M10-01..08 all A). Do not edit
M1–M9 milestone bodies to absorb these rules; amendments live in
`M10-denoise-interactive.md`.

This file documents **M10-only** notes. It does not replace `spec/tests/README.md`
or the M4/M7/M8/M9 READMEs. Harness kinds `run`, `runtime_trap`, `run_png` keep
their M1/M3/M4/M7 meanings. Prior headers `# requires_vulkan:`,
`# requires_vulkan_rt:`, `# png_tol:`, `# env:`, `# path_mode:` remain valid.
M10 MAY add `# requires_denoise:`.

## Layout

```
spec/tests/M10/
  README.md
  NNN_name.fm
  NNN_name.expected          (placeholders OK until poteto regenerates SHAs)
  bench/                     (wall-clock / progressive notes; optional)
  _ref/
    README.md
```

CPU compat goldens (denoise OFF) SHOULD reuse M4 SHA values (copy from M4/M7/M8/M9
twins). Denoise-ON fixtures use `# png_tol:` (stills gate recommend `8` vs
256-spp undenoised reference) or smoke-only until goldens exist.

## Working directory

The harness MUST run each M10 program with CWD = `spec/tests/M10`.
`savePNG("out.png")` writes `spec/tests/M10/out.png`.

## Denoise modes (OQ-M10-02 A / OQ-M10-05 A)

| Id | Constant | Notes |
|----|----------|-------|
| 0 | `PATH_DENOISE_OFF` | Default; M4 SHA on CPU |
| 1 | `PATH_DENOISE_SPATIAL` | Farmos-owned spatial (A-trous / bilateral class) |
| other | — | trap **120** at `setPathDenoise` |

## Soft-skip

| Header | When to soft-skip |
|--------|-------------------|
| `# requires_vulkan: true` | No usable Vulkan **compute** device (M7) |
| `# requires_vulkan_rt: true` | No usable Vulkan **hardware RT** (M9) |
| `# requires_denoise: true` | Denoise implementation unavailable (optional; under OQ-M10-02 A, CPU spatial SHOULD usually run without this) |

Soft-skip: do **not** fail; log skip; runner exit 0 for that case.

Linux CI without GPU: keep denoise-OFF CPU twins and API/trap fixtures green;
soft-skip GPU/RT benches and any fixture that truly needs GPU denoise shaders.

## Environment variables

| Variable | Notes |
|----------|--------|
| `FARMOS_PATH_DENOISE_STRICT` | `1` → denoise failure traps (recommend exit 120, stderr `path denoise failed`) |
| `FARMOS_PATH_DENOISE_FORCE_FAIL` | Recommended test hook: treat denoise as failed |
| `FARMOS_PATH_GPU_STRICT` | M7/M9 STRICT (unchanged) |
| `FARMOS_VULKAN_FORCE_FAIL` / `FARMOS_VULKAN_RT_FORCE_FAIL` | M7/M9 hooks (unchanged) |

## Stub pack counts

Harness pack: **6** `.fm` fixtures (001–004 stubs + 005 STRICT fail + 006 preview
API). Eggtooth size/DCE, silver-still denoise wall-clock, and progressive timing
are script ACs (see `spec/M10-denoise-interactive.md` AC-M10-05, AC-M10-06,
AC-M10-09).

## Artwork / interactive benches

- **Stills denoise:** `examples/artwork_silver_still.fm` @ 1920×1080, **8 spp**
  (≤ **64**), `PATH_DENOISE_SPATIAL`, prefer HW RT. Composition locked. Gate
  (OQ-M10-03 A): median ≤ **1500 ms** after warm-up; visual `# png_tol: 8` vs
  256-spp undenoised.
  Baseline undenoised M9 floor **4881 ms**.
- **Progressive:** first useful ≤ **250 ms**; step ≤ **100 ms**; no OS window
  (OQ-M10-01 A). Scripts under `spec/tests/M10/bench/`.

## Goldens

001 reuses the M4-001 / M7-001 / M8-001 / M9-001 twin SHA
`e7655a2c9a205f90786991219a3b558e27b2314377a4f5d4a66891b5b25915dd`.
004 is a 32×32 background smoke with `# png_tol: 8` vs a solid `0x4488ff` reference.
