# M14 Arc CLAIM — OQ-M14-01..08 A — PENDING Arc

**Verdict:** **PENDING Arc** (Linux draft PR). Official XeSS only. Enabler NOT used.

| Field | Value |
|-------|--------|
| PR | draft (this branch) |
| Branch | `cursor/m14-performance-d296` |
| Tip (docs / Linux) | `3c477a931ffc093049e6db6d2eb261c65c9d69c9` |
| Product | `4d7523ec` (`FARMOS_PATH_FRAME_MS` + FAST_TRACE + deferred XeSS readback + PSO dummy) |
| Master baseline | `47949c82` (M13 squash-merge PR #22; eggtooth AUDIT PASS) |
| Product baseline Arc | `945c13ae` inproc Pass1 **1.455** / Pass2 **1.607** (`FARMOS_PATH_FRAME_MS`) |
| Host | TommyLaptop Arc — **PENDING** poteto fill |
| Protocol | In-process `FARMOS_PATH_FRAME_MS` after 3 warmup (M14 SHOULD); process-per-frame AC-05 (M12 floor) |
| Spec | `spec/M14-performance.md` **FINAL** (OQ-M14-01..08 A) |
| SDK | Official XeSS only (`libxess.dll`). Enabler / Swapper / OptiScaler **OUT**. |
| PE ac05_xess | **PENDING Arc** ≤700 KiB |

## Binding meters (explicit)

1. **M14 SHOULD (OQ-M14-01/02 A):** in-process `FARMOS_PATH_FRAME_MS` after ≥3 warmup — Pass1 **and** Pass2 ≥ **1.50×**. Report ms. XeSS wall strictly < native.
2. **M13 floor:** inproc ≥ **1.20×** both passes. MUST NOT weaken.
3. **M12 floor:** process-per-frame AC-05 ≥ **1.05×** both; XeSS < native; ROI pin `x=940:980,y=520:560` @1080p L both [80,200] and \|ΔL\| of means ≤5.0; OFF; trap 122; upscale-glue PE ≤700 KiB.
4. Do **not** compare in-process ms to `d23acfb3` process-per-frame **1.264 / 1.348**.

## Measured (claim_pass1 / claim_pass2)

| Gate | Pass1 | Pass2 | Result |
|------|-------|-------|--------|
| inproc ratio (M14 SHOULD) | **PENDING Arc** | **PENDING Arc** | — |
| inproc ms native / xess | **PENDING Arc** | **PENDING Arc** | — |
| proc ratio (M12 floor) | **PENDING Arc** | **PENDING Arc** | — |
| XeSS < native | **PENDING Arc** | **PENDING Arc** | — |
| ROI L native/xess | **PENDING Arc** | **PENDING Arc** | — |
| \|ΔL\| means | **PENDING Arc** | **PENDING Arc** | — |
| PE | **PENDING Arc** | — | — |

Baseline product `945c13ae` (do not overwrite until Arc numbers exist): Pass1 **1.455×** (2570.275 / 1766.219 ms) / Pass2 **1.607×** (2575.415 / 1602.777 ms).

## AC-M14-01..10

| ID | Status | Evidence |
|----|--------|----------|
| AC-M14-01 | **PENDING** Linux OFF twin + Arc | M12 `001_upscale_off_cpu_twin` |
| AC-M14-02 | **PENDING Arc** | pinned ROI; L [80,200]; \|ΔL\| of means ≤5.0 |
| AC-M14-03 | **PENDING Arc** | XeSS wall strictly < native, inproc and proc |
| AC-M14-04 | **PENDING Arc** | inproc Pass1 **and** Pass2 ≥ **1.50×**; M13 ≥1.20× floor |
| AC-M14-05 | **PASS** Linux ELF / **PENDING Arc PE** | hello **14512**; ELF B-11 **72208** / 716800 |
| AC-M14-06 | **PASS** (code) | `FARMOS_PATH_FRAME_MS` kept; no new public `.fm` API |
| AC-M14-07 | **PASS** (code) | Official XeSS; Enabler / Swapper / OptiScaler NOT USED |
| AC-M14-08 | **PASS** Linux | M12 004/006 skip `requires_vulkan`; M7 008 skip |
| AC-M14-09 | **PASS** (harness) | M12 bench sources reused (960×540→1080p) |
| AC-M14-10 | **PASS** (harness) | inproc `FARMOS_PATH_FRAME_MS` named; no cross-meter compare |

## Linux verification (no Vulkan/SDK)

| Command | Result |
|---------|--------|
| `bash scripts/run_suite_linux.sh build/farmc spec/tests/M14` | 1/1 PASS |
| `bash scripts/run_suite_linux.sh build/farmc spec/tests/M12` | 8/8 (004/006 skip `requires_vulkan`) |
| `bash scripts/run_suite_linux.sh build/farmc spec/tests/M13` | 1/1 PASS |
| `bash scripts/run_suite_linux.sh build/farmc spec/tests/M4` | 24/24 PASS |
| `bash scripts/run_suite_linux.sh build/farmc spec/tests/M11` | 4/4 PASS |
| `bash scripts/run_suite_linux.sh build/farmc spec/tests/M7` | 9/9 (008 skip `requires_vulkan`) |
| `python3 scripts/check_size_budgets.py build/farmc .` | PASS — ELF B-11 **72208** / 716800; hello **14512** |
| `python3 spec/tests/M14/bench/run_linux_host_amort.py build/farmc --out out/m14_linux` | PASS. See `linux_host_amort.log`. |

## Hypotheses

| Id | Hypothesis | Verdict |
|----|------------|---------|
| H1 | Persist static AS with `PREFER_FAST_TRACE` (was FAST_BUILD) | **kept** (engine). Arc measure PENDING. Revert if ratio or quality regresses. |
| H2 | Defer XeSS 1080p host download + RGB unpack until `savePNG` | **kept** (fewer host RTs). Inproc `frame_ms` no longer includes display readback; process-per-frame still includes `savePNG`. |
| H3 | Dummy XeSS execute during `prepare_xess` on dedicated device while path GPU is in flight | **kept** (PSO warmup). First-frame only; timed frames skip. |
| H4 | fill_aux one-pass (~0.06 ms @960×540) | **not applied** (M13 rejected; too small vs 1.50× gap). |
| H5 | XeSS quality-mode / sharpness | **not tried**. AC-04 already PASS on product; quality MUST still pass. |
| H6 | GPU skip host color/depth round-trip via export/import across dedicated XeSS VkDevice | **not tried**. Dedicated device blocks a same-device copy; export/import left for a later wave if H1–H3 miss 1.50×. |

Dedicated XeSS VkDevice, inproc `FARMOS_PATH_FRAME_MS`, AS persist: kept from M12/M13.
