# Farmos M12 native Vulkan upscale test fixtures

Sources use the M1 TypeScript-style surface, M2 math, M3/M4 scene + path API,
M7/M8/M9 backends, M10 denoise, M11 glossy, and M12 upscale controls. See
`spec/M12-native-upscale.md` (**FINAL** — OQ-M12-01..09 A; OQ-10/11 A historical; **OQ-M12-12 A + OQ-M12-13 A** binding). Do not edit
M1–M11 milestone bodies to absorb these rules; amendments live in
`M12-native-upscale.md`.

This file documents **M12-only** notes. It does not replace `spec/tests/README.md`
or prior milestone READMEs. Harness kinds `run`, `runtime_trap`, `run_png` keep
their M1/M3/M4/M7 meanings. Prior headers `# requires_vulkan:`,
`# requires_vulkan_rt:`, `# png_tol:`, `# env:`, `# path_mode:` remain valid.

## Layout

```
spec/tests/M12/
  README.md
  NNN_name.fm
  NNN_name.expected
  bench/                     (Arc quality + timing notes)
  _ref/
    README.md
```

CPU compat goldens (upscale OFF) reuse M4 SHA values. Upscale-ON fixtures use
`# requires_xess:` / `# requires_fsr:` soft-skip and `# png_tol:` when they run.

## Working directory

The harness MUST run each M12 program with CWD = `spec/tests/M12`.
`savePNG("out.png")` writes `spec/tests/M12/out.png`.

## Upscale modes (OQ-M12-02 A / OQ-M12-04 A)

| Id | Constant | Notes |
|----|----------|-------|
| 0 | `PATH_UPSCALE_OFF` | Default; M4 SHA on CPU; jitter identity |
| 1 | `PATH_UPSCALE_XESS` | Native Intel XeSS Vulkan (primary) |
| 2 | `PATH_UPSCALE_FSR` | Native AMD FSR2 Vulkan (optional) |
| other | — | trap **122** at `setPathUpscale` |

Invalid `setPathUpscaleResolution` (any dim < 1, or display < render) traps
**122** with `runtime error: invalid path upscale resolution`.

## Soft-skip

| Header | When to soft-skip |
|--------|-------------------|
| `# requires_vulkan: true` | No usable Vulkan **compute** device (M7) |
| `# requires_vulkan_rt: true` | No usable Vulkan **hardware RT** (M9) |
| `# requires_xess: true` | Intel XeSS SDK (`libxess`) or Vulkan device unavailable |
| `# requires_fsr: true` | FSR2 SDK shared lib or Vulkan device unavailable |

Soft-skip: do **not** fail; log skip; runner exit 0 for that case.

Linux CI without SDK: keep OFF twins, roundtrip, trap, CPU soft-fail, and
STRICT FORCE_FAIL green; soft-skip XeSS/FSR smokes.

## Environment variables

| Variable | Notes |
|----------|--------|
| `FARMOS_PATH_UPSCALE` | Optional hook: `1` → XeSS if setter never called; method wins |
| `FARMOS_PATH_UPSCALE_STRICT` | `1` → upscale failure traps (exit 122, stderr `path upscale failed`) |
| `FARMOS_PATH_UPSCALE_FORCE_FAIL` | Treat upscale as failed (test hook) |
| `FARMOS_XESS_LIB` | Optional absolute path to `libxess` / `libxess.dll` |
| `FARMOS_FSR_LIB` | Optional absolute path to FSR2 Vulkan shared lib |
| `FARMOS_PATH_GPU_STRICT` | M7/M9 STRICT (unchanged) |

## Pack counts

Pack: **8** `.fm` fixtures (001–008) + README + `_ref` + bench note.
001–004 match the spec stub; 005–008 cover AC-M12-06/10/12 on Linux without SDK.

## Artwork / Arc bench

- Scene: `examples/artwork_silver_still.fm` composition **locked**.
- Smoke: render **960×540** → display **1920×1080**, XeSS ON, prefer HW RT.
- Timing vs native 1080p same spp: binding XeSS wall < native; SHOULD ≥ **1.05×**
  (OQ-M12-12 A; historical ≥1.2× / ≥1.5× superseded). Quality: ROI L ∈ **[80, 200]**
  + |ΔL| ≤ 5.0; png_tol / max_abs report-only (OQ-M12-13 A). Scripts under
  `spec/tests/M12/bench/`.
