# M9 silver-still HW RT bench notes

**Status: tip `eaeb0c6` AS/SSBO persist CLEAN — AC-M9-13/14 PASS under OQ-M9-08 A (≤5000 ms / ≥5×). RT median 4881 ms / compute 29335 ms / 6.010×. Claim tip for eggtooth. Do not invent new Windows numbers. Dropped: `1003468` BLAS compact (REGRESS 6067 > 4881).**

This Linux CI / cloud-agent host does not provide a hardware ray-tracing device
(`VK_KHR_ray_query` + acceleration structures). Windows numbers below are
TommyLaptop / poteto measurements only.

## Target (binding OQ-M9-08 A — PM 2026-10-18)

- Scene: `examples/artwork_silver_still.fm` (composition locked)
- 1920×1080 / 256 spp, RR+MIS, STRICT HW RT (`PATH_BACKEND_VULKAN_RT`,
  `FARMOS_PATH_GPU_STRICT=1`, no downgrade)
- AC-M9-13: median ≤ **5000 ms** after 1 warm-up (**binding**; prior ≤4000 superseded)
- AC-M9-14: ≥ **5×** vs post-M8 compute when measurable (**binding**; prior ≥8× superseded)
- Spec: `spec/M9-hardware-rt.md` FINAL (OQ-M9-01..08 all A)

## TommyLaptop measured rows (do not invent numbers)

| Tip | Compute median | RT median (STRICT `vulkan_rt`) | Ratio | Cold |
|-----|----------------|--------------------------------|-------|------|
| `061ebe62` (dirty) | 33699 ms | **8773 ms** (8792, 8711, 8773) | 3.841× | 76497 |
| `754b6a5` | 42986 ms | **11697 ms** (11611, 11697, 11732) | 3.675× | 12739 |
| `5127765` | 47381 ms | **9773 ms** (10224, 9636, 9773) | 4.848× | 10729 |
| `d91887b` dirty | 34281 ms | **10123 ms** (game contention) | 3.386× | 9128 |
| `d91887b` **CLEAN** | **28218 ms** (26810, 29985, 28218) | **4953 ms** (4953, 4996, 4950) | **5.697×** | 5356 |
| `eaeb0c6` AS persist CLEAN | **29335 ms** (25863, 29335, 384095*) | **4881 ms** (4881, 4678, 6724) | **6.010×** | 5287 |
| `1003468` BLAS compact CLEAN (**dropped**) | **353747 ms**† | **6067 ms** (6067, 5056, 6077) | inflated† | 36439 |

\*compute run3 outlier 384095 ms (machine noise); median of three used. RT run3 6724 also elevated; median 4881 still ≤4953.
†`1003468` compute lane contaminated/noisy; RT median 6067 > floor 4881 — **REGRESS**; do not claim.

`d91887b` runtime is bit-identical to `061ebe62`. Dirty 10123 was GPU contention — ignore it.

Dropped dead ends: `754b6a5` (8×4×4), `5127765` (thin-SPIR-V), `1003468` (BLAS compact). Claim tip: **`eaeb0c6`**.

### Detail (`eaeb0c6` CLEAN — claim tip)

Games closed. Confirmed absent before compute and before HW RT: DeltaForceClient, Steam, steamwebhelper.

| Field | Value |
|-------|--------|
| Host | TommyLaptop |
| GPU | Intel(R) Arc(TM) Graphics |
| farmc SHA | `eaeb0c6d00297e62658d71c0fef34806edd83dd3` |
| PE sizes | hello **13824**; glass **61952**; compute cornell **159232**; RT cornell **242176** — **PASS** (B-01/02/08/09; AC-M9-10) |
| Compute median | **29335** (25863, 29335, 384095*); stdout `vulkan`; cold 26564 |
| HW RT median | **4881** (4881, 4678, 6724); cold 5287 |
| stdout | `vulkan_rt`; STRICT exit 0; downgrade **no** |
| AC-M9-13 | **PASS** (4881 ≤ 5000) |
| AC-M9-14 | **PASS** (6.010× ≥ 5×) |
| Artifacts | `out/m9_compute_baseline_eaeb0c6.txt`, `out/m9_vulkan_rt_speed_eaeb0c6.txt`, `out/m9_compute_bench_eaeb0c6/`, `out/m9_rt_bench_eaeb0c6/` |

Lever class: host BLAS/TLAS + SSBO persist across `renderPath` when geometry fingerprint matches. Kernel / SPIR-V / 8×8 unchanged vs `d91887b`.

Method: median of 3 after 1 warm-up; `FARMOS_PATH_ROULETTE=1 FARMOS_PATH_SAMPLING=1`.

`farmc version`: `farmc 1.01`.

Stack: Vulkan AS + `VK_KHR_ray_query` inside compute. Not OptiX / full RT pipeline.

### Notes on prior AC

Under superseded OQ-M9-04 A (≤4000 / ≥8×) this same tip was FAIL. Under binding OQ-M9-08 A (≤5000 / ≥5×) it **PASSES**. No re-bench required for this claim; numbers unchanged.
