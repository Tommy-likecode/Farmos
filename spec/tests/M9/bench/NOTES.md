# M9 silver-still HW RT bench notes

**Status: CLEAN floor at `d91887b` is RT **4953 ms**. This tip is ONE host persist lever. AC-M9-13/14 still FAIL / pending poteto re-bench. Do not invent new Windows numbers.**

This Linux CI / cloud-agent host does not provide a hardware ray-tracing device
(`VK_KHR_ray_query` + acceleration structures). Windows numbers below are
TommyLaptop / poteto measurements only.

## Target (PM unchanged)

- Scene: `examples/artwork_silver_still.fm` (composition locked)
- 1920×1080 / 256 spp, RR+MIS, STRICT HW RT (`PATH_BACKEND_VULKAN_RT`,
  `FARMOS_PATH_GPU_STRICT=1`, no downgrade)
- AC-M9-13: median ≤ **4000 ms** after 1 warm-up
- AC-M9-14: ≥ **8×** vs post-M8 compute
- Need ~**1.24×** from the CLEAN RT median **4953 ms** to hit ≤4000

## TommyLaptop measured rows (do not invent numbers)

| Tip | Compute median | RT median (STRICT `vulkan_rt`) | Ratio | Cold |
|-----|----------------|--------------------------------|-------|------|
| `061ebe62` (dirty) | 33699 ms | **8773 ms** (8792, 8711, 8773) | 3.841× | 76497 |
| `754b6a5` | 42986 ms | **11697 ms** (11611, 11697, 11732) | 3.675× | 12739 |
| `5127765` | 47381 ms | **9773 ms** (10224, 9636, 9773) | 4.848× | 10729 |
| `d91887b` dirty | 34281 ms | **10123 ms** (games contention) | 3.386× | 9128 |
| `d91887b` **CLEAN** | **28218 ms** (26810, 29985, 28218) | **4953 ms** (4953, 4996, 4950) | **5.697×** | 5356 |

`d91887b` runtime is bit-identical to `061ebe62`. Dirty 10123 was GPU contention — ignore it. New hard floor: **4953 ms**. Must not exceed 4953; absolute never exceed old 8773.

`754b6a5` / `5127765` remain dropped dead ends. Do not reintroduce 8×4×4 or the 5127765 shader stack.

### Detail (`d91887b` CLEAN — authoritative floor)

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

## This tip — ONE lever (hypothesis)

**Class: host resource lifetime.** Persist BLAS/TLAS + SSBOs + descriptor set across `renderPath` calls when the geometry fingerprint matches. Skip per-frame `vkCreate`/`vkDestroy` and skip AS rebuild on reuse.

**Not changed:** `farm_vk_rt_path.comp`, committed SPIR-V, 8×8 dispatch, light packing (`nrect * 6`, GLSL `inverse()` stays), closest-hit confirm loop, MIS/RR.

**Why it should not exceed 4953:** the 8×8 kernel binary is unchanged. Reuse submits the same compute dispatch against the same PREFER_FAST_TRACE AS. Persist only subtracts host/device object churn and a redundant AS build that `d91887b` still paid on every timed run. Occupancy and SPIR-V shape are identical to the 4953 tip.

**Expected path to ≤4000:** cold vs steady on the clean binary was 5356 vs 4953 (~400 ms). Per-frame create/destroy of ~24 MiB accum + AS objects on Arc iGPU may be a similar slice (or larger under KMD alloc). If that slice is ≥~950 ms, persist lands ≤4000. If it is only tens of ms, this lever misses and the next one-at-a-time change must be measured separately. Not a claimed Windows number.

`farmc version`: `farmc 1.01`.

Stack: Vulkan AS + `VK_KHR_ray_query` inside compute. Not OptiX / full RT pipeline.
