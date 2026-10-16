# M8 reference notes

CPU `run_png` compat fixtures twin M4 scenes and reuse SHA-256 values from
`spec/tests/M4/_ref/goldens_sha256.txt`.

Speed-mode fixtures (RR on, MIS, adaptive) MUST NOT reuse M4 SHA as a
bit-identity gate.

This directory does **not** contain product code.

## RNG slot (AC-M8-05)

When Russian roulette is **off**, Hammersley / `rand01` dimensions match M4:

- Bounce `depth` base = `depth * 16`
- BSDF / glass: `base + 0` (and `base + 1` for cosine)
- RectArea k: `base + 2 + 2*k`, `base + 3 + 2*k`

When RR is **on**, one extra draw per candidate bounce uses **`base + 15`**.
That slot is unused in the M4 estimator, so RR-off paths stay bit-identical.

RR policy (when enabled): start depth ≥ 3; skip RR after delta glass/mirror
events (those continue to `maxBounces` or absorption); `q = clamp(luma_max, 0.05, 0.95)`;
terminate if `u >= q`, else divide throughput by `q`. Hard cap remains
`maxBounces`.

## Sampling pdf / estimator (AC-M8-07)

| Mode | RectArea sample | MIS |
|------|-----------------|-----|
| `PATH_SAMPLING_M4` (0) | Uniform area, pdf `1/area` (M4) | none |
| `PATH_SAMPLING_MIS` (1) | Angular (atan) warp toward the shading point in light-local XY; pdf is the product of 1D angular densities on the plane | Balance heuristic between cosine BSDF (`cos θ / π`) and RectArea light pdf in direction measure. Applied to the **diffuse** NEE term and to BSDF rays that hit a rect. GGX spec NEE stays weight 1 (no GGX BSDF sample in the bounce). Point / directional / ambient NEE unchanged. |
| `PATH_SAMPLING_AREA_SOLID` (2) | Same angular RectArea sample as MIS | no BSDF-light MIS |

Invalid / unimplemented modes trap **119** at `setPathSampling`. Mode 2 is implemented.

## Adaptive variance proxy (AC-M8-08)

Default **off**. When on, `samples` is the per-call maximum. Per pixel, Welford
variance of sample luminance; stop after at least 4 samples if
`M2/k < getPathAdaptiveTarget()`. Default target `1e-4` (clamp, no trap).
Display mean uses per-pixel sample counts when adaptive has run.
