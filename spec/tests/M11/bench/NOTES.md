# M11 Linux CPU ROI evidence (cloud VM, 2026-10-20)

Tip: this PR. Machine: Linux cloud agent (no Windows, no Vulkan device).

## Fixture 004 (64×64, 16 spp, metal=1, rough=0.04, bg 0.42/0.45/0.50)

ROI: 8×8 about image center (`x=28:36, y=28:36`).

| Mode | mean L | mean RGB | Gate |
|------|--------|----------|------|
| OFF (`PATH_GLOSSY_OFF`) | **0.000** | (0,0,0) | AC black L ≤ 5 **PASS** |
| ON (`PATH_GLOSSY_VNDF`) | **106.310** | (99,107,121) | AC silver L ≥ 80 **PASS** |

ON center matches Oct-14 v1 / `setRoughness(0)` CLAIM numbers **exactly**
(L=106.310, RGB=99,107,121) while **keeping roughness 0.04**. Stopgap
`setRoughness(0)` is still **not** the product fix; this is the VNDF bounce.

Whole-frame OFF mean is higher because RectArea NEE fireflies remain; the
**ball-center** crop is the AC (CLAIM used 40×40 @ 1080p).

## 001 OFF twin

SHA `e7655a2c9a205f90786991219a3b558e27b2314377a4f5d4a66891b5b25915dd` (M4-001).

## Windows follow-up (parent / TommyLaptop)

Not measured here. Still needed:

- Artwork `examples/artwork_silver_still.fm` 1920×1080 @ ≥64 spp (256 preferred)
  ball-center 40×40 (`x=940:980,y=520:560`) on **CPU / VULKAN / VULKAN_RT**.
- Confirm ON L ≥ 80 and OFF L ≤ 5 on each available backend.
- PE size budgets on Windows x64 (hello ≤ 20 KiB; prior 300/400/500/600).
