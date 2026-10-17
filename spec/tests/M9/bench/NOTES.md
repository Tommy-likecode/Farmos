# M9 silver-still HW RT bench notes

**Status: speed work RESET to `061ebe62` (hard floor 8773 ms). AC-M9-13/14 still FAIL / pending poteto re-bench. Do not invent new Windows numbers.**

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

`754b6a5` (8×4×4 fat rayQuery) and `5127765` (8×8 revert + persist / host inverse /
single-proceed / SPIR-V thinning) both **REGRESSED** vs `061ebe62` on steady RT.
Those commits are **dropped**. Do not celebrate cold-AS drops.

`5127765` PE RT cornell **244736** (PASS). Bench dirs `out/m9_compute_bench2`,
`out/m9_rt_bench2`.

### Detail (`061ebe62` — hard floor)

| Field | Value |
|-------|--------|
| Host | TommyLaptop |
| GPU | Intel(R) Arc(TM) Graphics (driver 32.0.101.8331) |
| farmc SHA | `061ebe6214bc8bb64437785f450f67204e1313e3` |
| Windows suite | **430/430 PASS** |
| PE sizes | hello **13824**; glass **61952**; compute **159232**; RT **242176** — **PASS** |
| Downgrade? | **no** — stdout `vulkan_rt`, STRICT exit 0 |
| AC-M9-13 / 14 | **FAIL** (8773 > 4000; 3.841× < 8×) |

### Detail (`754b6a5` — dead end)

8×4×4 + shared reduce + `barrier()` + `spirv-opt -O`. Occupancy collapse on Xe-LPG.
PE RT cornell **249344**. Suite skipped that pass.

### Detail (`5127765` — dead end)

8×8 restore plus host inverse, AS persist, single `rayQueryProceedEXT`, dead-code
cut, bary skip. Still **9773 > 8773**. Compute 47381 (runs 48690, 47381, 43249).
Artifacts: attached `m9_vulkan_rt_speed` / `m9_compute_baseline` for this SHA.

Method (all): median of 3 after 1 warm-up; `FARMOS_PATH_ROULETTE=1 FARMOS_PATH_SAMPLING=1`.

## This tip (reset)

Runtime / shader / SPIR-V are **bit-identical to `061ebe62`**. No persist, no host
inverse, no workgroup change, no `spirv-opt`. Those deltas are not proven-safe:
every post-floor kernel/host packing change missed the 8773 floor.

Expected steady RT: **8773-class** (same binary kernel). Must not exceed 8773.
Not a claim of ≤4000.

How ≤4000 could still be reached (not shipped here): the 8.7 s is the 8×8
integrator × ~2M×256×(closest + 2 shadow `rayQuery`s). Occupancy stacking and
“thin SPIR-V” both made Intel’s compiler worse. The next lever has to be
**one change, measured on TommyLaptop**, not a stack. Candidates that were
**not** tried in isolation: none remain that are proven-safe. Do not re-apply
`754b6a5` / `5127765` deltas.

`farmc version`: `farmc 1.01`.

Stack: Vulkan AS + `VK_KHR_ray_query` inside compute. Not OptiX / full RT pipeline.
