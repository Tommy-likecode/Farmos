# M11 artwork ROI bench notes

**FINAL** — OQ-M11-01..08 all A. Run after poteto implements glossy continuation.

## Gates (binding)

| Mode | Scene | Metric |
|------|-------|--------|
| OFF | artwork_silver_still (rough 0.04, metal 1) | ball-center 40×40 mean L ≤ 5 |
| ON (`PATH_GLOSSY_VNDF`) | same | mean L ≥ 80 (order ~106.31 v1) |

Do **not** modify artwork lights/background/roughness for the product gate.
Stopgap roughness=0 is documentation-only (restores L=106.310 exactly).

Evidence baseline: CLAIM PASS tip `9c5eebd`.

Linux CI: CPU smoke + fixtures. Windows TommyLaptop: full 1920×1080 silver ROI
on CPU / Vulkan compute / Vulkan RT when a device exists.

`measure_roi.py` decodes an 8-bit RGB PNG and prints mean luminance of a crop.
