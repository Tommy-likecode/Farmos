# M9 hardware RT artwork wall-clock bench (AC-M9-12..14)

Normative program: `examples/artwork_silver_still.fm`
Resolution: **1920×1080**. Quality: **`setSamples(256)`**, `setMaxBounces(6)` as in the `.fm`.
Composition is locked — do **not** edit meshes, lights, camera, or materials.

Primary backend: `PATH_BACKEND_VULKAN_RT` (successful HW RT, not a soft downgrade).
Secondary baseline: post-M8 compute Vulkan (`PATH_BACKEND_VULKAN`) on the same host.

## Method (spec §9.2)

- Wall-clock: **median of 3 timed runs after 1 warm-up** for one `renderPath` (steady-state; AS already built / reused).
- Also record cold first-call (includes AS build) in notes — not the primary gate.
- Record: host id, backend, RR/MIS/adaptive flags, spp, resolution, elapsed ms, whether HW RT or downgrade ran.

## Targets (OQ-M9-04 A)

| Gate | Target |
|------|--------|
| **Primary** | Silver still 1920×1080 @ 256 spp on TommyLaptop with HW RT: median ≤ **4 s** after 1 warm-up |
| **Secondary** | Same setup: ≥ **8×** wall-clock vs post-M8 compute baseline when measurable |

AC-M9-12 is the harness. AC-M9-13/14 **FAILED** at `061ebe62` (8773 ms) and
**REGRESSED** at `754b6a5` (11697 ms) and `5127765` (9773 ms). Speed work is
**RESET** to `061ebe62` (see `NOTES.md`). Pending poteto re-bench. Do not
invent Windows numbers from Linux CI.

## Scripts

```
# Linux (usually no HW RT device; records fallback / skip)
bash spec/tests/M9/bench/run_artwork_bench.sh /path/to/farmc

# Windows (TommyLaptop HW RT)
powershell -File spec/tests/M9/bench/run_artwork_bench.ps1 -Farmc path\to\farmc.exe
```

`FARMOS_PATH_ROULETTE=1 FARMOS_PATH_SAMPLING=1` may be set for a speed RT lane; a compat RT lane (defaults) SHOULD also be timed.

Stack: Vulkan AS + `VK_KHR_ray_query` inside compute. Not OptiX. Not a full ray-tracing pipeline / SBT.
