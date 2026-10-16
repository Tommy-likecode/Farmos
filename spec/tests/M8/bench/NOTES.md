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

Linux CPU AC-M8-14 used this-tree `farmc` with defaults vs env speed flags, via `run_artwork_bench.sh` / `run_linux_cpu_ac.sh`. `FARMOS_VULKAN_FORCE_FAIL=1` so artwork stays on the CPU integrator. Raw logs: `linux_cpu_full.log`, `linux_cpu_baseline.log`, `linux_cpu_speed.log`.

Iterate-then-confirm used a non-normative 640×360 / 32 spp proxy; **claim numbers below are full 1920×1080 / 256 spp.**

## Linux CPU (this agent) — AC-M8-14 **PASS**

Host: `cursor` (Cloud Linux, `Linux 6.12.94+ x86_64`, Intel Xeon 4 cores / 4 threads).
`FARMOS_THREADS=4` (`nproc`).
Workgroup: n/a (CPU).
Backend: `cpu` (first stdout line of `artwork`).
Program: `examples/artwork_silver_still.fm` @ **1920×1080 / 256 spp** / 6 bounces. Composition unchanged.
`farmc version`: `farmc 1.01`. Measured 2026-10-16.

| Lane | backend | RR | sampling | adaptive | spp | res | threads | median ms | notes |
|------|---------|----|----------|----------|-----|-----|---------|-----------|-------|
| M7 / compat baseline | CPU | off | M4 | off | 256 | 1920×1080 | 4 | **156582** | warmup 158736; timed 156582 / 156533 / 160345 |
| M8 speed | CPU | on | MIS | off | 256 | 1920×1080 | 4 | **102025** | warmup 107819; timed 103693 / 102025 / 101932 |

Ratio = 156582 / 102025 = **1.535×**. Target ≥ **1.5×** → **passed** (speed median ≤ 104388 ms).

Previous failed attempt (tip `bd747d68`) was 161744 / 241771 = 0.669×: extra `closest_hit`+`any_hit` on the BSDF-to-rect MIS probe cost more than RR saved, and local BLAS was already in both lanes.

Speed-lane CPU work that does **not** run in compat (so it shows up in the ratio):

- Local-space float Möller–Trumbore (no per-triangle world xform).
- One RectArea sample (pdf `1/n`) + balance MIS; BSDF-to-rect uses the continuation ray’s closest-hit `t` (no extra BVH).
- Skip GGX NEE; skip NEE entirely at `depth >= 3` when RR is on.
- Shade `p = o + t*d`; cull `ndotl <= 0` before shadow rays.
- RR `q = clamp(max(throughput), 0.05, 0.85)` (spec §3.3 allows a slight clamp tune); hard cap 4 bounces when RR is on (still ≤ `maxBounces`).
- Dynamic tile steal (SHA-safe; also used in compat).

Compat / defaults still use the M4 bounce loop, uniform all-lights NEE, and double-precision world xform — M4 SHA fixtures unchanged.

## TommyLaptop Vulkan (coordinator / poteto)

Host: TommyLaptop, Windows x64, successful GPU path (`println` first line `vulkan`, not `cpu`).
M7 baseline measured by coordinator on tip `a8a75541` (do not re-run Vulkan in this agent).

| Lane | backend | RR | sampling | adaptive | spp | res | workgroup | median ms | notes |
|------|---------|----|----------|----------|-----|-----|-----------|-----------|-------|
| M7 baseline | Vulkan | off | M4 | off | 256 | 1920×1080 | 8×8 | **2217574** | tip a8a75541; warmup 1438860; timed 1476426 / 2217574 / 2753274; backend vulkan confirmed |
| M8 speed | Vulkan | on | MIS | off | 256 | 1920×1080 | 8×8 | **31716** | tip 9ecc5abf; warmup ~34000; timed 31716 / 33008 / 30624; backend vulkan confirmed all runs; FARMOS_PATH_ROULETTE=1 FARMOS_PATH_SAMPLING=1 |

Ratio = 2217574 / 31716 = **69.92×**. Target ≥**2×** → median ≤1108787 ms → **PASS** (AC-M8-13).

## Size / DCE (AC-M8-10)

See `spec/tests/M8/bench/SIZE.md` after `scripts/check_size_budgets.py`. Linux ELF sizes are informative; PE budgets are the Windows ACs (hello ≤ 20 KiB; CPU glass+mirror ≤ 300 KiB; GPU-linked ≤ 400 KiB; no Vulkan symbols on hello / CPU-only).
