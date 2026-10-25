# M12 Arc CLAIM — clear-m12-backlog (Linux only on this tip)

**Verdict:** **not claimed.** This branch clears M12 backlog after squash-merge `f5b87f8f`. Arc glossy AC-04 / AC-05 are **not** re-measured here (TommyLaptop / poteto Arc-smoke is separate). Do **not** treat this tip as Arc PASS.

| Field | Value |
|-------|--------|
| Intent | clear-m12-backlog draft from master `f5b87f8f` |
| Spec | `spec/M12-native-upscale.md` **FINAL** (OQ-M12-01..13 A; **2026-10-25 clarification** ROI pin + \|ΔL\| vs mean_abs_dL; gates unchanged) |
| SDK | Official XeSS only. Enabler / Swapper / OptiScaler **OUT**. |
| Fixtures | In-repo `spec/tests/M12/bench/` (`PATH_GLOSSY_VNDF`) |

## Gates (binding OQ-M12-12 A / OQ-M12-13 A) — unchanged

| Gate | Rule |
|------|------|
| AC-M12-05 binding | XeSS wall strictly < native (two independent process-per-frame warm medians) |
| AC-M12-05 SHOULD | ≥ **1.05×** (historical ≥1.2× / 1.5× superseded) |
| AC-M12-04 primary | Pinned ROI `x=940:980,y=520:560` @1080p (40×40); both lanes L ∈ **[80, 200]**; **\|ΔL\| = \|L_xess − L_native\| of means** ≤ 5.0 |
| AC-M12-04 mean_abs / mean_abs_dL / png_tol / max_abs | **report-only** |
| AC-M12-07 PE | B-11 ≤ 700 KiB |

Historical OQ-M12-10 A SHOULD ≥ **1.2×** and OQ-M12-11 A band **[80, 140]** are **superseded** (documented; not binding). Helper logs must **not** print `should_1_2=FAIL` / `png_tol_12=FAIL` as if those were live gates.

## Baseline (merged glossy Arc; not this tip)

Docs / product `7f0a080` / `fd31e46` (clean host):

| Gate | Measured | Note |
|------|----------|------|
| AC-05 Pass1 | **1.052** (8.095 / 7.694) | ~0.2% headroom over 1.05× — backlog target ≥1.08–1.10 |
| AC-05 Pass2 | **1.162** | keep ≥1.05×; XeSS < native |
| AC-04 ROI L | **187.228 / 187.877** | ∈ [80, 200] |
| AC-04 **\|ΔL\|** | **≈ 0.650** | \|187.877 − 187.228\| of **means** |
| AC-04 mean_abs_dL | **3.932** | **report-only** — was mislabeled \|ΔL\| |
| PE B-11 | **72704** / 716800 | Windows PE of that product |

## This tip (product)

- XeSS CreateContext / init / first-submit PSO compile prefers a **dedicated** VkDevice so it cannot `DeviceWaitIdle` the in-flight path-tracer queue (Pass1 residual-init hypothesis). Borrow remains fallback. Official `xessVKGetRequired*` extras unchanged. Enabler still OUT.
- Do **not** claim Pass1 ≥1.08 until TommyLaptop measures this tip.

## Linux verification (this branch; no Vulkan/SDK on this host)

See the PR / run notes for exact suite commands and ELF B-11. Expected: M12 + M4 + M11 + M7 green (GPU fixtures soft-skip).
