# M14 notes

CLAIM recommendation: **PENDING Arc** until TommyLaptop fills inproc Pass1/Pass2
under FINAL OQ-M14-01..08 A. Glossy protocol. **No merge.** Official XeSS only.
Enabler **OUT**.

Do **not** compare in-process ms to `d23acfb3` process-per-frame **1.264 / 1.348**.

| Item | Status |
|------|--------|
| Spec | FINAL OQ-M14-01..08 A. SHOULD ≥ **1.50×** both Pass1 and Pass2 (in-process). |
| Baseline | Master `47949c82`. Arc product `945c13ae` Pass1 **1.455** / Pass2 **1.607** (inproc). |
| Product SHA | `4d7523ec` (runtime path/meter). |
| M14 SHOULD (inproc) | **PENDING Arc**. Need ≥1.50× both. |
| M13 floor (inproc) | **PENDING Arc**. Must remain ≥1.20× both. |
| M12 floor (proc) | **PENDING Arc**. ≥1.05×; XeSS < native. |
| AC-04 quality | **PENDING Arc**. Pinned ROI `x=940:980,y=520:560`. |
| Size | Linux ELF B-11 **72208** / 716800. Hello **14512**. Arc PE **PENDING**. |
| Enabler / Swapper / OptiScaler | **OUT** |

Kept levers: H1 FAST_TRACE persist AS; H2 defer XeSS display readback to savePNG;
H3 dummy XeSS PSO execute during path wait. Reverted/not applied: fill_aux
one-pass (H4), XeSS quality-mode (H5), GPU export/import skip (H6).
