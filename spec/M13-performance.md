# Farmos M13 — Performance / more speed (post-M12)

**Status:** **FINAL** (PM/user 2026-10-26: OQ-M13-01 .. OQ-M13-08 all **accepted A**)  
**Milestone:** M13 — performance / more speed after M12 native upscale  
**Depends on:** `spec/M1-core.md` .. `spec/M12-native-upscale.md` (all FINAL)  
**Normative keywords:** MUST / SHOULD / MAY per RFC 2119.  
**Date:** 2026-10-26 (DRAFT); **2026-10-26 FINAL** (PM/user lock OQ-M13-01..08 A)

This document specifies Farmos M13: a **next speed wave** on TommyLaptop Arc (XeSS path) that raises display-equivalent throughput **without regressing M12 binding acceptance criteria**. It does **not** specify compiler, runtime, or stdlib implementation code. It does **not** edit M1–M12 milestone markdown bodies; normative amendments relative to those milestones live in §11 of this file (binding under OQ-M13-01..08 A).

Open questions: **none**. OQ-M13-01 .. OQ-M13-08 all **accepted A** (PM lock 2026-10-26; user locked all as A). Binding under A: SHOULD ≥ **1.20×** both Pass1+Pass2; host+engine+aux/PSO; keep 960×540→1080p; no new public API; MUST NOT regress M12 binding; ≤700 KiB glue; frame-gen/window/DLSS OUT; Arc primary + Linux soft-skip. poteto stays **HOLD** until Projects Manager assigns coding after this FINAL.

Related: `/workspace/farmos/PLAN.md` (M13 stub), `spec/M8-path-speed.md`, `spec/M9-hardware-rt.md`, `spec/M10-denoise-interactive.md`, `spec/M11-glossy-continuation.md`, `spec/M12-native-upscale.md` (**FINAL**), `spec/tests/M13/README.md`.

**Binding context (document; do not invent absolute ms):**

