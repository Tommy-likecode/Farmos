# M13 Arc CLAIM — OQ-M13-01..08 A — PASS

**Verdict:** **CLAIM PASS**  
**Do not merge** until eggtooth AUDIT PASS. Official XeSS only. Enabler NOT used.

| Field | Value |
|-------|--------|
| PR | #22 draft https://github.com/Tommy-likecode/Farmos/pull/22 |
| Branch | `cursor/m13-performance-a23d` |
| Tip (measured) | `e4978668cc477fd131c7aea71472967b248e5727` |
| Product | `945c13aeaf7fb086bfc718c7d869358441706a17` (`FARMOS_PATH_FRAME_MS`; **unchanged**) |
| Master baseline | `affc63ea00f147f9ad09a0d559d0650e4543287a` |
| Product baseline Arc | `d23acfb3` Pass1 **1.264** / Pass2 **1.348** (**process-per-frame** seconds) |
| Host | TommyLaptop Arc 2026-10-28 Asia/Shanghai |
| Dirty | chrome+msedge (informational) |
| Protocol | In-process `FARMOS_PATH_FRAME_MS` after 3 warmup (M13 SHOULD); process-per-frame AC-05 (M12 floor) |
| Spec | `spec/M13-performance.md` **FINAL** (OQ-M13-01..08 A) |
| SDK | Official XeSS only (`libxess.dll`). Enabler / Swapper / OptiScaler **OUT**. |
| PE ac05_xess | **324096** bytes (316.5 KiB) ≤700 KiB **PASS** |

## Binding meters (explicit)

1. **M13 SHOULD (OQ-M13-01/02 A):** in-process `FARMOS_PATH_FRAME_MS` after 3 warmup frames — Pass1 **and** Pass2 ≥ **1.20×**. Measured Pass1 **1.455×** (2570.275 / 1766.219 ms) and Pass2 **1.607×** (2575.415 / 1602.777 ms).
2. **M12 floor:** process-per-frame AC-05 ≥ **1.05×**; XeSS wall strictly < native (inproc + proc). Measured Pass1 **1.288×** (4.729 / 3.671 s) and Pass2 **1.288×** (4.608 / 3.578 s).
3. **AC-04:** ROI L ∈ [80,200]; **|ΔL| of means ≤ 5.0** at pinned `x=940:980,y=520:560` @1080p 40×40; mean_abs_dL report-only. Measured L **105.810 / 105.801**, \|ΔL\| of means **0.009**, mean_abs_dL **0.440** report-only.
4. **PE ac05_xess:** **324096** bytes (316.5 KiB) ≤ **700 KiB**.
5. Do **not** compare in-process ms to `d23acfb3` process-per-frame **1.264 / 1.348**.

## Measured (claim_pass1 / claim_pass2, tip `e4978668`)

| Gate | Pass1 | Pass2 | Result |
|------|-------|-------|--------|
| inproc ratio (M13 SHOULD) | **1.455** (2570.275 / 1766.219 ms) | **1.607** (2575.415 / 1602.777 ms) | **PASS** both |
| proc ratio (M12 floor) | **1.288** (4.729 / 3.671 s) | **1.288** (4.608 / 3.578 s) | **PASS** both |
| XeSS < native | PASS | PASS | **PASS** |
| ROI L native/xess | 105.810 / 105.801 | 105.810 / 105.801 | **PASS** |
| \|ΔL\| means | 0.009 | 0.009 | **PASS** |
| PE | 324096 | — | **PASS** |

Logs on TommyLaptop: `out/m13_arc/claim_pass1.log`, `out/m13_arc/claim_pass2.log`.

## AC-M13-01..09

| ID | Status | Evidence |
|----|--------|----------|
| AC-M13-01 | **PASS** | Linux M12 `001_upscale_off_cpu_twin` |
| AC-M13-02 | **PASS** | pinned ROI L **105.810 / 105.801** ∈ [80,200]; \|ΔL\| of means **0.009** ≤ 5.0; mean_abs_dL **0.440** report-only |
| AC-M13-03 | **PASS** | XeSS wall strictly < native, inproc and proc, both passes |
| AC-M13-04 | **PASS** | inproc Pass1 **1.455×** / Pass2 **1.607×** ≥ 1.20× |
| AC-M13-05 | **PASS** | PE ac05_xess **324096** / 716800; Linux ELF B-11 **68112**; hello **14512** |
| AC-M13-06 | **PASS** | `FARMOS_PATH_FRAME_MS` only; no new public `.fm` API |
| AC-M13-07 | **PASS** | Official XeSS; Enabler / Swapper / OptiScaler NOT USED |
| AC-M13-08 | **PASS** | Linux M12 004/006 skip `requires_vulkan` |
| AC-M13-09 | **PASS** | M12 bench sources reused (960×540→1080p) |

## Linux verification (product `945c13ae`; no Vulkan/SDK)

| Command | Result |
|---------|--------|
| `bash scripts/run_suite_linux.sh build/farmc spec/tests/M13` | 1/1 PASS |
| `bash scripts/run_suite_linux.sh build/farmc spec/tests/M12` | 8/8 (004/006 skip `requires_vulkan`) |
| `bash scripts/run_suite_linux.sh build/farmc spec/tests/M4` | 24/24 PASS |
| `bash scripts/run_suite_linux.sh build/farmc spec/tests/M11` | 4/4 PASS |
| `bash scripts/run_suite_linux.sh build/farmc spec/tests/M7` | 9/9 (008 skip `requires_vulkan`) |
| `python3 scripts/check_size_budgets.py build/farmc .` | PASS — ELF B-11 **68112** / 716800; hello **14512** |
| `python3 spec/tests/M13/bench/run_linux_host_amort.py build/farmc --out out/m13_linux` | PASS. See `linux_host_amort.log`. |

## Hypotheses

| Id | Hypothesis | Verdict |
|----|------------|---------|
| H1 | In-process `frame_ms` amortizes process-per-frame spawn (OQ-03 host) | **kept**. Arc inproc Pass1 **1.455×** / Pass2 **1.607×**. |
| H2 | Merge fill_aux two-pass depth init into one pass | **not applied**. 960×540 micro 0.58→0.52 ms. |
| H3 | XeSS quality-mode / sharpness | **not tried**. AC-04 already PASS on product. |
| H4 | GPU skip host color/depth round-trip | **not tried**. |

Dedicated XeSS VkDevice, PSO disk cache, AS persist, FAST_BUILD: kept as already on master `affc63ea`.
