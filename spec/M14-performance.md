# Farmos M14 — Performance / more speed (post-M13)

**Status:** **FINAL** (PM/user 2026-10-29: OQ-M14-01 .. OQ-M14-08 all **accepted A**)  
**Milestone:** M14 — next performance / more-speed wave after M13 host amort  
**Depends on:** `spec/M1-core.md` .. `spec/M13-performance.md` (all FINAL)  
**Normative keywords:** MUST / SHOULD / MAY per RFC 2119.  
**Date:** 2026-10-29 (DRAFT); **2026-10-29 FINAL** (PM/user lock OQ-M14-01..08 A; Asia/Shanghai)

This document specifies Farmos M14: a **next speed wave** on TommyLaptop Arc (XeSS path) that raises display-equivalent throughput **beyond M13 host amortization**, without regressing M12 or M13 binding meters. It does **not** specify compiler, runtime, or stdlib implementation code. It does **not** edit M1–M13 milestone markdown bodies; normative amendments relative to those milestones live in §11 of this file (binding under OQ-M14-01..08 A).

Open questions: **none**. OQ-M14-01 .. OQ-M14-08 all **accepted A** (PM lock 2026-10-29; user locked all as A). Binding under A: SHOULD ≥ **1.50×** both Pass1+Pass2 inproc `FARMOS_PATH_FRAME_MS`; path/RT + aux/PSO primary; keep 960×540→1080p; no new public API; MUST NOT regress M12 or weaken M13 ≥1.20×; ≤700 KiB glue; frame-gen/window/DLSS OUT; Arc primary + Linux soft-skip. poteto stays **HOLD** until Projects Manager assigns coding after this FINAL.

Related: `/workspace/farmos/PLAN.md` (M14 stub), `spec/M8-path-speed.md`, `spec/M9-hardware-rt.md`, `spec/M10-denoise-interactive.md`, `spec/M11-glossy-continuation.md`, `spec/M12-native-upscale.md` (**FINAL**), `spec/M13-performance.md` (**FINAL**), `spec/tests/M14/README.md`.

**Binding context (document; do not invent absolute ms beyond measured tips):**

