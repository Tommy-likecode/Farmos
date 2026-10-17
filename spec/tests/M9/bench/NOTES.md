# M9 silver-still HW RT bench notes

**Status: CLEAN re-bench on `d91887b` — RT median 4953 ms (≤8773-class). AC-M9-13/14 still FAIL. Do not invent numbers.**

This Linux CI / cloud-agent host does not provide a hardware ray-tracing device
(`VK_KHR_ray_query` + acceleration structures). Windows numbers below are
TommyLaptop / poteto measurements only.

## Target (PM unchanged)

- Scene: `examples/artwork_silver_still.fm` (composition locked)
- 1920×1080 / 256 spp, RR+MIS, STRICT HW RT (`PATH_BACKEND_VULKAN_RT`,
  `FARMOS_PATH_GPU_STRICT=1`, no downgrade)
- AC-M9-13: median ≤ **4000 ms** after 1 warm-up
- AC-M9-14: ≥ **8×** vs post-M8 compute (~≤4212 ms vs the 33699 ms baseline)
- Need ~**1.24×** from the CLEAN RT median **4953 ms** to hit ≤4000

## TommyLaptop FAIL rows (do not invent numbers)

| Tip | Compute median | RT median (STRICT `vulkan_rt`) | Ratio | Cold |
|-----|----------------|--------------------------------|-------|------|
| `061ebe62` | 33699 ms | **8773 ms** (8792, 8711, 8773) | 3.841× | 76497 |
| `754b6a5` | 42986 ms | **11697 ms** (11611, 11697, 11732) | 3.675× | 12739 |
| `5127765` | 47381 ms | **9773 ms** (10224, 9636, 9773) | 4.848× | 10729 |
| `d91887b` dirty | 34281 ms | **10123 ms** (games contention) | 3.386× | 9128 |
| `d91887b` **CLEAN** | **28218 ms** (26810, 29985, 28218) | **4953 ms** (4953, 4996, 4950) | **5.697×** | 5356 |

`754b6a5` / `5127765` remain dropped dead ends.

### Detail (`d91887b` CLEAN — authoritative floor check)

Games closed (user+PM confirmed). Confirmed absent before benches: DeltaForceClient,
Steam, steamwebhelper, df_launcher, gameoverlay. Present non-contending: Steam++
(Watt Toolkit), GameBarPresenceWriter, GameInputRedistService.

| Field | Value |
|-------|--------|
| Host | TommyLaptop |
| GPU | Intel(R) Arc(TM) Graphics |
| farmc SHA | `d91887b350b86ed7c5f09c18f48b4d3f95988a6d` (runtime bit-identical to `061ebe62`) |
| PE sizes (prior AC-M9-10) | hello **13824**; glass **61952**; compute cornell **159232**; RT **235520** — **PASS** |
| Compute median | **28218** (26810, 29985, 28218); stdout `vulkan` |
| HW RT median | **4953** (4953, 4996, 4950); cold 5356 |
| stdout | `vulkan_rt`; STRICT exit 0; downgrade **no** |
| vs 8773 floor | **≤8773-class** (4953 << 8773; dirty 10123 was GPU contention) |
| AC-M9-13 / 14 | **FAIL** (4953 > 4000; 5.697× < 8×) |
| Artifacts | `out/m9_compute_baseline_clean.txt`, `out/m9_vulkan_rt_speed_clean.txt`, `out/m9_compute_bench_clean/`, `out/m9_rt_bench_clean/` |

New hard floor for steady RT on this tip / binary class: **4953 ms**.

### Detail (`d91887b` dirty — superseded)

| Field | Value |
|-------|--------|
| Compute median | **34281** |
| HW RT median | **10123** (DeltaForceClient + Steam resident) |
| vs 8773 | appeared REGRESSED; superseded by CLEAN |

### Detail (`061ebe62` — prior hard floor reference)

| Field | Value |
|-------|--------|
| HW RT median ms | **8773** (8792, 8711, 8773) |
| Compute median ms | **33699** |
| Ratio | 3.841× |
| PE RT cornell | **242176** |

Method (all): median of 3 after 1 warm-up; `FARMOS_PATH_ROULETTE=1 FARMOS_PATH_SAMPLING=1`.
Compute forces `PATH_BACKEND_VULKAN`; RT uses artwork auto-select + `FARMOS_PATH_GPU_STRICT=1`.

## This tip (reset)

Runtime / shader / SPIR-V are **bit-identical to `061ebe62`**. No persist, no host
inverse, no workgroup change, no `spirv-opt`. Those deltas are not proven-safe:
every post-floor kernel/host packing change missed the prior 8773 floor under dirty load;
CLEAN shows the true binary is **~4953 ms** class when GPU is free.

AC-M9-13/14 still need ~1.24× more RT speed (4953→≤4000) and/or higher ratio vs compute.

Do not re-apply `754b6a5` / `5127765` deltas. Do not merge / eggtooth until PASS.

`farmc version`: `farmc 1.01`.

Stack: Vulkan AS + `VK_KHR_ray_query` inside compute. Not OptiX / full RT pipeline.
