# M13 notes

CLAIM recommendation: **PENDING** Arc. Linux product + harness green. **No merge.**

| Item | Status |
|------|--------|
| Spec | FINAL OQ-M13-01..08 A. SHOULD ≥ **1.20×** both Pass1 and Pass2. |
| Baseline | Master `affc63ea`. Arc product `d23acfb3` Pass1 **1.264** / Pass2 **1.348**. |
| Product SHA | `945c13ae` (`FARMOS_PATH_FRAME_MS`). |
| Host amort | Linux CPU 32×32 proc/inproc **6.06×** (median of 5). Spawn is the limiter on this host. |
| fill_aux one-pass | Measured 0.06 ms saved at 960×540. Not applied. |
| XeSS quality | Not touched. |
| M12 floor | ≥1.05× process-per-frame still reported by `run_arc_ac.py`. ROI pin unchanged. |
| Size | ELF B-11 **68112** / 716800. Hello **14512**. |
| Enabler / Swapper / OptiScaler | **OUT** |
