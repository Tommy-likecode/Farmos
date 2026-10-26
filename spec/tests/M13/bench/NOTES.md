# M13 notes

CLAIM recommendation: **PENDING** Arc; Linux product + harness ready. **No merge.**

| Item | Status |
|------|--------|
| Spec | FINAL OQ-M13-01..08 A. SHOULD ≥ **1.20×** both Pass1 and Pass2. |
| Baseline | Master `affc63ea`. Arc product `d23acfb3` Pass1 **1.264** / Pass2 **1.348**. |
| Host amort | In-process `FARMOS_PATH_FRAME_MS=1`. Keep dedicated XeSS VkDevice. |
| Engine | M12 persist / PSO disk cache / FAST_BUILD / dedicated device kept. |
| fill_aux / XeSS quality | Not used as the speed strategy on this tip (cannot verify AC-04 here). |
| M12 floor | ≥1.05× process-per-frame still reported. ROI pin unchanged. |
| Enabler / Swapper / OptiScaler | **OUT** |
