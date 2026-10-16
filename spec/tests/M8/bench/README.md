# M8 artwork wall-clock bench (AC-M8-12..15)

Normative program: `examples/artwork_silver_still.fm`
Resolution: **1920×1080**. Quality: **`setSamples(256)`**, `setMaxBounces(6)` as in the `.fm`.
Composition is locked to `out/artwork_silver_still.png` — do **not** edit meshes, lights, camera, or materials.

## Method (spec §7.2)

- Wall-clock: **median of 3 timed runs after 1 warm-up** for one `renderPath` (steady-state).
- Record: host id, backend, RR on/off, sampling mode, adaptive on/off, spp, resolution, threads / workgroup, elapsed ms.
- **Measure and record M7 baselines on the same host before claiming speedups.**

## Speed vs baseline

| Lane | How |
|------|-----|
| M7 baseline | M7-era binary, or this binary with RR off, `PATH_SAMPLING_M4`, adaptive off (defaults). Unset `FARMOS_PATH_ROULETTE` / `FARMOS_PATH_SAMPLING`. |
| M8 speed | `FARMOS_PATH_ROULETTE=1 FARMOS_PATH_SAMPLING=1` (method wins if the program called the setters; artwork does not). Adaptive off. |

Artwork already opts into Vulkan when `isVulkanPathAvailable()`.

## Workgroup (AC-M8-15)

Default GPU workgroup is **8×8** pixels (`layout(local_size_x = 8, local_size_y = 8)` in `runtime/farm_vk_path.comp`). One `renderPath` with `n` samples is **one** upload + **one** compute dispatch (sample loop is inside the shader) + one readback — not per-sample host RTTs. `FARMOS_THREADS` is CPU-only. `FARMOS_VULKAN_WORKGROUP=WxH` is reserved as a non-normative note; the committed SPIR-V is 8×8.

GPU path uploads a world-space triangle BVH (bindings 6–7) so occupancy is not a brute-force triangle scan.

## Scripts

```
# Linux CPU (document FARMOS_THREADS; recommend physical cores)
bash spec/tests/M8/bench/run_artwork_bench.sh /path/to/farmc

# Speed mode
FARMOS_PATH_ROULETTE=1 FARMOS_PATH_SAMPLING=1 \
  bash spec/tests/M8/bench/run_artwork_bench.sh /path/to/farmc

# Windows (TommyLaptop Vulkan)
powershell -File spec/tests/M8/bench/run_artwork_bench.ps1 -Farmc path\to\farmc.exe
```

## Targets (OQ-M8-04 A)

| Host / backend | Target vs M7 baseline |
|----------------|----------------------|
| TommyLaptop Vulkan | ≥ **2×** |
| Linux CPU | ≥ **1.5×** |

Fill `NOTES.md` with measured ms. Vulkan numbers are left for the coordinator on TommyLaptop.
