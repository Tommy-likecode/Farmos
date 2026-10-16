# M8 bench notes — fill in measured baselines before claiming AC-M8-13/14

## How to measure

1. Build farmc (Release).
2. **M7 baseline on the same host** (required before a speedup claim):
   - Prefer an M7-era binary (`c7df378` / v1.01) if still available.
   - Else this tree with speed features at **defaults** (RR off, sampling M4, adaptive off).
   - Command: `bash spec/tests/M8/bench/run_artwork_bench.sh build/farmc`
   - Record median of 3 after 1 warm-up.
3. **M8 speed mode:** `FARMOS_PATH_ROULETTE=1 FARMOS_PATH_SAMPLING=1` and the same command.
4. Ratio = baseline_ms / speed_ms (greater is faster).

Linux CPU AC-M8-14 on this agent used this-tree `farmc` (tip `3f1cd1cf7553ca162b61455118d84077271358f2` at measure time) with defaults vs env speed flags, via `bash spec/tests/M8/bench/run_linux_cpu_ac.sh build/farmc`. `FARMOS_VULKAN_FORCE_FAIL=1` so artwork stays on the CPU integrator even if a loader is present. Raw logs: `linux_cpu_full.log`, `linux_cpu_baseline.log`, `linux_cpu_speed.log`.

## Linux CPU (this agent)

Host: `cursor` (Cloud Linux, `Linux 6.12.94+ x86_64`, Intel Xeon 4 cores / 4 threads).
`FARMOS_THREADS=4` (`nproc`).
Workgroup: n/a (CPU).
Backend: `cpu` (first stdout line of `artwork`).
Program: `examples/artwork_silver_still.fm` @ **1920×1080 / 256 spp** / 6 bounces. Composition unchanged.
`farmc version`: `farmc 1.01`. Measured 2026-10-15/16.

| Lane | backend | RR | sampling | adaptive | spp | res | threads | median ms | notes |
|------|---------|----|----------|----------|-----|-----|---------|-----------|-------|
| M7 / compat baseline | CPU | off | M4 | off | 256 | 1920×1080 | 4 | **161744** | warmup 184549; timed 165453 / 161744 / 161744 |
| M8 speed | CPU | on | MIS | off | 256 | 1920×1080 | 4 | **241771** | warmup 239314; timed 237836 / 241771 / 243371; RR+MIS |

Ratio = 161744 / 241771 = **0.669×** (speed lane is slower). Target ≥ **1.5×** → **not met** on this host with this-tree defaults as the M7/compat baseline.

To hit ≥1.5× vs this baseline, speed median would need to be ≤ **107829** ms.

MIS (angular RectArea + extra pdf/eval) added more work than RR saved at `maxBounces=6` (RR starts at depth ≥ 3). Local-space BLAS AABB is already on in both lanes (compat-safe), so it does not appear in this ratio.

## TommyLaptop Vulkan (coordinator / poteto)

Host: TommyLaptop, Windows x64, successful GPU path (`println` first line `vulkan`, not `cpu`).
M7 baseline measured by coordinator on tip `a8a75541` (do not re-run Vulkan in this agent).

| Lane | backend | RR | sampling | adaptive | spp | res | workgroup | median ms | notes |
|------|---------|----|----------|----------|-----|-----|-----------|-----------|-------|
| M7 baseline | Vulkan | off | M4 | off | 256 | 1920×1080 | 8×8 | **2217574** | tip a8a75541; warmup 1438860; timed 1476426 / 2217574 / 2753274; backend vulkan confirmed |
| M8 speed | Vulkan | on | MIS | off | 256 | 1920×1080 | 8×8 | *pending poteto / TommyLaptop* | BVH + RR + MIS; one dispatch / renderPath |

Target: ≥ **2×** vs the recorded M7 Vulkan baseline → M8 speed median **≤ 1108787 ms**.

## Size / DCE (AC-M8-10)

See `spec/tests/M8/bench/SIZE.md` after `scripts/check_size_budgets.py`. Linux ELF sizes are informative; PE budgets are the Windows ACs (hello ≤ 20 KiB; CPU glass+mirror ≤ 300 KiB; GPU-linked ≤ 400 KiB; no Vulkan symbols on hello / CPU-only).