| Item | Value |
|------|-------|
| Master tip (clear-m12-backlog squash-merged) | `affc63ea` (PR #21; eggtooth AUDIT PASS) |
| M12 FINAL gates (unchanged floor) | OQ-M12-12 A SHOULD ≥ **1.05×**; OQ-M12-13 A ROI L **[80, 200]** + **\|ΔL\| of means ≤ 5.0**; ROI pinned **`x=940:980,y=520:560`** @1080p **40×40**; mean_abs_dL report-only; XeSS wall **strictly <** native (MUST) |
| Latest Arc CLAIM baseline (product) | tip / product **`d23acfb3`**: Pass1 **1.264** / Pass2 **1.348** under OQ-M12-12 A; dedicated XeSS `VkDevice` already in |
| Soft backlog | Non-blocking (stale tip cites L~187 / 3.932 beside pinned ROI; LF drift) — §8 notes only; MUST NOT block M13 |
| Historical M12 RCA | Process-per-frame overhead capped early **1.5×**; fill_aux→path depth/MV, persistent Vk, PSO disk cache, device borrow landed; more-headroom tips varied |
| Prior speed waves | M8 engine RR/MIS/Vulkan occupancy; M9 HW RT; M10 denoise; M11 glossy; M12 upscale — M13 is the **next** wave |

**Product intent (PM / user GO draft-m13-perf):**

1. Deliver **more display-equivalent speed** on TommyLaptop Intel Arc (XeSS path) after warm-up.
2. Prefer **engine + host amortization** levers first; do **not** chase speed by lowering product spp or changing silver artwork.
3. **MUST NOT** regress M12 binding ACs (XeSS < native; ROI band + \|ΔL\|; OFF goldens; trap 122; size glue; frame-gen OUT; no window).
4. M12 OQ-12 A ≥ **1.05×** remains a **floor** (do not weaken). Historical M12 OQ-10 A ≥ **1.2×** is **reclaimed as M13 SHOULD** (OQ-M13-01 A; new milestone gate; does not reopen M12 OQ tables).
5. Soft backlog (stale tip / LF) is **informational only**.

**Target platform:** Windows x64 primary (TommyLaptop Arc) for speed ACs; Linux CI soft-skip / skip when XeSS/Vulkan absent — CPU OFF-path twins still required.

---

## 1. Scope and non-goals

### 1.1 In scope (OQ-M13-03 A — binding)

M13 MUST deliver measurable speed gains via one or more of:

| Lever | Requirement (OQ-M13-03 A binding) |
|-------|-------------------------------|
| Host / process amortization | In-process multi-frame benches; reduce process-per-frame overhead for AC timing; keep dedicated XeSS `VkDevice` |
| Path / Vulkan RT engine | Further occupancy; AS rebuild vs refit; spp batching; fewer host round-trips — **additive** to M8/M9 |
| Aux / MV / depth / jitter | `fill_aux` efficiency (depth/MV/jitter path cost) |
| PSO / pipeline cache / warmup | Deepen existing PSO disk cache / shader warmup if measurable |
| XeSS/FSR quality-mode tuning | Sharpness / history / quality-mode OK **if** M12 AC-04 still PASSes on pinned ROI |
| Default render→display | **Keep** M12 default **960×540 → 1920×1080** for binding AC continuity |
| CPU/host stills when upscale OFF | Secondary; report-only unless an AC later binds it |

### 1.2 Explicit non-goals (OUT unless an OQ locks IN)

| Item | Note |
|------|------|
| Frame generation (DLSS-G / FSR-FG / XeSS-FG) | **OUT** (OQ-M13-07 A; M12-09 stands) |
| OS window / swapchain | **OUT** (M10-01 / M12-01 stand) |
| DLSS / NGX / Enabler / Swapper / OptiScaler | **OUT forever** (M12 stands) |
| Changing silver artwork composition | **OUT** |
| Lowering product spp as the speed strategy | **OUT** (engine/host first; M8 spirit) |
| Opt-in lower render-res (e.g. 720p→1080p) | **OUT** (OQ-M13-03 A; OQ-03 B rejected) |
| Regressing M12 OFF goldens / pinned ROI gates | **OUT** |
| Editing M1–M12 milestone markdown bodies | Amendments in §11 only |
| Full game engine / input loop | **OUT** |
| OptiX / GPU raster / accelerating M3 `render()` | **OUT** |

### 1.3 Design constraints (OQ-M13-05 A — binding)

- M12 AC-M12-04 / AC-M12-05 remain a **floor**: XeSS wall **strictly <** native (MUST); ROI L both lanes in **[80, 200]**; **\|ΔL\| = \|L_xess − L_native\| ≤ 5.0** on pinned ROI **`x=940:980,y=520:560`** @1080p 40×40; mean_abs_dL / png_tol / max_abs **report-only**.
- M12 OQ-12 A SHOULD ≥ **1.05×** MUST NOT be weakened; M13 raises the SHOULD floor (OQ-M13-01 A).
- OFF CPU twins, trap **122**, upscale-glue PE ≤ **700 KiB**, hello ≤ **20 KiB**, prior ≤300/400/500/600 caps remain.
- Pixel values under upscale OFF MUST NOT depend on thread scheduling on CPU (M4 determinism).
- Do **not** invent absolute millisecond caps until measured on a comparable harness; prefer ratios + tip SHAs (`d23acfb3`, `affc63ea`).

---

## 2. Relationship to M8–M12

| Milestone | What it already shipped | What M13 adds |
|-----------|-------------------------|---------------|
| M8 | RR/MIS opt-in; Vulkan occupancy / spp batching; ≥2× Vulkan / ≥1.5× CPU historical | Further engine occupancy; fewer host round-trips; AS rebuild vs refit |
| M9 | HW RT (`PATH_BACKEND_VULKAN_RT`); wall ≤5000 ms / ≥5× (OQ-M9-08 A) | Additive RT/path scheduling; no reopen of OptiX / full RT pipeline |
| M10 | Spatial denoise; progressive API; **no** window | Do **not** reopen interactive progressive wall ACs as M13 focus (OQ-M13-04 C rejected) |
| M11 | Opt-in glossy VNDF | Unchanged; M13 benches use glossy ON as M12 CLAIM did |
| M12 | Native XeSS/FSR; OQ-12 A ≥1.05×; OQ-13 A ROI [80,200] + \|ΔL\|≤5; pinned ROI; dedicated XeSS device | **Next** speed wave; reclaim historical ≥1.2× as M13 SHOULD (OQ-01 A); host amortization + engine + aux/PSO |

M13 does **not** reopen M12 OQ decision tables. Supersession of M12 SHOULD floor (if any) is **only** by raising a **new** M13 SHOULD above the M12 floor under locked OQ-M13-01 — M12 ≥1.05× remains the regression floor.

---

## 3. Candidate levers (survey → scope)

| # | Lever | IN (OQ-M13-03 A)? | Notes |
|---|-------|----------------------|-------|
| 1 | Host / process amortization (in-process multi-frame; keep dedicated XeSS VkDevice) | **IN** | Historical process-per-frame capped early 1.5× |
| 2 | Path / Vulkan RT engine (occupancy, AS rebuild vs refit, spp batching, fewer host RTs) | **IN** | Additive to M8/M9 |
| 3 | Aux / MV / depth / jitter (`fill_aux` cost) | **IN** | fill_aux→path depth/MV already landed; deepen efficiency |
| 4 | PSO / pipeline cache / shader warmup | **IN** if measurable | Partly landed in M12 tips |
| 5 | XeSS/FSR quality mode / sharpness / history | **IN** if AC-04 holds | Quality MUST still pass M12 AC-04 |
| 6 | Optional lower render-res (e.g. 720p→1080p) | **OUT** (OQ-03 B rejected) | Default keep 960×540→1080p |
| 7 | CPU/host stills when upscale OFF | Secondary | Report-only unless later AC binds |

---

## 4. API surface (OQ-M13-04 A — binding)

**Binding (OQ-M13-04 A):** No new `.fm` public API required. MAY add env / internal perf hooks. M12 `setPathUpscale` / `getPathUpscale` / `PATH_UPSCALE_*` and prior path APIs **unchanged**.

| Option | Summary | Lock |
|--------|---------|------|
| **A (ACCEPTED)** | No new public API; MAY env / internal hooks | **LOCKED** |
| B | Additive `setPathPerf` / similar | Rejected |
| C | Reopen interactive progressive wall ACs from M10 as M13 focus | Rejected |

---

## 5. Acceptance criteria (binding under OQ-M13-01..08 A)

All ACs below are **binding** under OQ-M13-01..08 **accepted A**:

| ID | Criterion |
|----|-----------|
| AC-M13-01 | M12 OFF CPU twin(s) still **PASS** (M4 SHA / existing OFF goldens). |
| AC-M13-02 | M12 AC-M12-04 quality **PASS** on pinned ROI under M13 tip: both lanes L in **[80, 200]**; **\|ΔL\| ≤ 5.0**; mean_abs_dL / png_tol / max_abs report-only. |
| AC-M13-03 | M12 XeSS wall **strictly <** native **MUST PASS** after warm-up (unchanged binding). |
| AC-M13-04 | Timing (script): SHOULD display-equivalent ≥ **1.20×** (native_median / xess_median) on TommyLaptop Arc after warm-up on M12 bench scene (**960×540→1080p**, glossy ON as M12 CLAIM) for **both** Pass1 and Pass2. Document baseline tip `d23acfb3` Pass1 **1.264** / Pass2 **1.348**. M12 OQ-12 A ≥ **1.05×** remains floor. Report absolute ms (no hard ms cap until measured). |
| AC-M13-05 | Size / DCE: hello ≤ **20 KiB**; upscale unused DCE; upscale-glue-linked ≤ **700 KiB** PE; prior ≤300/400/500/600 unchanged (OQ-M13-06 A). |
| AC-M13-06 | No new public API break; M12 setters / traps / STRICT families unchanged (OQ-M13-04 A). |
| AC-M13-07 | No frame-gen; no OS window; no DLSS/Enabler/Swapper/OptiScaler; artwork composition locked (OQ-M13-07 A). |
| AC-M13-08 | Linux without SDK: `# requires_xess:` soft-skip; OFF twins + trap + roundtrip still green (OQ-M13-08 A). |
| AC-M13-09 | Default render→display remains **960×540 → 1920×1080** for binding AC lanes (OQ-M13-03 A). |

**Repeatability (OQ-M13-02 A — binding):** Secondary SHOULD: both Pass1 and Pass2 ratios ≥ **1.20×**; report absolute ms; no hard ms cap until first comparable M13 tip.

---

## 6. Size / DCE (OQ-M13-06 A — binding)

| Binary class | Cap (OQ-M13-06 A binding) |
|--------------|---------------------------|
| Hello | ≤ **20 KiB** |
| CPU glass+mirror | ≤ **300 KiB** |
| Compute-linked | ≤ **400 KiB** |
| RT-linked | ≤ **500 KiB** |
| Denoise-linked | ≤ **600 KiB** |
| Upscale-glue-linked | ≤ **700 KiB** stripped Farmos PE (vendor DLLs external) |

**Binding (OQ-M13-06 A):** No new PE ceiling. Options B (≤800) / C (≤650) **rejected**.

---

## 7. Fixtures plan (`spec/tests/M13/`)

Binding outline (see `spec/tests/M13/README.md`):

| Kind | Plan |
|------|------|
| Regression | Cite / reuse M12 OFF twins, trap 122, mode roundtrip — do not rewrite M12 bodies |
| Arc bench | Timing + quality notes under `spec/tests/M13/bench/` (or reuse M12 harness with M13 tip SHA) |
| Soft-skip | `# requires_xess:` / `# requires_vulkan_rt:` as M12 |
| New `.fm` stubs | Optional / cheap; MAY add after FINAL if harness needs them |

Harness CWD for any M13-local programs: `spec/tests/M13`. poteto **HOLD** until Projects Manager assigns after this FINAL.

---

## 8. Soft backlog notes (non-blocking)

Folded from eggtooth AUDIT PASS r2 / clear-m12-backlog — **informational**; MUST NOT block M13 FINAL:

1. Stale tip text may still cite L~**187** / **3.932** beside pinned ROI — CLAIM/harness MUST use pinned box + correct \|ΔL\| vs mean_abs_dL labels (already clarified in M12 FINAL 2026-10-25).
2. LF drift on Windows vs Linux — harness concern; not an M13 scope gate.
3. Pass1 headroom / harness soft-skip improvements — poteto track separately; not OQ blockers.

---

## 9. Blockers (informational)

| Risk | Mitigation |
|------|------------|
| ≥1.20× not repeatable on Arc (eggtooth historically saw 1.237 then 1.131 under old M12) | OQ-01 A uses tip `d23acfb3` **1.264 / 1.348** as baseline (~5% headroom on Pass1); OQ-02 A requires both passes ≥1.20× |
| Absolute ms unknown for `d23acfb3` | Ratios + tip SHAs only; no hard ms cap until measured |
| Quality regression from XeSS tuning | AC-M13-02 binds M12 AC-04 |
| Scope creep (frame-gen / window / lower spp) | OQ-03/07 keep OUT under A |

---

## 10. Open questions (OQ-M13-01 .. OQ-M13-08) — **ACCEPTED A**; do not reopen

Status: **FINAL**. Open questions: **none**. OQ-M13-01 .. OQ-M13-08 all **accepted A** (PM lock 2026-10-26; user locked all as A). Rejected options remain below for audit trail only.

### OQ-M13-01 — Primary speed gate — **ACCEPTED A**

**Context:** Baseline Arc CLAIM product `d23acfb3`: Pass1 **1.264** / Pass2 **1.348** under M12 OQ-12 A (≥1.05×). Historical M12 OQ-10 A ≥1.2× was superseded for M12 SHOULD floor only; reclaim as **M13 SHOULD** under A. M12 ≥1.05× remains floor.

| Option | Proposal | Lock |
|--------|----------|------|
| **A (ACCEPTED)** | SHOULD display-equivalent ≥ **1.20×** (native_median / xess_median) on TommyLaptop Arc after warm-up on M12 bench (960×540→1080p, glossy ON). Binding: XeSS wall **strictly <** native (MUST). Document tip `d23acfb3` Pass1 **1.264** / Pass2 **1.348**. M12 OQ-12 A ≥1.05× remains floor. Historical OQ-10 A ≥1.2× **reclaimed as M13 SHOULD**. | **LOCKED** |
| B | SHOULD ≥ **1.25×** (Pass1 1.264 ≈1% headroom — tight for audit repeatability) | Rejected |
| C | Absolute XeSS wall-clock cap only (ratio report-only) — needs PM number | Rejected |
| D | Keep M12 ≥1.05× only; M13 gains report-only | Rejected |

**Rationale for A:** Tip already clears **1.20×** on both passes with ~5% Pass1 headroom; reclaims the historical product intent without pretending **1.25×** is audit-safe yet.

### OQ-M13-02 — Absolute wall / repeatability secondary — **ACCEPTED A**

| Option | Proposal | Lock |
|--------|----------|------|
| **A (ACCEPTED)** | Secondary SHOULD: **both** Pass1 and Pass2 ratios ≥ **1.20×** (repeatability); report absolute ms; **no** hard ms cap until measured on a comparable harness. | **LOCKED** |
| B | Hard ms cap (PM supplies number) | Rejected |
| C | No secondary absolute / repeatability gate | Rejected |

**Rationale for A:** Addresses eggtooth repeatability failures without inventing unknown ms; forces both warm passes under the same SHOULD floor.

### OQ-M13-03 — Lever scope — **ACCEPTED A**

| Option | Proposal | Lock |
|--------|----------|------|
| **A (ACCEPTED)** | Host amortization + path/RT engine + aux/PSO/cache; **keep** M12 default 960×540→1080p for binding AC; quality-mode XeSS tuning OK if AC-04 holds | **LOCKED** |
| B | Also allow opt-in lower render-res (e.g. 720p→1080p) as separate mode / AC lane | Rejected |
| C | Engine-only (no host/bench harness changes) | Rejected |
| D | Host/bench-only (no engine) | Rejected |

**Rationale for A:** Matches historical RCA (process-per-frame + engine + aux/PSO); preserves M12 AC continuity at default scale.

### OQ-M13-04 — API surface — **ACCEPTED A**

| Option | Proposal | Lock |
|--------|----------|------|
| **A (ACCEPTED)** | No new `.fm` public API; MAY env / internal perf hooks; M12 APIs unchanged | **LOCKED** |
| B | Additive `setPathPerf` / similar | Rejected |
| C | Reopen interactive progressive wall ACs from M10 as M13 focus | Rejected |

**Rationale for A:** Speed wave should not force API churn; env hooks suffice for benches.

### OQ-M13-05 — M12 regression — **ACCEPTED A**

| Option | Proposal | Lock |
|--------|----------|------|
| **A (ACCEPTED)** | MUST NOT regress M12 binding: XeSS < native; ROI L both lanes **[80, 200]**; \|ΔL\| ≤ 5.0 on pinned ROI; OFF CPU twins; trap 122; size ≤700 KiB glue PE; frame-gen OUT; no window | **LOCKED** |
| B | Allow SHOULD floor supersession only (already covered by OQ-01 A reclaiming 1.20×) | Rejected (redundant with 01 A) |
| C | Allow quality band widen | Rejected |

**Rationale for A:** M13 is a speed wave, not a quality relax; quality widen would undo OQ-13 A.

### OQ-M13-06 — Size — **ACCEPTED A**

| Option | Proposal | Lock |
|--------|----------|------|
| **A (ACCEPTED)** | No new PE ceiling; keep M12 ≤700 KiB upscale-glue; hello ≤20; prior caps | **LOCKED** |
| B | Allow ≤800 KiB if justified | Rejected |
| C | Tighten ≤650 KiB | Rejected |

**Rationale for A:** Perf work should DCE; no evidence yet that glue must grow.

### OQ-M13-07 — Frame-gen / window / DLSS — **ACCEPTED A**

| Option | Proposal | Lock |
|--------|----------|------|
| **A (ACCEPTED)** | All remain **OUT** (M12-01/09; Enabler forever OUT) | **LOCKED** |
| B | Minimal window IN | Rejected |
| C | Frame-gen IN | Rejected |

**Rationale for A:** Out of product direction for this wave; keep headless buffer path.

### OQ-M13-08 — Platforms / soft-skip — **ACCEPTED A**

| Option | Proposal | Lock |
|--------|----------|------|
| **A (ACCEPTED)** | TommyLaptop Arc primary for speed ACs; Linux `# requires_xess` soft-skip; CPU OFF twins always green | **LOCKED** |
| B | Require Linux GPU | Rejected |
| C | Windows-only fixtures (no Linux soft-skip policy change) | Rejected |

**Rationale for A:** Matches M7–M12 CI reality; Arc remains the timing authority.

---

## 11. Amendments relative to prior milestones (binding under OQ-M13-01..08 A)

- M12 body **untouched**. M13 adds a new SHOULD ≥ **1.20×** speed gate (OQ-01 A) **above** the M12 ≥1.05× floor; does not reopen M12 OQ tables.
- M8/M9 engine non-goals are **not** reopened into OptiX / full RT pipeline / GPU raster.
- M10 no-window and M12 frame-gen OUT **stand** (OQ-07 A).
- Soft backlog clarifications already in M12 FINAL (ROI pin, \|ΔL\| vs mean_abs_dL) remain binding floors for AC-M13-02.

---

## 12. Document history

| Date | Entry |
|------|-------|
| 2026-10-26 | **DRAFT** created. User locked direction: perf / more speed after clear-m12-backlog. Master tip `affc63ea`; baseline CLAIM product `d23acfb3` Pass1 **1.264** / Pass2 **1.348**. Open OQ-M13-01..08; recommend **A** everywhere. Status **DRAFT** — not FINAL. poteto **HOLD**. No implementation code. M1–M12 bodies untouched. |
| 2026-10-26 | **FINAL**. PM/user locked OQ-M13-01..08 all **A**. Binding: SHOULD ≥ **1.20×** both Pass1+Pass2; host+engine+aux/PSO; keep 960×540→1080p; no new public API; MUST NOT regress M12; ≤700 KiB glue; frame-gen/window/DLSS OUT; Arc primary + Linux soft-skip. poteto **HOLD** until PM assigns. No implementation code. M1–M12 bodies untouched. |

---

## Appendix A — Gate summary (binding under OQ-M13-01..08 A)

- **Floor (M12, unchanged):** SHOULD ≥ **1.05×**; XeSS < native MUST; ROI **[80, 200]** + \|ΔL\| ≤ 5.0 on pinned `x=940:980,y=520:560`; OFF twins; trap 122; ≤700 KiB glue.
- **M13 SHOULD (OQ-01 A / OQ-02 A):** ≥ **1.20×** on **both** Pass1 and Pass2 after warm-up; document `d23acfb3` **1.264 / 1.348**; report ms; no hard ms cap yet.
- **Scope (OQ-03 A):** host + engine + aux/PSO; keep 960×540→1080p default.
- **API (OQ-04 A):** no new public API required.
- **OUT (OQ-07 A):** frame-gen, window, DLSS/Enabler/Swapper.
