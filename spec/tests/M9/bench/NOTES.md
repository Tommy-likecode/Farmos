# M9 silver-still HW RT bench notes

**Status: tip `1003468` BLAS compact TommyLaptop CLEAN re-bench FAIL + REGRESS. RT median **6067 ms** > floor **4881** (`eaeb0c6`). AC-M9-13 FAIL. Stop — no further CloudAgent lever. Do not invent new Windows numbers.**

This Linux CI / cloud-agent host does not provide a hardware ray-tracing device.
Windows numbers below are TommyLaptop / poteto only.

## Target (PM unchanged)

- Scene: `examples/artwork_silver_still.fm` (composition locked)
- 1920×1080 / 256 spp, RR+MIS, STRICT HW RT
- AC-M9-13: median ≤ **4000 ms** after 1 warm-up
- AC-M9-14: ≥ **8×** vs post-M8 compute
- Hard floor before this tip: **4881 ms** (`eaeb0c6` persist CLEAN)

## TommyLaptop measured rows (do not invent numbers)

| Tip | Compute median | RT median (STRICT `vulkan_rt`) | Ratio | Cold |
|-----|----------------|--------------------------------|-------|------|
| `d91887b` CLEAN | **28218 ms** | **4953 ms** | 5.697× | 5356 |
| `eaeb0c6` persist CLEAN | **29335 ms** (25863, 29335, 384095*) | **4881 ms** (4881, 4678, 6724) | **6.010×** | 5287 |
| `1003468` BLAS compact CLEAN | **353747 ms** (385416, 353747, 349691)† | **6067 ms** (6067, 5056, 6077) | 58.307×† | 36439 |

\*compute run3 outlier 384095 ms on `eaeb0c6` (machine noise); median used.
†`1003468` compute lane ~12× slower than prior ~29 s class (possible Arc thermal / driver state after long compute); ratio inflated. RT hard gate uses wall RT median vs 4881 only.

`eaeb0c6` persist remains best CLEAN floor: **4881 ms**. `1003468` **REGRESS** (6067 > 4881; best timed run 5056 still > 4881). Do not keep compact as the speed tip.

Dropped dead ends: `754b6a5` (8×4×4), `5127765` (thin-SPIR-V), dirty 10123 (GPU contention), and now **`1003468` BLAS compact** (RT regress).

### Detail (`1003468` CLEAN — BLAS compact after persist)

Games closed before sync and during runs. Confirmed absent: DeltaForceClient, Steam, steamwebhelper, df_launcher, artwork, DeltaForce.

| Field | Value |
|-------|--------|
| Host | TommyLaptop |
| GPU | Intel(R) Arc(TM) Graphics |
| farmc SHA | `1003468d4273b9621014bd200e1c7ffb5c565765` |
| PE sizes | hello **13824**; glass **61952** — **PASS** (B-01/02). (cornell RT PE not re-sampled same as prior 242176 path this pass) |
| Compute median | **353747** (385416, 353747, 349691); stdout `vulkan`; cold 388595 |
| HW RT median | **6067** (6067, 5056, 6077); cold 36439 |
| stdout | `vulkan_rt`; STRICT exit 0; downgrade **no** |
| vs 4881 floor | **REGRESS** (6067 > 4881) |
| AC-M9-13 / 14 | **FAIL** / (ratio 58.3× on inflated compute — ignore for gate; RT fails 13) |
| Artifacts | `out/m9_compute_baseline_1003468.txt`, `out/m9_vulkan_rt_speed_1003468.txt`, `out/m9_compute_bench_1003468/`, `out/m9_rt_bench_1003468/` |

Lever class: host BLAS compact after first persist build (`vkCmdCopyAccelerationStructureKHR` COMPACT). Kernel / SPIR-V / 8×8 / persist fingerprint unchanged vs `eaeb0c6`.

Decision: **stop**. Hard-stop on regress past 4881. No merge / no eggtooth. Best tip remains `eaeb0c6` at **4881 ms**.

`farmc version`: `farmc 1.01`.

Stack: Vulkan AS + `VK_KHR_ray_query` inside compute. Not OptiX / full RT pipeline.
