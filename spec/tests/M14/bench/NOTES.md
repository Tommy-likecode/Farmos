# M14 notes

CLAIM recommendation: **PASS** at measured tip `d409152e` / product `4d7523ec` under FINAL OQ-M14-01..08 A. Glossy protocol. Dirty host (chrome/msedge informational). **No merge.** Official XeSS only. Enabler **OUT**.

Do **not** compare in-process ms to `d23acfb3` process-per-frame **1.264 / 1.348**.

| Item | Status |
|------|--------|
| Spec | FINAL OQ-M14-01..08 A. SHOULD ≥ **1.50×** both Pass1 and Pass2 (in-process). |
| Baseline | Master `47949c82`. Arc product `945c13ae` Pass1 **1.455** / Pass2 **1.607** (inproc). |
| Product SHA | `4d7523ec` (`FARMOS_PATH_FRAME_MS` + FAST_TRACE + deferred readback + PSO dummy). Unchanged for this docs sync. |
| Measured tip | `d409152e` (PR #23 HEAD at Arc run). |
| M14 SHOULD (inproc) | **PASS**. Pass1 **3.184×** (2552.985 / 801.892 ms). Pass2 **3.913×** (2743.773 / 701.114 ms). |
| M13 floor (inproc) | **PASS**. ≥1.20× both. |
| M12 floor (proc) | **PASS**. Pass1 **1.235×** (4.794 / 3.883 s). Pass2 **1.139×** (4.251 / 3.732 s). XeSS < native inproc+proc. |
| AC-04 quality | **PASS**. Pinned ROI `x=940:980,y=520:560`. L **105.810 / 105.801**. \|ΔL\| of means **0.009**. mean_abs_dL **0.440** report-only. |
| Size | PE ac05_xess **325120** / 716800. Linux ELF B-11 **72208**. Hello **14512**. |
| Enabler / Swapper / OptiScaler | **OUT** |

Kept levers: H1 FAST_TRACE persist AS; H2 defer XeSS display readback to savePNG;
H3 dummy XeSS PSO execute during path wait. Reverted/not applied: fill_aux
one-pass (H4), XeSS quality-mode (H5), GPU export/import skip (H6).
