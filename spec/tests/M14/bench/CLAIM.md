# M14 Arc CLAIM — OQ-M14-01..08 A — PASS

**Verdict:** **CLAIM PASS**  
**Do not merge** until eggtooth AUDIT PASS. Official XeSS only. Enabler NOT used.

| Field | Value |
|-------|--------|
| PR | #23 draft https://github.com/Tommy-likecode/Farmos/pull/23 |
| Branch | `cursor/m14-performance-d296` |
| Tip (measured) | `d409152e667155ac4d520482e0a7c9848508bdb0` |
| Product | `4d7523ec` (`FARMOS_PATH_FRAME_MS` + FAST_TRACE + deferred XeSS readback + PSO dummy; **unchanged**) |
| Master baseline | `47949c82` (M13 squash-merge PR #22; eggtooth AUDIT PASS) |
| Product baseline Arc | `945c13ae` inproc Pass1 **1.455** / Pass2 **1.607** (`FARMOS_PATH_FRAME_MS`) |
| Host | TommyLaptop Arc 2026-10-29 Asia/Shanghai |
| Dirty | chrome+msedge (informational) |
| Protocol | In-process `FARMOS_PATH_FRAME_MS` after 3 warmup (M14 SHOULD); process-per-frame AC-05 (M12 floor) |
| Spec | `spec/M14-performance.md` **FINAL** (OQ-M14-01..08 A) |
| SDK | Official XeSS only (`libxess.dll`). Enabler / Swapper / OptiScaler **OUT**. |
| PE ac05_xess | **325120** bytes (317.5 KiB) ≤700 KiB **PASS** |

## Binding meters (explicit)

1. **M14 SHOULD (OQ-M14-01/02 A):** in-process `FARMOS_PATH_FRAME_MS` after 3 warmup frames — Pass1 **and** Pass2 ≥ **1.50×**. Measured Pass1 **3.184×** (2552.985 / 801.892 ms) and Pass2 **3.913×** (2743.773 / 701.114 ms).
2. **M13 floor:** in-process ≥ **1.20×** both passes. **PASS** both (3.184 / 3.913). MUST NOT weaken.
3. **M12 floor:** process-per-frame AC-05 ≥ **1.05×**; XeSS wall strictly < native (inproc + proc). Measured Pass1 **1.235×** (4.794 / 3.883 s) and Pass2 **1.139×** (4.251 / 3.732 s).
4. **AC-04:** ROI L ∈ [80,200]; **|ΔL| of means ≤ 5.0** at pinned `x=940:980,y=520:560` @1080p 40×40; mean_abs_dL report-only. Measured L **105.810 / 105.801**, \|ΔL\| of means **0.009**, mean_abs_dL **0.440** report-only.
5. **PE ac05_xess:** **325120** bytes (317.5 KiB) ≤ **700 KiB**.
6. Do **not** compare in-process ms to `d23acfb3` process-per-frame **1.264 / 1.348**.

## Measured (claim_pass1 / claim_pass2, tip `d409152e`)

| Gate | Pass1 | Pass2 | Result |
|------|-------|-------|--------|
| inproc ratio (M14 SHOULD) | **3.184** (2552.985 / 801.892 ms) | **3.913** (2743.773 / 701.114 ms) | **PASS** both |
| inproc M13 floor ≥1.20× | **PASS** | **PASS** | **PASS** both |
| proc ratio (M12 floor) | **1.235** (4.794 / 3.883 s) | **1.139** (4.251 / 3.732 s) | **PASS** both |
| XeSS < native | PASS | PASS | **PASS** |
| ROI L native/xess | 105.810 / 105.801 | 105.810 / 105.801 | **PASS** |
| \|ΔL\| means | 0.009 | 0.009 | **PASS** |
| PE | 325120 | — | **PASS** |

Logs on TommyLaptop: `out/m14_arc/claim_pass1.log`, `out/m14_arc/claim_pass2.log`.

## AC-M14-01..10

| ID | Status | Evidence |
|----|--------|----------|
| AC-M14-01 | **PASS** | Linux M12 `001_upscale_off_cpu_twin` |
| AC-M14-02 | **PASS** | pinned ROI L **105.810 / 105.801** ∈ [80,200]; \|ΔL\| of means **0.009** ≤ 5.0; mean_abs_dL **0.440** report-only |
| AC-M14-03 | **PASS** | XeSS wall strictly < native, inproc and proc, both passes |
| AC-M14-04 | **PASS** | inproc Pass1 **3.184×** / Pass2 **3.913×** ≥ 1.50×; M13 floor ≥1.20× **PASS** both |
| AC-M14-05 | **PASS** | PE ac05_xess **325120** / 716800; Linux ELF B-11 **72208**; hello **14512** |
| AC-M14-06 | **PASS** | `FARMOS_PATH_FRAME_MS` only; no new public `.fm` API |
| AC-M14-07 | **PASS** | Official XeSS; Enabler / Swapper / OptiScaler NOT USED |
| AC-M14-08 | **PASS** | Linux M12 004/006 skip `requires_vulkan` |
| AC-M14-09 | **PASS** | M12 bench sources reused (960×540→1080p) |
| AC-M14-10 | **PASS** | inproc `FARMOS_PATH_FRAME_MS` named; no cross-meter compare |

## Linux verification (product `4d7523ec`; no Vulkan/SDK)

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
| H1 | Persist static AS with `PREFER_FAST_TRACE` (was FAST_BUILD) | **kept**. Arc inproc Pass1 **3.184×** / Pass2 **3.913×**. |
| H2 | Defer XeSS 1080p host download + RGB unpack until `savePNG` | **kept**. Inproc `frame_ms` no longer includes display readback; process-per-frame still includes `savePNG`. |
| H3 | Dummy XeSS execute during `prepare_xess` on dedicated device while path GPU is in flight | **kept**. Pass1 PSO overlap. |
| H4 | fill_aux one-pass (~0.06 ms @960×540) | **not applied** (M13 rejected). |
| H5 | XeSS quality-mode / sharpness | **not tried**. AC-04 already PASS on product. |
| H6 | GPU skip host color/depth round-trip via export/import | **not tried**. Dedicated XeSS `VkDevice` blocks a same-device copy. |

Dedicated XeSS VkDevice, inproc `FARMOS_PATH_FRAME_MS`, AS persist: kept from M12/M13. Product `4d7523ec` **unchanged** by this docs sync.
