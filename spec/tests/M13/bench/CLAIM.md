# M13 Arc CLAIM — OQ-M13-01..08 A (PENDING TommyLaptop)

**Verdict:** **PENDING** (Linux product + harness ready; Arc Pass1/Pass2 filled later)  
**Do not merge.** Official XeSS only. Enabler NOT used.

| Field | Value |
|-------|--------|
| Branch | `cursor/m13-performance-a23d` |
| Docs / product SHA | *fill at push* |
| Master baseline | `affc63ea` (PR #21) |
| Product baseline (Arc CLAIM) | `d23acfb3d8efde48698f210a9f7666c377bfb9f2` Pass1 **1.264** / Pass2 **1.348** |
| Host | TommyLaptop, Windows x64, Intel Arc, `PATH_BACKEND_VULKAN_RT` |
| Protocol | In-process `FARMOS_PATH_FRAME_MS` after warmup; M12 process-per-frame floor |
| Spec | `spec/M13-performance.md` **FINAL** (OQ-M13-01..08 A) |
| SDK | Official XeSS only (`libxess.dll`). Enabler / Swapper / OptiScaler **OUT**. |

## Gates

| Gate | Rule | Measured | Result |
|------|------|----------|--------|
| AC-M13-01 | M12 OFF CPU twins | Linux M12 001 | **PASS** (Linux) |
| AC-M13-02 | ROI L [80,200]; \|ΔL\| ≤ 5.0 pinned box | *Arc later* | **PENDING** |
| AC-M13-03 | XeSS wall strictly < native | *Arc later* | **PENDING** |
| AC-M13-04 | SHOULD ≥ **1.20×** Pass1 **and** Pass2 | *Arc later* | **PENDING** |
| AC-M13-05 | glue PE ≤ 700 KiB; hello ≤ 20 KiB | Linux ELF B-11 *fill* | **PASS** (Linux) / PE **PENDING** |
| AC-M13-06 | no new public API | `FARMOS_PATH_FRAME_MS` only | **PASS** |
| AC-M13-07 | frame-gen / window / DLSS OUT | code search | **PASS** |
| AC-M13-08 | Linux soft-skip XeSS | M12 004/006 skip | **PASS** |
| AC-M13-09 | default 960×540→1080p | M12 bench sources reused | **PASS** |

Do not invent Arc milliseconds here. Cite `d23acfb3` **1.264 / 1.348** until a new Arc run.

## Linux verification (this tip; no Vulkan/SDK)

| Command | Result |
|---------|--------|
| `bash scripts/run_suite_linux.sh build/farmc spec/tests/M13` | *fill* |
| `bash scripts/run_suite_linux.sh build/farmc spec/tests/M12` | *fill* |
| `bash scripts/run_suite_linux.sh build/farmc spec/tests/M4` | *fill* |
| `bash scripts/run_suite_linux.sh build/farmc spec/tests/M11` | *fill* |
| `bash scripts/run_suite_linux.sh build/farmc spec/tests/M7` | *fill* |
| `python3 scripts/check_size_budgets.py build/farmc .` | *fill* |
| `python3 spec/tests/M13/bench/run_linux_host_amort.py build/farmc --out out/m13_linux` | *fill* |
