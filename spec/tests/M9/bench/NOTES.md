# M9 silver-still HW RT bench notes

**Status: persist kept (`eaeb0c6` CLEAN RT **4881 ms**). This tip is ONE extra lever: BLAS compact. AC-M9-13/14 still FAIL / pending poteto re-bench. Do not invent new Windows numbers.**

This Linux CI / cloud-agent host does not provide a hardware ray-tracing device.
Windows numbers below are TommyLaptop / poteto only.

## Target (PM unchanged)

- Scene: `examples/artwork_silver_still.fm` (composition locked)
- 1920×1080 / 256 spp, RR+MIS, STRICT HW RT
- AC-M9-13: median ≤ **4000 ms** after 1 warm-up
- AC-M9-14: ≥ **8×** vs post-M8 compute
- Need ~**880 ms** / ~1.22× from the persist floor **4881 ms**

## TommyLaptop measured rows (do not invent numbers)

| Tip | Compute median | RT median (STRICT `vulkan_rt`) | Ratio | Cold |
|-----|----------------|--------------------------------|-------|------|
| `d91887b` CLEAN | **28218 ms** | **4953 ms** | 5.697× | 5356 |
| `eaeb0c6` persist CLEAN | **29335 ms** (25863, 29335, 384095*) | **4881 ms** (4881, 4678, 6724) | **6.010×** | 5287 |

\*compute run3 outlier 384095 ms (machine noise); RT run3 6724 elevated; median used.

`eaeb0c6` persist is **≤4953** (~72 ms better). Keep persist. New hard floor: **4881 ms**. Must not exceed 4881; absolute never past 4953/8773.

Dropped dead ends: `754b6a5` (8×4×4), `5127765` (thin-SPIR-V). Dirty 10123 was GPU contention.

### Detail (`eaeb0c6` CLEAN — persist)

Games closed. PE: hello **13824**; glass **61952**; compute **159232**; RT cornell **242176** — **PASS**.
stdout `vulkan_rt`; STRICT exit 0. Artifacts: `out/m9_*_eaeb0c6*`.

## This tip — ONE lever (hypothesis)

**Class: AS quality (compact), not occupancy / SPIR-V / persist.** After the first BLAS build, query compacted size and `vkCmdCopyAccelerationStructureKHR` (COMPACT) if smaller. Persist then reuses the compacted BLAS. TLAS is rebuilt once from the compacted BLAS address (warmup only).

**Not changed:** `farm_vk_rt_path.comp`, committed SPIR-V, 8×8 dispatch, light packing, confirm-loop closest-hit, persist-on-fingerprint.

**Why it should not exceed 4881:** shader binary is unchanged. Compact is warmup-only; timed runs dispatch against a same-or-smaller FAST_TRACE BLAS. If compact is unavailable or not smaller, the uncompacted persist path is kept (the 4881 path). Compact failure does not tear down persist.

**Expected path to ≤4000:** ~5e9 `rayQuery` traversals. If uncompacted BLAS traffic is a large slice of 4.9 s, a tighter BLAS can return part of the ~880 ms. If shading dominates, this lever misses. Not a claimed Windows number.

`farmc version`: `farmc 1.01`.

Stack: Vulkan AS + `VK_KHR_ray_query` inside compute. Not OptiX / full RT pipeline.
