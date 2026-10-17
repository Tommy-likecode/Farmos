# M9 silver-still HW RT bench notes

**Status: tip `d91887b` re-bench on TommyLaptop — AC-M9-13/14 FAIL; steady RT REGRESSED vs 8773 floor (contaminated?). Do not invent numbers.**

This Linux CI / cloud-agent host does not provide a hardware ray-tracing device
(`VK_KHR_ray_query` + acceleration structures). Windows numbers below are
TommyLaptop / poteto measurements only.

## Target (PM unchanged)

- Scene: `examples/artwork_silver_still.fm` (composition locked)
- 1920×1080 / 256 spp, RR+MIS, STRICT HW RT (`PATH_BACKEND_VULKAN_RT`,
  `FARMOS_PATH_GPU_STRICT=1`, no downgrade)
- AC-M9-13: median ≤ **4000 ms** after 1 warm-up
- AC-M9-14: ≥ **8×** vs post-M8 compute (~≤4212 ms vs the 33699 ms baseline)
- Need ~**2.2×** from the best measured RT median **8773 ms**

## TommyLaptop FAIL rows (do not invent numbers)

| Tip | Compute median | RT median (STRICT `vulkan_rt`) | Ratio | Cold |
|-----|----------------|--------------------------------|-------|------|
| `061ebe62` | 33699 ms | **8773 ms** (8792, 8711, 8773) | 3.841× | 76497 |
| `754b6a5` | 42986 ms | **11697 ms** (11611, 11697, 11732) | 3.675× | 12739 |
| `5127765` | 47381 ms | **9773 ms** (10224, 9636, 9773) | 4.848× | 10729 |
| `d91887b` | 34281 ms | **10123 ms** (10123, 10118, 10129); rerun (9052, 10139, 10123) med **10123** | 3.386× | 9201 / 9128 |

`d91887b` parent is `061ebe62`; runtime / `farm_vk_rt.c` / SPIR-V / `.comp` **git blob-identical** to `061ebe62`. Expected ~8773-class. Measured steady **10123 > 8773** → treat as **REGRESS vs floor** for claim purposes, with caveat: TommyLaptop had **DeltaForceClient + Steam** resident during both RT passes (possible GPU contention). One rerun sample hit **9052** (still >8773). AC power connected (BatteryStatus=2, ~19% charge). Do not celebrate; do not merge.

`754b6a5` / `5127765` remain dropped dead ends.

### Detail (`d91887b` — this re-bench)

| Field | Value |
|-------|--------|
| Host | TommyLaptop |
| GPU | Intel(R) Arc(TM) Graphics |
| farmc SHA | `d91887b350b86ed7c5f09c18f48b4d3f95988a6d` |
| PE sizes (AC-M9-10) | hello **13824**; glass **61952**; compute cornell **159232**; RT (`006_rt_png_tol`) **235520** — **PASS** (≤20/300/400/500 KiB) |
| Compute median | **34281** (36277, 34281, 33259); stdout `vulkan` |
| HW RT median | **10123** (primary 10123/10118/10129; confirm rerun med 10123) |
| stdout | `vulkan_rt`; STRICT exit 0; downgrade **no** |
| vs 8773 floor | **REGRESSED** (10123 > 8773; >5% band) |
| AC-M9-13 / 14 | **FAIL** (10123 > 4000; 3.386× < 8×) |
| Artifacts | `out/m9_compute_baseline.txt`, `out/m9_vulkan_rt_speed.txt`, `out/m9_pe_size.txt`, `out/m9_compute_bench3/`, `out/m9_rt_bench3/` |

### Detail (`061ebe62` — hard floor reference)

| Field | Value |
|-------|--------|
| HW RT median ms | **8773** (8792, 8711, 8773) |
| Compute median ms | **33699** |
| Ratio | 3.841× |
| PE RT cornell | **242176** |

Method (all): median of 3 after 1 warm-up; `FARMOS_PATH_ROULETTE=1 FARMOS_PATH_SAMPLING=1`.

`farmc version`: `farmc 1.01`.

Stack: Vulkan AS + `VK_KHR_ray_query` inside compute. Not OptiX / full RT pipeline.