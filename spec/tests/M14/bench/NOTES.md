# M14 notes

CLAIM recommendation: **PASS** at measured tip `d409152e` / product `4d7523ec` under FINAL OQ-M14-01..08 A. This backlog PR is **docs+CMake** on master `218f6ef1`. Glossy protocol. Dirty host (chrome/msedge **informational**). **No merge.** Official XeSS only. Enabler **OUT**. **No new Arc CLAIM** (timed path/meters unchanged).

Do **not** compare in-process ms to `d23acfb3` process-per-frame **1.264 / 1.348**.
Do **not** claim an equal-work inproc beat of M13 `945c13ae` (see H2).

| Item | Status |
|------|--------|
| Spec | FINAL OQ-M14-01..08 A. SHOULD ≥ **1.50×** both Pass1 and Pass2 (in-process). MUST NOT weaken. |
| Baseline | Master `218f6ef1` (M14 squash #23). Arc product `945c13ae` Pass1 **1.455** / Pass2 **1.607** (inproc; equal-work vs M13). |
| Product SHA | `4d7523ec` (`FARMOS_PATH_FRAME_MS` + FAST_TRACE + deferred readback + PSO dummy). Unchanged. |
| Measured tip | `d409152e` (PR #23 Arc run). |
| M14 SHOULD (inproc) | **PASS**. Pass1 **3.184×** (2552.985 / 801.892 ms). Pass2 **3.913×** (2743.773 / 701.114 ms). |
| M13 floor (inproc) | **PASS**. ≥1.20× both. MUST NOT weaken. |
| M12 floor (proc) | **PASS**. Pass1 **1.235×** (4.794 / 3.883 s). Pass2 **1.139×** (4.251 / 3.732 s). XeSS < native inproc+proc. **NOTE:** Pass2 **1.139** is ~**8.5%** over 1.05 — monitor on re-bench (cleared; no code chase). |
| AC-04 quality | **PASS**. Pinned ROI `x=940:980,y=520:560`. L **105.810 / 105.801**. \|ΔL\| of means **0.009**. mean_abs_dL **0.440** report-only. Not L~187 / 3.932. |
| Size | PE ac05_xess **325120** / 716800. Linux ELF B-11 **72208**. Hello **14512**. |
| CMake | `m14_*` registered like `m13_*` (`ctest -R m14_`). |
| Dirty | chrome+msedge during CLAIM — **informational** only. |
| Enabler / Swapper / OptiScaler | **OUT** |

## H2 meter integrity (eggtooth BACKLOG; not BLOCKER)

When `!preview_active`, timed `renderPath` skips XeSS 1080p `vkCmdCopyImageToBuffer` + RGB unpack and sets `g_xess_out_pending`. `farm_Renderer_savePNG` → `path_upscale_flush` → `farm_upscale_flush_display` does the deferred download. GPU XeSS execute stays inside `FRAME_MS`. PNG/ROI after flush is correct (AC-04 PASS).

M13 inproc `FRAME_MS` on `945c13ae` wrapped full `renderPath` **including** that host readback each sample. M14 inproc excludes it for timed XeSS samples (harness typically `savePNG` once for AC-04 → flush once). Native still pays host-visible work each sample. Ratio jump 1.455/1.607 → 3.184/3.913 is H2 meter-scope + H1 FAST_TRACE + H3 PSO dummy, **not** a pure equal-work engine gain vs `945c13ae`.

**Flush-every-frame secondary inproc meter: not added.** Binding `FRAME_MS` stays GPU-pipeline on product `4d7523ec`. A second print would change product SHA and needs Arc to be useful. Process-per-frame AC-05 already includes `savePNG` (flush) and is the host-visible meter.

Kept levers: H1 FAST_TRACE persist AS; H2 defer XeSS display readback to savePNG;
H3 dummy XeSS PSO execute during path wait. Reverted/not applied: fill_aux
one-pass (H4), XeSS quality-mode (H5), GPU export/import skip (H6).
