# M13 Arc CLAIM — OQ-M13-01..08 A (PENDING TommyLaptop)

**Verdict:** **PENDING** Arc Pass1/Pass2. Linux product + harness **green**.  
**Do not merge.** Official XeSS only. Enabler NOT used.

| Field | Value |
|-------|--------|
| PR | #22 (draft) https://github.com/Tommy-likecode/Farmos/pull/22 |
| Branch | `cursor/m13-performance-a23d` |
| Product SHA | `945c13aeaf7fb086bfc718c7d869358441706a17` (`FARMOS_PATH_FRAME_MS`) |
| Master baseline | `affc63ea00f147f9ad09a0d559d0650e4543287a` (PR #21) |
| Product baseline (Arc CLAIM) | `d23acfb3d8efde48698f210a9f7666c377bfb9f2` Pass1 **1.264** / Pass2 **1.348** |
| Host | TommyLaptop Arc timing **not run** on this cloud agent (no XeSS/Vulkan device) |
| Protocol | In-process `FARMOS_PATH_FRAME_MS` after 3 warmup frames; M12 process-per-frame floor |
| Spec | `spec/M13-performance.md` **FINAL** (OQ-M13-01..08 A) |
| SDK | Official XeSS only. Enabler / Swapper / OptiScaler **OUT**. |

## Gates

| Gate | Rule | Measured | Result |
|------|------|----------|--------|
| AC-M13-01 | M12 OFF CPU twins | Linux M12 001 PASS | **PASS** (Linux) |
| AC-M13-02 | ROI L [80,200]; \|ΔL\| ≤ 5.0 pinned box | *Arc later* | **PENDING** |
| AC-M13-03 | XeSS wall strictly < native | *Arc later* | **PENDING** |
| AC-M13-04 | SHOULD ≥ **1.20×** Pass1 **and** Pass2 | *Arc later*; baseline `d23acfb3` 1.264 / 1.348 | **PENDING** |
| AC-M13-05 | glue PE ≤ 700 KiB; hello ≤ 20 KiB | Linux ELF B-11 **68112** / 716800; hello **14512** | **PASS** (Linux) / PE **PENDING** |
| AC-M13-06 | no new public API | `FARMOS_PATH_FRAME_MS` only | **PASS** |
| AC-M13-07 | frame-gen / window / DLSS OUT | no new refs | **PASS** |
| AC-M13-08 | Linux soft-skip XeSS | M12 004/006 skip `requires_vulkan` | **PASS** |
| AC-M13-09 | default 960×540→1080p | M12 bench sources reused | **PASS** |

Do not invent Arc milliseconds. Cite `d23acfb3` **1.264 / 1.348** until a new Arc run.

## Linux verification (product `945c13ae`; no Vulkan/SDK)

Host: 4 cores, load 0.00–0.45. `FARM_RUNTIME` = repo `runtime/`.

| Command | Result |
|---------|--------|
| `bash scripts/run_suite_linux.sh build/farmc spec/tests/M13` | 1/1 PASS |
| `bash scripts/run_suite_linux.sh build/farmc spec/tests/M12` | 8/8 (004/006 skip `requires_vulkan`) |
| `bash scripts/run_suite_linux.sh build/farmc spec/tests/M4` | 24/24 PASS |
| `bash scripts/run_suite_linux.sh build/farmc spec/tests/M11` | 4/4 PASS |
| `bash scripts/run_suite_linux.sh build/farmc spec/tests/M7` | 9/9 (008 skip `requires_vulkan`) |
| `python3 scripts/check_size_budgets.py build/farmc .` | PASS — ELF B-11 **68112** / 716800; hello **14512** |
| `python3 spec/tests/M13/bench/run_linux_host_amort.py build/farmc --out out/m13_linux` | PASS. error_count=0 frames_done=20. easy 8×8 proc/inproc median **54.4×** (0.544 / 0.010 ms, n=5). target 32×32 **6.06×** (0.927 / 0.153 ms, n=5). Limiter: process spawn+init vs in-process `renderPath`. |

## Hypotheses

| Id | Hypothesis | Verdict |
|----|------------|---------|
| H1 | In-process `frame_ms` amortizes process-per-frame spawn (OQ-03 host) | **kept** (Linux harness PASS; Arc ratio later) |
| H2 | Merge fill_aux two-pass depth init into one pass | **reverted / not applied**. 960×540 micro: 0.58→0.52 ms (n=7). Too small vs process-per-frame seconds. |
| H3 | XeSS quality-mode / sharpness | **not tried**. Cannot check AC-04 without SDK. |
| H4 | GPU skip host color/depth round-trip | **not tried**. No Vulkan device here. |

Dedicated XeSS VkDevice, PSO disk cache, AS persist, FAST_BUILD: kept as already on master `affc63ea`.