| Item | Value |
|------|-------|
| Master tip (M13 squash-merge PR #22; eggtooth AUDIT PASS) | `47949c82` |
| M13 product (in-process meter) | `945c13ae` (`FARMOS_PATH_FRAME_MS`) — Pass1 **1.455** / Pass2 **1.607** (CLAIM / AUDIT; inproc after 3 warmup; median of remaining samples) |
| M13 docs tip (pre-squash PR HEAD / CLAIM sync lineage) | `e4978668` / docs tip `2607eccb` (historical; master tip is `47949c82`) |
| M13 SHOULD floor (MUST NOT weaken) | in-process `FARMOS_PATH_FRAME_MS` SHOULD ≥ **1.20×** **both** Pass1+Pass2 |
| M12 floor (MUST NOT regress) | process-per-frame SHOULD ≥ **1.05×**; XeSS wall **strictly <** native (MUST); ROI L **[80, 200]** + **\|ΔL\| of means ≤ 5.0**; ROI pinned **`x=940:980,y=520:560`** @1080p **40×40**; OFF; trap **122**; ≤**700 KiB** glue; frame-gen OUT |
| M13 what shipped (evidence) | Host amort: in-process multi-frame + `FARMOS_PATH_FRAME_MS` env hook; dedicated XeSS `VkDevice` already from M12 clear-backlog. PR #22 tradeoffs: **did not** merge fill_aux one-pass (~**0.06 ms** @960×540); **did not** tune XeSS quality (AC-04 already PASS); **did not** change render-res / spp / artwork / dedicated device |
| Soft backlog (non-blocking) | AC-M13-04 table text omits naming `FARMOS_PATH_FRAME_MS`; OQ-M13-01 old p-p-f rationale vs inproc meter — §8 notes only; MUST NOT block M14; MUST NOT reopen M13 OQ tables |
| Prior speed waves | M8 engine RR/MIS/Vulkan occupancy; M9 HW RT; M10 denoise; M11 glossy; M12 upscale; M13 host amort — M14 is the **next** engine-first wave |

**Product intent (PM / user GO draft-m14 more-perf):**

1. Deliver **more** display-equivalent speed on TommyLaptop Intel Arc (XeSS path) after warm-up, **beyond** M13 in-process host amort.
2. Prefer **path/RT engine + aux/PSO deepen** levers first; do **not** chase speed by lowering product spp or changing silver artwork.
3. **MUST NOT** regress M12 binding ACs or weaken M13 inproc SHOULD ≥ **1.20×** floor.
4. Soft backlog from eggtooth M13 AUDIT is **informational only**.
5. Keep the binding meter explicit: M14 primary SHOULD uses **in-process** `FARMOS_PATH_FRAME_MS` (same family as M13 CLAIM); M12 process-per-frame ≥1.05× remains a **separate** floor — do **not** cross-compare meters.

**Target platform:** Windows x64 primary (TommyLaptop Arc) for speed ACs; Linux CI soft-skip / skip when XeSS/Vulkan absent — CPU OFF-path twins still required.

---

## 1. Scope and non-goals

### 1.1 In scope (OQ-M14-03 A — binding)

M14 MUST deliver measurable speed gains via one or more of:

| Lever | Requirement (OQ-M14-03 A binding) |
|-------|-------------------------------------|
| Path / Vulkan RT engine | Further occupancy; AS rebuild vs refit; spp batching; fewer host round-trips — **additive** to M8/M9; **primary** remaining headroom after M13 host amort |
| Aux / MV / depth / jitter | `fill_aux` efficiency deepen **if** measurable beyond the rejected ~0.06 ms one-pass microgain |
| PSO / pipeline cache / warmup | Deepen existing PSO disk cache / shader warmup if measurable (Pass1 cold vs Pass2 warm already visible on tip) |
| XeSS/FSR quality-mode tuning | Sharpness / history / quality-mode OK **only if** M12 AC-04 still PASSes on pinned ROI |
| Default render→display | **Keep** M12/M13 default **960×540 → 1920×1080** for binding AC continuity (OQ-03 A; B rejected) |
| Host / device amort deepen | Keep dedicated XeSS `VkDevice`; further amort only if evidence supports (secondary; M13 already delivered inproc multi-frame) |
| CPU/host stills when upscale OFF | Secondary; report-only unless an AC later binds it |

### 1.2 Explicit non-goals (OUT unless an OQ locks IN)

| Item | Note |
|------|------|
| Frame generation (DLSS-G / FSR-FG / XeSS-FG) | **OUT** (OQ-M14-07 A; M12-09 / M13-07 stand) |
| OS window / swapchain | **OUT** (M10-01 / M12-01 / M13 stand) |
| DLSS / NGX / Enabler / Swapper / OptiScaler | **OUT forever** (M12 stands) |
| Changing silver artwork composition | **OUT** |
| Lowering product spp as the speed strategy | **OUT** (engine first; M8 spirit) |
| Opt-in lower render-res (e.g. 720p→1080p) | **OUT** (OQ-M14-03 A; OQ-03 B rejected) |
| Weakening M13 inproc ≥1.20× or M12 ≥1.05× / quality floors | **OUT** |
| Editing M1–M13 milestone markdown bodies | Amendments in §11 only |
| Full game engine / input loop | **OUT** |
| OptiX / GPU raster / accelerating M3 `render()` | **OUT** |
| Replacing host amort with process-per-frame as M14 primary meter | **OUT** (keep inproc `FARMOS_PATH_FRAME_MS`) |

### 1.3 Design constraints (OQ-M14-05 A — binding)

- M12 AC-M12-04 / AC-M12-05 remain a **floor**: XeSS wall **strictly <** native (MUST); ROI L both lanes in **[80, 200]**; **\|ΔL\| = \|L_xess − L_native\| ≤ 5.0** on pinned ROI **`x=940:980,y=520:560`** @1080p 40×40; mean_abs_dL / png_tol / max_abs **report-only**.
- M12 OQ-12 A SHOULD ≥ **1.05×** process-per-frame MUST NOT be weakened.
- M13 OQ-01/02 A SHOULD ≥ **1.20×** in-process `FARMOS_PATH_FRAME_MS` both Pass1+Pass2 MUST NOT be weakened; M14 raises a **new** SHOULD above the M13 tip (OQ-M14-01 A).
- OFF CPU twins, trap **122**, upscale-glue PE ≤ **700 KiB**, hello ≤ **20 KiB**, prior ≤300/400/500/600 caps remain.
- Pixel values under upscale OFF MUST NOT depend on thread scheduling on CPU (M4 determinism).
- Do **not** invent absolute millisecond caps until measured on a comparable harness; prefer ratios + tip SHAs (`945c13ae`, `47949c82`).
- Do **not** compare in-process ms ratios to historical process-per-frame tips (`d23acfb3` 1.264/1.348).

---

## 2. Relationship to M8–M13

| Milestone | What it already shipped | What M14 adds |
|-----------|-------------------------|---------------|
| M8 | RR/MIS opt-in; Vulkan occupancy / spp batching; ≥2× Vulkan / ≥1.5× CPU historical | Further engine occupancy; fewer host RTs; AS rebuild vs refit deepen |
| M9 | HW RT (`PATH_BACKEND_VULKAN_RT`); wall ≤5000 ms / ≥5× (OQ-M9-08 A) | Additive RT/path scheduling; no reopen of OptiX / full RT pipeline |
| M10 | Spatial denoise; progressive API; **no** window | Do **not** reopen interactive progressive wall ACs as M14 focus (OQ-M14-04 C rejected) |
| M11 | Opt-in glossy VNDF | Unchanged; M14 benches use glossy ON as M12/M13 CLAIM did |
| M12 | Native XeSS/FSR; OQ-12 A ≥1.05×; OQ-13 A ROI [80,200] + \|ΔL\|≤5; pinned ROI; dedicated XeSS device | Unchanged floors; quality-mode tuning only if AC-04 holds |
| M13 | In-process `FARMOS_PATH_FRAME_MS` host amort; SHOULD ≥1.20× both passes; tip `945c13ae` **1.455 / 1.607**; master `47949c82` | **Next** speed wave; raise SHOULD above tip (OQ-01 A ≥**1.50×**); engine + aux/PSO primary |

M14 does **not** reopen M12 or M13 OQ decision tables. Supersession of M13 SHOULD floor (if any) is **only** by raising a **new** M14 SHOULD above the M13 tip under locked OQ-M14-01 — M13 ≥1.20× and M12 ≥1.05× remain regression floors.

---

## 3. Candidate levers (survey → scope)

| # | Lever | IN (OQ-M14-03 A binding)? | Notes |
|---|-------|-------------------|-------|
| 1 | Path / Vulkan RT engine (occupancy, AS rebuild vs refit, spp batching, fewer host RTs) | **IN** (primary) | M13 scoped IN but product did not land engine gains; remaining headroom |
| 2 | Aux / MV / depth / jitter (`fill_aux` cost) | **IN** if measurable | One-pass ~0.06 ms rejected in M13; deepen only with evidence |
| 3 | PSO / pipeline cache / shader warmup | **IN** if measurable | Pass1 cold vs Pass2 warm gap (1.455→1.607) shows cache matter |
| 4 | XeSS/FSR quality mode / sharpness / history | **IN** if AC-04 holds | M13 deferred; quality MUST still pass M12 AC-04 |
| 5 | Host / dedicated XeSS VkDevice further amort | Secondary | Device already in; inproc multi-frame already in |
| 6 | Optional lower render-res (e.g. 720p→1080p) | **OUT** (OQ-03 B rejected) | Default keep 960×540→1080p |
| 7 | CPU/host stills when upscale OFF | Secondary | Report-only unless later AC binds |

---

## 4. API surface (OQ-M14-04 A — binding)

**Binding (OQ-M14-04 A):** No new `.fm` public API required. MAY add env / internal perf hooks. Keep `FARMOS_PATH_FRAME_MS` as the binding inproc timing hook. M12 `setPathUpscale` / `getPathUpscale` / `PATH_UPSCALE_*` and prior path APIs **unchanged**.

| Option | Summary | Lock |
|--------|---------|------|
| **A (ACCEPTED)** | No new public API; MAY env / internal hooks; keep `FARMOS_PATH_FRAME_MS` | **LOCKED** |
| B | Additive `setPathPerf` / similar | Rejected |
| C | Reopen interactive progressive wall ACs from M10 as M14 focus | Rejected |

---

## 5. Acceptance criteria (binding under OQ-M14-01..08 A)

All ACs below are **binding** under OQ-M14-01..08 **accepted A**:

| ID | Criterion |
|----|-----------|
| AC-M14-01 | M12 OFF CPU twin(s) still **PASS** (M4 SHA / existing OFF goldens). |
| AC-M14-02 | M12 AC-M12-04 quality **PASS** on pinned ROI under M14 tip: both lanes L in **[80, 200]**; **\|ΔL\| ≤ 5.0**; mean_abs_dL / png_tol / max_abs report-only. |
| AC-M14-03 | M12 XeSS wall **strictly <** native **MUST PASS** after warm-up (unchanged binding); both inproc and process-per-frame lanes when reported. |
| AC-M14-04 | Timing (script): primary SHOULD display-equivalent ≥ **1.50×** (native_median / xess_median) on TommyLaptop Arc after warm-up on M12/M13 bench scene (**960×540→1080p**, glossy ON) for **both** Pass1 and Pass2, measured with **in-process** `FARMOS_PATH_FRAME_MS` (same protocol family as M13 CLAIM: warmup then median of timed samples). Document baseline product `945c13ae` Pass1 **1.455** / Pass2 **1.607** (~**3%** Pass1 headroom below 1.50× — tip does **not** yet clear A; engine work required). M13 SHOULD ≥ **1.20×** inproc remains floor. M12 process-per-frame ≥ **1.05×** remains floor. Report absolute ms (no hard ms cap until measured). |
| AC-M14-05 | Size / DCE: hello ≤ **20 KiB**; upscale unused DCE; upscale-glue-linked ≤ **700 KiB** PE; prior ≤300/400/500/600 unchanged (OQ-M14-06 A). |
| AC-M14-06 | No new public API break; M12/M13 setters / traps / STRICT / `FARMOS_PATH_FRAME_MS` families unchanged (OQ-M14-04 A). |
| AC-M14-07 | No frame-gen; no OS window; no DLSS/Enabler/Swapper/OptiScaler; artwork composition locked (OQ-M14-07 A). |
| AC-M14-08 | Linux without SDK: `# requires_xess:` soft-skip; OFF twins + trap + roundtrip still green (OQ-M14-08 A). |
| AC-M14-09 | Default render→display remains **960×540 → 1920×1080** for binding AC lanes (OQ-M14-03 A). |
| AC-M14-10 | Meter hygiene: CLAIM/harness MUST name **in-process** `FARMOS_PATH_FRAME_MS` as the M14 primary SHOULD meter; MUST NOT cross-compare to process-per-frame tips (`d23acfb3`). |

**Repeatability (OQ-M14-02 A — binding):** Secondary SHOULD: both Pass1 and Pass2 ratios ≥ **1.50×** under the same inproc meter; report absolute ms; no hard ms cap until first comparable M14 tip.

---

## 6. Size / DCE (OQ-M14-06 A — binding)

| Binary class | Cap (OQ-M14-06 A binding) |
|--------------|-----------------------------|
| Hello | ≤ **20 KiB** |
| CPU glass+mirror | ≤ **300 KiB** |
| Compute-linked | ≤ **400 KiB** |
| RT-linked | ≤ **500 KiB** |
| Denoise-linked | ≤ **600 KiB** |
| Upscale-glue-linked | ≤ **700 KiB** stripped Farmos PE (vendor DLLs external) |

**Binding (OQ-M14-06 A):** No new PE ceiling. Options B (≤800) / C (≤650) **rejected**.

---

## 7. Fixtures plan (`spec/tests/M14/`)

Binding outline (see `spec/tests/M14/README.md`):

| Kind | Plan |
|------|------|
| Regression | Cite / reuse M12 OFF twins, trap 122, mode roundtrip; cite M13 inproc harness — do not rewrite M12/M13 bodies |
| Arc bench | Timing + quality notes under `spec/tests/M14/bench/` (or reuse M13 harness with M14 tip SHA) |
| Soft-skip | `# requires_xess:` / `# requires_vulkan_rt:` as M12/M13 |
| New `.fm` stubs | Optional / cheap; MAY add after FINAL if harness needs them |

Harness CWD for any M14-local programs: `spec/tests/M14`. poteto **HOLD** until Projects Manager assigns after FINAL.

---

## 8. Soft backlog notes (non-blocking)

Folded from eggtooth M13 AUDIT PASS / PR #22 — **informational**; MUST NOT block M14 FINAL; MUST NOT reopen M13 OQ tables:

1. AC-M13-04 table text does not explicitly name `FARMOS_PATH_FRAME_MS` as the binding meter (OQ-03 A + CLAIM/harness do). M14 AC-M14-04 / AC-M14-10 **name the meter explicitly**.
2. OQ-M13-01 lock rationale cites `d23acfb3` 1.264/1.348 (process-per-frame) beside ≥1.20× reclaim — M13 SHOULD meter is inproc. Claim correctly forbids cross-meter compare; M14 repeats that rule.
3. fill_aux one-pass ~0.06 ms @960×540 was not merged in M13 — do not expect that alone to clear ≥1.50×.
4. Stale tip / LF drift / dirty-browser CLAIM noise remain harness concerns, not M14 scope gates.

---

## 9. Blockers (informational)

| Risk | Mitigation |
|------|------------|
| ≥1.50× not reachable on Pass1 (tip **1.455** is ~3% short) | OQ-01 A intentionally requires engine gains; OQ-01 B (≥1.45×) rejected as tip-clearing soft floor; if Arc cannot clear 1.50 after sincere engine work, PM may reopen OQ-01 only — do not invent a relax without PM OQ reopen |
| Absolute ms unknown for next tip | Ratios + tip SHAs only; no hard ms cap until measured |
| Quality regression from XeSS tuning | AC-M14-02 binds M12 AC-04 |
| Scope creep (frame-gen / window / lower spp / lower res) | OQ-03/07 keep OUT under A |
| Meter confusion (inproc vs process-per-frame) | AC-M14-10; §1.3; soft backlog §8 |

---

## 10. Open questions (OQ-M14-01 .. OQ-M14-08) — **ACCEPTED A**; do not reopen

Status: **FINAL**. Open questions: **none**. OQ-M14-01 .. OQ-M14-08 all **accepted A** (PM lock 2026-10-29; user locked all as A). Rejected options remain below for audit trail only.

### OQ-M14-01 — Primary speed gate — **ACCEPTED A**

**Context:** M13 product `945c13ae` inproc Pass1 **1.455** / Pass2 **1.607** under M13 SHOULD ≥1.20×. M13 floor MUST NOT weaken. M14 SHOULD raises above tip if headroom exists via remaining engine levers.

| Option | Proposal | Lock |
|--------|----------|------|
| **A (ACCEPTED)** | SHOULD display-equivalent ≥ **1.50×** (native_median / xess_median) on TommyLaptop Arc after warm-up on M12/M13 bench (960×540→1080p, glossy ON), **in-process** `FARMOS_PATH_FRAME_MS`, **both** Pass1+Pass2. Binding: XeSS wall **strictly <** native (MUST). Document tip `945c13ae` Pass1 **1.455** / Pass2 **1.607** (~**3%** Pass1 short of 1.50×; ~**7%** Pass2 headroom over 1.50×). M13 ≥**1.20×** inproc remains floor. M12 ≥**1.05×** process-per-frame remains floor. | **LOCKED** |
| B | SHOULD ≥ **1.45×** (Pass1 1.455 ≈0.3% headroom — tip-clearing soft floor; weak "next wave") | Rejected |
| C | SHOULD ≥ **1.60×** (Pass1 1.455 fails hard; Pass2-only aspiration) | Rejected |
| D | Keep M13 ≥1.20× only; M14 gains report-only | Rejected |

**Rationale for A:** Grounded in measured tip; forces real path/RT/aux/PSO work beyond host amort; Pass2 already clears 1.50× so the gate is not fantasy; Pass1 ~3% gap is the intended product work. Prefer both-pass repeatability like M13 OQ-02.

### OQ-M14-02 — Absolute wall / repeatability secondary — **ACCEPTED A**

| Option | Proposal | Lock |
|--------|----------|------|
| **A (ACCEPTED)** | Secondary SHOULD: **both** Pass1 and Pass2 ratios ≥ **1.50×** (same inproc meter); report absolute ms; **no** hard ms cap until measured on a comparable harness. | **LOCKED** |
| B | Hard ms cap (PM supplies number) | Rejected |
| C | No secondary absolute / repeatability gate | Rejected |

**Rationale for A:** Mirrors M13 OQ-02; addresses historical Arc repeatability failures without inventing unknown ms.

### OQ-M14-03 — Lever scope — **ACCEPTED A**

| Option | Proposal | Lock |
|--------|----------|------|
| **A (ACCEPTED)** | Path/RT engine + aux/PSO/cache primary; XeSS quality-mode OK if AC-04 holds; keep 960×540→1080p; host/device deepen secondary | **LOCKED** |
| B | Also allow opt-in lower render-res (e.g. 720p→1080p) as separate mode / AC lane | Rejected |
| C | Engine-only (forbid any harness / meter clarifications) | Rejected |
| D | Host/bench-only (no engine) — already largely delivered in M13 | Rejected |

**Rationale for A:** Matches remaining headroom survey (PR #22 did not land engine/XeSS-quality/fill_aux); preserves AC continuity at default scale.

### OQ-M14-04 — API surface — **ACCEPTED A**

| Option | Proposal | Lock |
|--------|----------|------|
| **A (ACCEPTED)** | No new `.fm` public API; MAY env / internal hooks; keep `FARMOS_PATH_FRAME_MS`; M12/M13 APIs unchanged | **LOCKED** |
| B | Additive `setPathPerf` / similar | Rejected |
| C | Reopen interactive progressive wall ACs from M10 as M14 focus | Rejected |

**Rationale for A:** Survey finds no clear public-API need; env hook already binds the meter.

### OQ-M14-05 — M12/M13 regression — **ACCEPTED A**

| Option | Proposal | Lock |
|--------|----------|------|
| **A (ACCEPTED)** | MUST NOT regress M12 binding (XeSS < native; ROI [80,200] + \|ΔL\|≤5 pinned; OFF; trap 122; ≤700 KiB; frame-gen OUT; no window) **and** MUST NOT weaken M13 inproc ≥1.20× floor | **LOCKED** |
| B | Allow quality band widen | Rejected |
| C | Allow M13 SHOULD weaken if M14 raises another meter | Rejected |

**Rationale for A:** M14 is a speed wave, not a quality or meter-relax; floors stack.

### OQ-M14-06 — Size — **ACCEPTED A**

| Option | Proposal | Lock |
|--------|----------|------|
| **A (ACCEPTED)** | No new PE ceiling; keep M12/M13 ≤700 KiB upscale-glue; hello ≤20; prior caps | **LOCKED** |
| B | Allow ≤800 KiB if justified | Rejected |
| C | Tighten ≤650 KiB | Rejected |

**Rationale for A:** Perf work should DCE; no evidence yet that glue must grow.

### OQ-M14-07 — Frame-gen / window / DLSS — **ACCEPTED A**

| Option | Proposal | Lock |
|--------|----------|------|
| **A (ACCEPTED)** | All remain **OUT** (M12-01/09; M13-07; Enabler forever OUT) | **LOCKED** |
| B | Minimal window IN | Rejected |
| C | Frame-gen IN | Rejected |

**Rationale for A:** Out of product direction for this wave; keep headless buffer path.

### OQ-M14-08 — Platforms / soft-skip — **ACCEPTED A**

| Option | Proposal | Lock |
|--------|----------|------|
| **A (ACCEPTED)** | TommyLaptop Arc primary for speed ACs; Linux `# requires_xess` soft-skip; CPU OFF twins always green | **LOCKED** |
| B | Require Linux GPU | Rejected |
| C | Windows-only fixtures (no Linux soft-skip policy) | Rejected |

**Rationale for A:** Matches M7–M13 CI reality; Arc remains the timing authority.

---

## 11. Amendments relative to prior milestones (binding under OQ-M14-01..08 A)

- M12 / M13 bodies **untouched**. M14 adds a new SHOULD ≥ **1.50×** inproc speed gate (OQ-01 A) **above** the M13 tip and **above** the M13 ≥1.20× floor; does not reopen M12/M13 OQ tables.
- Soft backlog clarifications (name `FARMOS_PATH_FRAME_MS`; no cross-meter compare) live in M14 ACs / §8 — not as M13 body edits.
- M8/M9 engine non-goals are **not** reopened into OptiX / full RT pipeline / GPU raster.
- M10 no-window and M12/M13 frame-gen OUT **stand** (OQ-07 A).

---

## 12. Document history

| Date | Entry |
|------|-------|
| 2026-10-29 | **DRAFT** created. User locked direction: more-perf after M13. Master tip `47949c82` (PR #22 squash; eggtooth AUDIT PASS); product `945c13ae` Pass1 **1.455** / Pass2 **1.607** inproc. Open OQ-M14-01..08; recommend **A** everywhere (SHOULD ≥**1.50×** both passes). Status **DRAFT** — not FINAL. poteto **HOLD**. No implementation code. M1–M13 bodies untouched. Soft backlog folded as notes only. |
| 2026-10-29 | **FINAL**. PM/user locked OQ-M14-01..08 all **A**. Binding: SHOULD ≥ **1.50×** both Pass1+Pass2 inproc `FARMOS_PATH_FRAME_MS`; path/RT + aux/PSO; keep 960×540→1080p; no new public API; MUST NOT regress M12 or weaken M13 ≥1.20×; ≤700 KiB; frame-gen/window/DLSS OUT; Arc primary + Linux soft-skip. poteto **HOLD** until PM assigns. No implementation code. M1–M13 bodies untouched. |

---

## Appendix A — Gate summary (binding under OQ-M14-01..08 A)

- **Floor (M12, unchanged):** process-per-frame SHOULD ≥ **1.05×**; XeSS < native MUST; ROI **[80, 200]** + \|ΔL\| ≤ 5.0 on pinned `x=940:980,y=520:560`; OFF twins; trap 122; ≤700 KiB glue; frame-gen OUT.
- **Floor (M13, unchanged):** in-process `FARMOS_PATH_FRAME_MS` SHOULD ≥ **1.20×** both Pass1+Pass2.
- **M14 SHOULD (OQ-01 A / OQ-02 A):** ≥ **1.50×** on **both** Pass1 and Pass2 after warm-up under inproc `FARMOS_PATH_FRAME_MS`; document `945c13ae` **1.455 / 1.607**; report ms; no hard ms cap yet.
- **Scope (OQ-03 A):** path/RT engine + aux/PSO primary; XeSS quality if AC-04 holds; keep 960×540→1080p; host/device secondary.
- **API (OQ-04 A):** no new public API required; keep `FARMOS_PATH_FRAME_MS`.
- **OUT (OQ-07 A):** frame-gen, window, DLSS/Enabler/Swapper.
