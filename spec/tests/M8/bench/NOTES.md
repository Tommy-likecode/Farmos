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

## Linux CPU (this agent)

Host: Cloud Linux CI / cloud-agent VM (not TommyLaptop).
`FARMOS_THREADS`: see script log (nproc).
Workgroup: n/a (CPU).

| Lane | backend | RR | sampling | adaptive | spp | res | threads | median ms | notes |
|------|---------|----|----------|----------|-----|-----|---------|-----------|-------|
| M7 / compat baseline | CPU | off | M4 | off | 256 | 1920×1080 | (nproc) | *TBD this run* | record before claiming 1.5× |
| M8 speed | CPU | on | MIS | off | 256 | 1920×1080 | (nproc) | *TBD this run* | RR+MIS+local BLAS |

Target: ≥ **1.5×** vs the recorded M7/compat baseline on this host.

A full 1920×1080 / 256 spp CPU render is slow; the harness times `renderPath` via `/usr/bin/time` around the whole process (includes compile-once then 1 warm-up + 3 timed exe runs).

## TommyLaptop Vulkan (coordinator / poteto)

Host: TommyLaptop, Windows x64, successful GPU path (`println` first line `vulkan`, not `cpu`).

| Lane | backend | RR | sampling | adaptive | spp | res | workgroup | median ms | notes |
|------|---------|----|----------|----------|-----|-----|-----------|-----------|-------|
| M7 baseline | Vulkan | off | M4 | off | 256 | 1920×1080 | 8×8 | **not measured here** | coordinator |
| M8 speed | Vulkan | on | MIS | off | 256 | 1920×1080 | 8×8 | **not measured here** | BVH + RR + MIS; one dispatch / renderPath |

Target: ≥ **2×** vs the recorded M7 Vulkan baseline on TommyLaptop.

## Size / DCE (AC-M8-10)

See `spec/tests/M8/bench/SIZE.md` after `scripts/check_size_budgets.py`. Linux ELF sizes are informative; PE budgets are the Windows ACs (hello ≤ 20 KiB; CPU glass+mirror ≤ 300 KiB; GPU-linked ≤ 400 KiB; no Vulkan symbols on hello / CPU-only).
