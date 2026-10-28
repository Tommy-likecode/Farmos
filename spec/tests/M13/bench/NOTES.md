# M13 notes

CLAIM recommendation: **PASS** at measured tip `e4978668` / product `945c13ae` under FINAL OQ-M13-01..08 A. Glossy protocol. Dirty host (chrome/msedge informational). **No merge.** Official XeSS only. Enabler **OUT**.

Do **not** compare in-process ms to `d23acfb3` process-per-frame **1.264 / 1.348**.

| Item | Status |
|------|--------|
| Spec | FINAL OQ-M13-01..08 A. SHOULD ≥ **1.20×** both Pass1 and Pass2 (in-process). |
| Baseline | Master `affc63ea`. Arc product `d23acfb3` Pass1 **1.264** / Pass2 **1.348** (process-per-frame). |
| Product SHA | `945c13ae` (`FARMOS_PATH_FRAME_MS`). Unchanged for this docs sync. |
| M13 SHOULD (inproc) | **PASS**. Pass1 **1.455×** (2570.275 / 1766.219 ms). Pass2 **1.607×** (2575.415 / 1602.777 ms). |
| M12 floor (proc) | **PASS**. Pass1 **1.288×** (4.729 / 3.671 s). Pass2 **1.288×** (4.608 / 3.578 s). XeSS < native inproc+proc. |
| AC-04 quality | **PASS**. Pinned ROI `x=940:980,y=520:560`. L **105.810 / 105.801**. \|ΔL\| of means **0.009**. mean_abs_dL **0.440** report-only. |
| Size | PE ac05_xess **324096** / 716800. Linux ELF B-11 **68112**. Hello **14512**. |
| Enabler / Swapper / OptiScaler | **OUT** |
