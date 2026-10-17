# M9 silver-still HW RT bench notes

**Status: tip `eaeb0c6` AS/SSBO persist re-bench FAIL vs ≤4000 / ≥8×. RT median **4881 ms** (≤4953 floor, no regress). AC-M9-13/14 FAIL. Do not invent new Windows numbers.**

This Linux CI / cloud-agent host does not provide a hardware ray-tracing device
(`VK_KHR_ray_query` + acceleration structures). Windows numbers below are
TommyLaptop / poteto measurements only.

## Target (PM unchanged)

- Scene: `examples/artwork_silver_still.fm` (composition locked)
- 1920×1080 / 256 spp, RR+MIS, STRICT HW RT (`PATH_BACKEND_VULKAN_RT`,
  `FARMOS_PATH_GPU_STRICT=1`, no downgrade)
- AC-M9-13: median ≤ **4000 ms** after 1 warm-up
- AC-M9-14: ≥ **8×** vs post-M8 compute
- Need ~**1.22×** from tip `eaeb0c6` RT median **4881 ms** to hit ≤4000

## TommyLaptop measured rows (do not invent numbers)

| Tip | Compute median | RT median (STRICT `vulkan_rt`) | Ratio | Cold |
|-----|----------------|--------------------------------|-------|------|
| `061ebe62` (dirty) | 33699 ms | **8773 ms** (8792, 8711, 8773) | 3.841× | 76497 |
| `754b6a5` | 42986 ms | **11697 ms** (11611, 11697, 11732) | 3.675× | 12739 |
| `5127765` | 47381 ms | **9773 ms** (10224, 9636, 9773) | 4.848× | 10729 |
| `d91887b` dirty | 34281 ms | **10123 ms** (game contention) | 3.386× | 9128 |
| `d91887b` **CLEAN** | **28218 ms** (26810, 29985, 28218) | **4953 ms** (4953, 4996, 4950) | **5.697×** | 5356 |
| `eaeb0c6` AS persist CLEAN | **29335 ms** (25863, 29335, 384095*) | **4881 ms** (4881, 4678, 6724) | **6.010×** | 5287 |

\*compute run3 outlier 384095 ms (machine noise); median of three used. RT run3 6724 also elevated; median 4881 still ≤4953.

`d91887b` runtime is bit-identical to `061ebe62`. Dirty 10123 was GPU contention — ignore it. Floor before this lever: **4953 ms**. `eaeb0c6` must not exceed 4953; absolute never exceed old 8773.

`754b6a5` / `5127765` remain dropped dead ends. Do not reintroduce 8×4×4 or the 5127765 shader stack.

### Detail (`d91887b` CLEAN — prior floor)

Games closed. Confirmed absent: DeltaForceClient, Steam, steamwebhelper, df_launcher, gameoverlay. Present non-contending: Steam++ (Watt Toolkit), GameBarPresenceWriter, GameInputRedistService.

| Field | Value |
|-------|--------|
| Host | TommyLaptop |
| GPU | Intel(R) Arc(TM) Graphics |
| farmc SHA | `d91887b350b86ed7c5f09c18f48b4d3f95988a6d` |
| PE sizes (prior AC-M9-10) | hello **13824**; glass **61952**; compute cornell **159232**; RT **235520** — **PASS** |
| Compute median | **28218** (26810, 29985, 28218); stdout `vulkan` |
| HW RT median | **4953** (4953, 4996, 4950); cold 5356 |
| stdout | `vulkan_rt`; STRICT exit 0; downgrade **no** |
| vs 8773 floor | **≤8773-class** (4953 << 8773) |
| AC-M9-13 / 14 | **FAIL** (4953 > 4000; 5.697× < 8×) |
| Artifacts | `out/m9_compute_baseline_clean.txt`, `out/m9_vulkan_rt_speed_clean.txt` |

Method: median of 3 after 1 warm-up; `FARMOS_PATH_ROULETTE=1 FARMOS_PATH_SAMPLING=1`.

### Detail (`eaeb0c6` CLEAN — AS/SSBO persist lever)

Games closed. Confirmed absent before compute and before HW RT: DeltaForceClient, Steam, steamwebhelper.

| Field | Value |
|-------|--------|
| Host | TommyLaptop |
| GPU | Intel(R) Arc(TM) Graphics |
| farmc SHA | `eaeb0c6d00297e62658d71c0fef34806edd83dd3` |
| PE sizes | hello **13824**; glass **61952**; compute cornell **159232**; RT cornell **242176** — **PASS** (B-01/02/08/09) |
| Compute median | **29335** (25863, 29335, 384095*); stdout `vulkan`; cold 26564 |
| HW RT median | **4881** (4881, 4678, 6724); cold 5287 |
| stdout | `vulkan_rt`; STRICT exit 0; downgrade **no** |
| vs 4953 floor | **≤4953-class** (4881 ≤ 4953; ~1.5% better) |
| AC-M9-13 / 14 | **FAIL** (4881 > 4000; 6.010× < 8×) |
| Artifacts | `out/m9_compute_baseline_eaeb0c6.txt`, `out/m9_vulkan_rt_speed_eaeb0c6.txt`, `out/m9_compute_bench_eaeb0c6/`, `out/m9_rt_bench_eaeb0c6/` |

Lever class: host BLAS/TLAS + SSBO persist across `renderPath` when geometry fingerprint matches. Kernel / SPIR-V / 8×8 unchanged vs `d91887b`.

Gain vs floor: 4953 → 4881 (~72 ms / ~1.5%). Far short of the ~950 ms needed for ≤4000. Next lever must be measured separately; do not stack unmeasured shader occupancy changes.

`farmc version`: `farmc 1.01`.

Stack: Vulkan AS + `VK_KHR_ray_query` inside compute. Not OptiX / full RT pipeline.