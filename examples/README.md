# Farmos example programs (M6)

Normative names (`spec/M6-hardening.md` §9). Each MUST build with `farmc` and smoke-run.

| File | Shape | Budget |
|------|-------|--------|
| `01_hello.fm` | M1 hello | ≤ 20 KiB stripped |
| `02_rotating_cube.fm` | M3 rotating cube → PNG | ≤ 96 KiB |
| `03_cornell_path.fm` | M4 Cornell `renderPath` | (hero / docs) |
| `04_glass_mirror.fm` | M4 glass + mirror | ≤ 300 KiB |
| `05_stacking_bounce.fm` | M5 stack + bounce | S_demo − S_scene ≤ 64 KiB |
| `06_vulkan_cornell.fm` | M7 Cornell with Vulkan opt-in | ≤ 400 KiB GPU-linked |
| `07_vulkan_rt_cornell.fm` | M9 Cornell with HW RT opt-in | ≤ 500 KiB RT-linked |
| `08_liquid_5s.fm` | M15 liquid + `saveMP4` (~5 s) | S_liq − S_scene ≤ 96 KiB (no ffmpeg in PE) |
| `artwork_silver_still.fm` | 1920×1080 path-traced still (silver sphere + random matte shapes) | not a size gate |

Bodies are production demos derived from `spec/tests/M3|M4|M5|M7` fixtures. Smoke: `scripts/run_examples.ps1` / `scripts/run_examples.sh` (01..05). `examples/m4_cornell_800x600.fm` is the B-05 peak-RAM driver (not part of the 01..05 smoke set). `06_vulkan_cornell.fm` is the M7 GPU-linked size sample; it falls back to CPU when no device is present. `07_vulkan_rt_cornell.fm` is the M9 RT-linked size sample (HW RT → compute → CPU).

`artwork_silver_still.fm` is a 1080p still (256 samples, 6 bounces, large overhead area light). It is **not** in the 01..05 smoke set. On Windows, from the directory where you want the PNG:

```
farmc build examples\artwork_silver_still.fm -o artwork_silver_still.exe
.\artwork_silver_still.exe
```

Writes `artwork_silver_still.png` in the process CWD. Prefers HW RT (`isVulkanRayTracingAvailable()`), else compute Vulkan, else CPU. Matte object positions change each run via `random01()`.
