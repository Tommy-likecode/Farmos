# M14 Arc CLAIM — OQ-M14-01..08 A — PASS

**Verdict:** **CLAIM PASS** (Arc numbers from measured tip `d409152e`; this backlog PR is docs+CMake only)  
**Do not merge.** Official XeSS only. Enabler NOT used. **No new Arc CLAIM** (timed path/meters unchanged).

| Field | Value |
|-------|--------|
| PR | draft (clear-m14-backlog after squash-merge #23) |
| Branch | `cursor/clear-m14-backlog-782e` |
| Tip (measured Arc) | `d409152e667155ac4d520482e0a7c9848508bdb0` |
| Master tip | `218f6ef1` (M14 squash-merge PR #23; docs lineage `6d1d9c81`; product `4d7523ec`) |
| Product | `4d7523ec` (`FARMOS_PATH_FRAME_MS` + FAST_TRACE + deferred XeSS readback + PSO dummy; **unchanged**) |
| Product baseline Arc | `945c13ae` inproc Pass1 **1.455** / Pass2 **1.607** (`FARMOS_PATH_FRAME_MS`; equal-work vs M13 — see H2) |
| Host | TommyLaptop Arc 2026-10-29 Asia/Shanghai |
| Dirty | chrome+msedge present during CLAIM (**informational**; not a gate) |
| Protocol | In-process `FARMOS_PATH_FRAME_MS` after 3 warmup (M14 SHOULD); process-per-frame AC-05 (M12 floor) |
| Spec | `spec/M14-performance.md` **FINAL** (OQ-M14-01..08 A). SHOULD ≥ **1.50×** both; M13 ≥1.20× / M12 ≥1.05× floors **unchanged**. |
| SDK | Official XeSS only (`libxess.dll`). Enabler / Swapper / OptiScaler **OUT**. |
| PE ac05_xess | **325120** bytes (317.5 KiB) ≤700 KiB **PASS** |

## Binding meters (explicit)

1. **M14 SHOULD (OQ-M14-01/02 A):** in-process `FARMOS_PATH_FRAME_MS` after 3 warmup frames — Pass1 **and** Pass2 ≥ **1.50×**. Measured Pass1 **3.184×** (2552.985 / 801.892 ms) and Pass2 **3.913×** (2743.773 / 701.114 ms). MUST NOT weaken.
2. **M13 floor:** in-process ≥ **1.20×** both passes. **PASS** both (3.184 / 3.913). MUST NOT weaken.
3. **M12 floor:** process-per-frame AC-05 ≥ **1.05×**; XeSS wall strictly < native (inproc + proc). Measured Pass1 **1.235×** (4.794 / 3.883 s) and Pass2 **1.139×** (4.251 / 3.732 s). **NOTE:** Pass2 proc **1.139** is ~**8.5%** over 1.05 — monitor on re-bench (cleared; no code chase).
4. **AC-04:** ROI L ∈ [80,200]; **|ΔL| of means ≤ 5.0** at pinned `x=940:980,y=520:560` @1080p 40×40; mean_abs_dL report-only. Measured L **105.810 / 105.801**, \|ΔL\| of means **0.009**, mean_abs_dL **0.440** report-only. Do **not** cite lock-time L~187 / mean_abs_dL 3.932 as this pinned CLAIM.
5. **PE ac05_xess:** **325120** bytes (317.5 KiB) ≤ **700 KiB**.
6. Do **not** compare in-process ms to `d23acfb3` process-per-frame **1.264 / 1.348**.
7. **H2 meter integrity:** inproc `FRAME_MS` is **GPU-pipeline**. XeSS 1080p host download + RGB unpack is deferred out of timed `renderPath` (`g_xess_out_pending`; flush on `savePNG` via `path_upscale_flush`). GPU XeSS execute stays inside the timed path. PNG/ROI still produced after flush (AC-04 **PASS**). Native lane still pays its host-visible path each sample → **asymmetric** vs XeSS. **Do not claim an equal-work beat of M13 `945c13ae`** (that CLAIM wrapped full `renderPath` including XeSS host readback each sample). Observed XeSS drop ~1766→802 / ~1603→701 ms is H2 meter-scope + H1/H3, not a pure equal-work engine gain vs `945c13ae`.
8. **No flush-every-frame secondary inproc meter:** not added. Binding `FRAME_MS` must stay as shipped on product `4d7523ec`. A second inproc print (`frame_ms_flush=`) would be a product SHA change and is useless without a new Arc run. Process-per-frame AC-05 **already includes `savePNG` → flush** and is the existing host-visible meter (M12 floor). Preview still unpacks immediately.

## Measured (claim_pass1 / claim_pass2, tip `d409152e`)

| Gate | Pass1 | Pass2 | Result |
|------|-------|-------|--------|
| inproc ratio (M14 SHOULD) | **3.184** (2552.985 / 801.892 ms) | **3.913** (2743.773 / 701.114 ms) | **PASS** both |
| inproc M13 floor ≥1.20× | **PASS** | **PASS** | **PASS** both |
| proc ratio (M12 floor) | **1.235** (4.794 / 3.883 s) | **1.139** (4.251 / 3.732 s) (~8.5% over 1.05) | **PASS** both |
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
| AC-M14-10 | **PASS** | inproc `FARMOS_PATH_FRAME_MS` named; no cross-meter compare; H2 GPU-pipeline scope documented (not equal-work vs `945c13ae`) |

## Linux verification (product `4d7523ec`; no Vulkan/SDK)

| Command | Result |
|---------|--------|
| `bash scripts/run_suite_linux.sh build/farmc spec/tests/M14` | 1/1 PASS |
| `ctest --test-dir build -R 'm12_|m13_|m14_'` | 10/10 PASS (`m14_001_m14_stub` registered) |
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
| H2 | Defer XeSS 1080p host download + RGB unpack until `savePNG` | **kept**. GPU XeSS still timed; host RGB deferred to `savePNG`. Inflates inproc vs `945c13ae` equal-work. No flush-every-frame secondary inproc meter (proc AC-05 already flushes). |
| H3 | Dummy XeSS execute during `prepare_xess` on dedicated device while path GPU is in flight | **kept**. Pass1 PSO overlap. |
| H4 | fill_aux one-pass (~0.06 ms @960×540) | **not applied** (M13 rejected). |
| H5 | XeSS quality-mode / sharpness | **not tried**. AC-04 already PASS on product. |
| H6 | GPU skip host color/depth round-trip via export/import | **not tried**. Dedicated XeSS `VkDevice` blocks a same-device copy. |

Dedicated XeSS VkDevice, inproc `FARMOS_PATH_FRAME_MS`, AS persist: kept from M12/M13. Product `4d7523ec` **unchanged** (docs+CMake backlog only; no Arc remeasure).
