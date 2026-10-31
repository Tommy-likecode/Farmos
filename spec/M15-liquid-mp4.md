# Farmos M15 — Liquid / fluid simulation + MP4 export (~5 s)

**Status:** **FINAL** (PM/user 2026-10-31: OQ-M15-01 .. OQ-M15-10 all **accepted A**)  
**Milestone:** M15 — liquid / fluid simulation + MP4 export (~5 seconds)  
**Depends on:** `spec/M1-core.md` .. `spec/M14-performance.md` (all FINAL); M3 scene + M5 physics (FINAL) as integration context  
**Normative keywords:** MUST / SHOULD / MAY per RFC 2119.  
**Date:** 2026-10-31 (DRAFT); **2026-10-31 FINAL** (PM/user lock-all-oq-a; Asia/Shanghai)

This document specifies Farmos M15: a **new feature** milestone that adds a **CPU-first liquid / fluid simulation** suitable for a shippable ~**5 second** demo, plus **MP4 export**. It does **not** specify compiler, runtime, or stdlib implementation code. It does **not** edit M1–M14 milestone markdown bodies (PLAN.md gets an M15 stub only). Normative amendments relative to prior milestones live in §11 of this file (binding under OQ-M15-01..10 A).

Open questions: **none**. OQ-M15-01 .. OQ-M15-10 all **accepted A** (PM lock 2026-10-31; user lock-all-oq-a). Binding under A: 2D height-field / shallow-water CPU (`farmos:liquid`); XZ +Y; external ffmpeg; 5.0 s / 30 fps / 640×360; `Renderer.saveMP4`; `S_liq − S_scene ≤ 96 KiB`; hello ≤20 + DCE; trap **123**-family; OUT cloth / 3D SPH / coupling / bundled H.264 / window / FG / DLSS / M12–M14 weaken / M5 edit. poteto stays **HOLD** until Projects Manager assigns coding after this FINAL.

Related: `/workspace/farmos/PLAN.md` (M15 stub), `spec/M5-physics.md` (**FINAL** — RD-M5-04 fluids **OUT** of M5), `spec/M3-scene.md`, `spec/M4-ray.md`, `spec/M12-native-upscale.md` .. `spec/M14-performance.md` (perf floors **orthogonal** — MUST NOT weaken), `spec/tests/M15/README.md`.

**Binding context (document; verify on disk / PM citation):**

| Item | Value |
|------|-------|
| Master tip (M14 + clear-m14-backlog; eggtooth AUDIT PASS) | `8b19896b` (PR #24 squash; PM / proof doc) |
| Product GPU / runtime tip (informational; M14 product) | `4d7523ec` (unchanged by clear-m14-backlog) |
| User ask (2026-10-31 Asia/Shanghai) | 能不能再做一个液体模拟，输出mp4，5秒 — liquid simulation, output mp4, ~5 seconds |
| M5 fluids posture | RD-M5-04: soft bodies, cloth, **fluids** **OUT** of M5 — M15 is a **deliberate new milestone**, not an M5 body edit |
| Existing media | Demos / Renderer: **PNG only** (`savePNG`); **no** mp4 / ffmpeg pipeline in PLAN or specs surveyed |
| Size floors (MUST keep) | hello ≤ **20 KiB**; unused-feature DCE; prior ≤300/400/500/600; upscale glue ≤**700 KiB** |
| Perf floors (orthogonal; MUST NOT weaken) | M12 process-per-frame ≥1.05×; M13 inproc ≥1.20×; M14 inproc ≥1.50×; XeSS quality / OFF / trap 122 / frame-gen OUT stand |
| Trap band already used | 105–108, 110–116, **117–122**; exit **109** still reserved unused (M3.5). M15 uses **123**-family (OQ lock A) |

**Product intent (PM / user GO draft-m15 liquid-mp4):**

1. Ship a **visible liquid** demo that runs ~**5 seconds** of simulated / displayed time and writes an **MP4**.
2. Prefer **tiny-binary spirit**: CPU-first sim; Arc / Vulkan **optional**, not required for the liquid AC itself.
3. Treat fluids as **new scope** (cite M5 RD-M5-04 OUT) via a new module — do **not** reopen or edit `spec/M5-physics.md`.
4. Video export is **new** (bundling / deps / size budgets matter). Prefer **external encoder** over growing the PE with a full H.264 stack.
5. This milestone is **orthogonal** to M12–M14 more-perf: MUST NOT weaken those floors as part of delivering liquid/mp4.

**Target platform:** Windows x64 primary (TommyLaptop) for size + mp4 ACs; Linux CI soft-skip when ffmpeg / optional GPU absent — CPU liquid unit fixtures still required.

---

## 1. Scope and non-goals

### 1.1 In scope (OQ-M15-01..10 A — binding)

| Area | Requirement (binding under A) |
|------|---------------------------|
| Liquid / fluid sim | **2D height-field / shallow-water** on a regular grid (CPU); fixed-step; deterministic fixtures |
| Language module | New built-in module **`farmos:liquid`** (separate link unit; default unused → DCE) |
| Scene hook | Sync height / displacement into an M3 `Mesh` (or height texture) after `step`; does **not** call `render` / `renderPath` |
| Demo media | Loop: step → sync → `render` (M3 raster preferred for cheap frames) → accumulate frames → **MP4 ~5 s** |
| MP4 export | External **`ffmpeg`** on PATH muxes RGB/PNG frame sequence → `.mp4`; **`Renderer.saveMP4`** (binding) |
| Fixtures | Import / step / trap / size / DCE; video ACs by **frame count / duration / exit**, not pixel-perfect MP4 SHA |
| Size | hello ≤20; liquid unused DCE; new liquid-linked delta budget (OQ-08) |

### 1.2 Explicit non-goals (OUT unless an OQ locks IN)

| Item | Note |
|------|------|
| Soft bodies / cloth | **OUT** (user asked liquid only; M5 spirit) |
| Editing `spec/M5-physics.md` to add fluids | **OUT** — cite RD-M5-04; reopen via **this** milestone only |
| Full 3D SPH / FLIP / grid Navier–Stokes | **OUT** (OQ-01 C rejected) |
| Two-way fluid ↔ rigid coupling | **OUT** of M15 (MAY coexist with `farmos:physics` without coupling) |
| Bundling libx264 / full ffmpeg into the PE | **OUT** (OQ-06 B/D rejected) |
| OS window / swapchain / interactive player | **OUT** (M10/M12 stand) |
| Frame-gen / DLSS / Enabler / Swapper / OptiScaler | **OUT forever** (M12 stands) |
| Weakening M12–M14 perf / quality floors | **OUT** (orthogonal feature) |
| Path-tracing every demo frame as a hard AC | **OUT** (too slow for 150 frames); MAY allow optional path stills |
| Pixel-perfect / SHA-256 golden of the `.mp4` bitstream | **OUT** (encoder nondeterminism across ffmpeg builds) |
| Implementation code in this repo milestone by farmspec | **OUT** (spec only; poteto HOLD until PM assigns) |

### 1.3 Design constraints

- PLAN / M1: hello that never imports liquid / never calls MP4 encode stays ≤ **20 KiB** stripped Windows x64.
- M5 RD-M5-04 remains true **for M5**: M5 does not gain fluids; M15 adds a **sibling** feature.
- M3 `savePNG` and canonical PNG encoding remain; MP4 is additive.
- Traps **117–122** MUST NOT be reused for liquid/mp4 meanings. Binding exit **123**-family (OQ lock A). Exit **109** remains unused.
- Determinism for **sim state** fixtures: same steps ⇒ bit-identical printed heights / particle counts (IEEE 754 binary64, no `-ffast-math`) under the published oracle.
- Video bitstream MAY differ across ffmpeg versions; harness MUST NOT require bit-identical MP4.

---

## 2. Relationship to prior milestones

| Milestone | What it already shipped | What M15 adds |
|-----------|-------------------------|---------------|
| M3 | Scene / Mesh / Renderer `render` + `savePNG` | Frame loop + optional `saveMP4` (or helper) |
| M4 | `renderPath` + path PNG | Unchanged; liquid demo SHOULD prefer raster for speed |
| M5 | `farmos:physics` rigid bodies; **fluids OUT** (RD-M5-04) | **New** `farmos:liquid`; does **not** edit M5 body |
| M7–M11 | GPU path / denoise / glossy; traps 117–121 | Unchanged; liquid CPU-first |
| M12–M14 | Upscale + perf waves; trap 122; floors | Unchanged floors; M15 MUST NOT weaken |

**Contradiction note (intentional):** PLAN M5 / `spec/M5-physics.md` say fluids are OUT. M15 **deliberately reopens fluids** as a **new** milestone. Do **not** “fix” M5 by editing it.

---

## 3. Candidate sim models (survey → OQ-M15-01 / 02)

| # | Model | Visual “liquid” | Code / size | Determinism | Lock |
|---|-------|-----------------|-------------|-------------|------|
| 1 | **2D height-field / shallow water** | Good (pond / ripples / dam-break-ish) | Small | Easy | **A (ACCEPTED)** |
| 2 | 2D SPH-lite particles | Good (spray / blob) | Medium | Medium | Rejected |
| 3 | 3D SPH / grid NS | Best | Large; Arc compute tempting | Harder | Rejected |
| 4 | Particle fountain / spray only | Weak “liquid” | Tiny | Easy | Rejected |

**Survey note:** Existing Vulkan path is for **path tracing**, not a general fluid compute stack. No mp4/ffmpeg pipeline exists. CPU-first height-field best matches tiny-binary + shippable 5 s demo.

**Coordinate convention (OQ-M15-02 A — binding):** height field on **XZ** grid; height along **+Y** (M3/M5 gravity −Y). Units: meters. Gravity magnitude default **9.81** (direction −Y when syncing to Mesh).

### 3.1 Shallow-water scheme (binding under OQ-M15-01 A — FINAL refine)

Normative sim model for M15:

1. **State per cell:** water column height `h ≥ 0` and depth-averaged horizontal velocity components `(u, w)` on the **XZ** staggered or collocated grid (implementation MAY choose collocated with artificial viscosity; MUST document the choice in CLAIM and match `spec/tests/M15/_ref/liquid_ref.py`).
2. **Gravity / wave speed:** shallow-water gravity term uses `g = 9.81`; bed is flat at `y = 0` unless a future OQ adds terrain (OUT of M15).
3. **Time integration:** fixed-step; default `LiquidWorld` dt = **1/60**. Display loop at **30 fps** MUST advance sim by **two** `step()` calls per display frame (2 × 1/60 = 1/30 s of sim time per frame) — see §4.5. Programs MAY call `step` at other rates; demo / AC-M15-02 MUST use the two-steps-per-frame rule.
4. **Boundaries:** reflective / solid walls on the domain border (zero normal flux). Exact stencil MUST match the oracle.
5. **Determinism:** IEEE 754 binary64; no `-ffast-math`; same sequence of `setHeight` / `step` ⇒ bit-identical `getHeight` for fixtures.
6. **Oracle:** poteto MUST ship `spec/tests/M15/_ref/liquid_ref.py` with the CLAIM; eggtooth audits printed heights against it.

---

## 4. API surface (OQ-M15-03 / 04 / 05 A — binding)

### 4.1 Module (OQ-M15-03 A)

Built-in module path: **`farmos:liquid`**.

MUST NOT re-export `farmos:scene`, `farmos:math`, or `farmos:physics`. User imports what they need.

### 4.2 Exports (OQ-M15-04 A — binding)

```
LiquidWorld,
HeightField,
LIQUID_BACKEND_CPU,   // int 0 — only required backend in M15 A
```

Classes use **reference semantics** (same as M3/M5).

### 4.3 `LiquidWorld` / `HeightField` (sketch)

```
class LiquidWorld {
  // no public transform fields
}

// Construction defaults (binding):
//   fixed dt = 1/60
//   gravity  = 9.81 (scalar used by shallow-water; direction −Y when syncing)
//   disposed = false

setFixedTimeStep(dt: float): void
addHeightField(field: HeightField): void
removeHeightField(field: HeightField): void
step(): void
dispose(): void

class HeightField {
  // resolution nx, nz ≥ 2; cell size hx, hz > 0
  // heights[i + j*nx] = water column height h (meters); layout MUST match liquid_ref.py
}

constructor(nx: int, nz: int, hx: float, hz: float);
setHeight(i: int, j: int, h: float): void
getHeight(i: int, j: int): float
linkMesh(mesh: Mesh): void          // optional; sync after step
clearMeshLink(): void
```

**Sync (binding):** after each successful `step`, if a `Mesh` is linked, write vertex **Y** (or equivalent displacement) from cell heights so the mesh surface tracks the water. Mesh **scale is not overwritten** (M5 spirit). Exact vertex ↔ cell mapping MUST match `liquid_ref.py` / CLAIM; OOB or dispose still trap as §5.

**Physics coexistence (OQ-05 A):** programs MAY import both `farmos:physics` and `farmos:liquid`. M15 does **not** require two-way coupling (fluid does not push RigidBody; RigidBody does not displace height unless user copies by hand).

### 4.4 Renderer / MP4 API (OQ-M15-06 A — binding)

**Binding:** additive method on existing M3 `Renderer` (mirrors `savePNG`):

```
// On Renderer:
saveMP4(path: string): void
```

Optional `beginVideo` / `pushFrame` / `endVideo` MAY exist as implementation detail; the **normative** user-facing API for ACs is **`Renderer.saveMP4(path)`** after a loop that has rendered frames into an implementation-defined capture buffer / temp sequence.

**Binding behavior:**

1. Demo / AC loop: for each of **150** display frames: call `LiquidWorld.step()` **twice** (dt=1/60 each) → mesh sync → `renderer.render(scene, camera)` → capture frame from display buffer.
2. Frames are written as a temporary PNG sequence (or raw RGB) under an implementation-defined temp dir.
3. `saveMP4(path)` invokes **`ffmpeg`** from PATH to mux frames → `path` as MP4. Suggested flags (implementation MUST document actual flags in CLAIM): H.264, `yuv420p`, fps **30**, size **640×360**. Exact CLI string is implementation-defined but MUST produce ffprobe duration ≈ **5.0 s**, **30** fps, **150** frames, **640×360**.
4. If ffmpeg is missing / fails:
   - Soft harness: `# requires_ffmpeg: true` → soft-skip.
   - Product STRICT: env **`FARMOS_VIDEO_STRICT=1`** → trap **123** with stderr exactly `runtime error: MP4 encode failed` (distinct from invalid-liquid stderr).
5. `savePNG` unchanged. `saveMP4` inside a user `parallel` task remains **E0805**-class FILE_IO (same spirit as `savePNG`).

**CLI (MAY):** `farmc` does not need a separate video subcommand in M15 if `.fm` programs call `Renderer.saveMP4`.

### 4.5 Demo defaults (OQ-M15-07 A — binding)

| Parameter | Binding |
|-----------|---------|
| Duration | **5.0** seconds of displayed / encoded time |
| Frame rate | **30** fps ⇒ **150** frames |
| Resolution | **640×360** (demo / AC default; `setSize`) |
| Sim dt | **1/60** fixed default on `LiquidWorld` |
| Steps per display frame | **Two** `step()` calls per display frame (2 × 1/60 = 1/30 s sim time per frame) — **binding** for AC-M15-02 demo |
| Render | M3 **`render`** (raster), not `renderPath`, for the 5 s loop |
| Encode API | **`Renderer.saveMP4(path)`** after the loop |

---

## 5. Runtime traps (binding under OQ-M15-06 A / liquid params)

| Exit | stderr (binding) | When |
|------|----------------------|------|
| 105 | `runtime error: use after dispose` | `step` / mutating API after `dispose` (reuse) |
| **123** | `runtime error: invalid liquid parameter` | `setFixedTimeStep` with `dt <= 0`; `HeightField` ctor with `nx/nz < 2` or `hx/hz <= 0`; OOB `setHeight`/`getHeight` indices; mutating liquid API after failed construction — **MUST NOT** collide with M5 114–116 meanings on physics objects |
| **123** | `runtime error: MP4 encode failed` | `FARMOS_VIDEO_STRICT=1` and ffmpeg missing/fails — **same exit, distinct stderr REQUIRED** (M10/M12 pattern) |

Do **not** reuse 117–122. Exit **109** still unused.

---

## 6. Linking and size (OQ-M15-08 A — binding)

### 6.1 Link units

`farmos:liquid` is a **separate** link unit. A program that does not reference any liquid export MUST NOT link liquid code (DCE). MP4 encode glue MUST NOT link into hello. ffmpeg remains an **external** process (not statically linked) (OQ-M15-06 A).

### 6.2 Budgets (binding)

| Gate | Budget |
|------|--------|
| Hello (no liquid, no video) | ≤ **20 KiB** stripped Windows x64 PE |
| Prior caps | ≤300 / 400 / 500 / 600; upscale glue ≤**700 KiB** — **unchanged** |
| Liquid-linked delta | Let `S_liq` = stripped size of Appendix A liquid+raster demo (no ffmpeg link), `S_scene` = scene-hello without liquid. Require **`S_liq − S_scene ≤ 96 KiB`** |
| Video glue | Prefer **0** PE growth beyond tiny spawn/glue; if glue unavoidable when `saveMP4` is referenced, additional stripped PE ≤ **32 KiB** (binding soft ceiling; report actual in CLAIM) |

CI script class names: `liquid_demo` (S_liq gate) and `video_encode` (optional glue report) — poteto MAY rename but MUST preserve the numeric gates.

---

## 7. Acceptance criteria (binding under OQ-M15-01..10 A)

| ID | Criterion |
|----|-----------|
| AC-M15-01 | Fixtures under `spec/tests/M15/` pass for kinds listed in tests README (import, step, traps, approx heights). |
| AC-M15-02 | Demo program produces an MP4 of **duration ≈ 5.0 s** at **30 fps** (**150** frames) and **640×360** (ffprobe or equivalent); exit 0 when ffmpeg present. |
| AC-M15-03 | Liquid unused: hello ≤ **20 KiB**; DCE when liquid / saveMP4 unreferenced. |
| AC-M15-04 | `S_liq − S_scene ≤ 96 KiB` (§6.2). |
| AC-M15-05 | Determinism: same liquid program ⇒ bit-identical printed heights / counts for `run` / `run_approx` fixtures (oracle). |
| AC-M15-06 | Sync: linked Mesh reflects height field after `step` (fixture). |
| AC-M15-07 | Traps: 105 dispose; 123 invalid param; 123-family MP4 STRICT fail with distinct stderr. |
| AC-M15-08 | `# requires_ffmpeg: true` soft-skips MP4 AC when ffmpeg absent; CPU liquid fixtures still green. |
| AC-M15-09 | MUST NOT regress M12–M14 binding floors / OUT list (orthogonal; cite; no need to re-run Arc speed ACs unless product tip moves). |
| AC-M15-10 | M5 body / RD-M5-04 text **unchanged**; fluids remain OUT **of M5**. |

Video quality: **no** mandatory `# sha256:` on the `.mp4`. SHOULD extract first/last frame to PNG and MAY soft-compare with `# png_tol` or smoke “not all black” (report-only unless a fixture binds it).

---

## 8. Soft backlog / blockers (non-blocking)

| Item | Note |
|------|------|
| Exact stencil / collocated vs staggered | Documented in CLAIM + `liquid_ref.py`; §3.1 binds walls + g + two-steps-per-frame |
| PLAN.md M14 tip still cites older squash in places | Informational; M15 binding tip `8b19896b` / product `4d7523ec` |
| ffmpeg version skew | Soft-skip + duration/fps/resolution checks only |

---

## 9. Platforms / CI (OQ-M15-09 A — binding)

| Header | When to soft-skip |
|--------|-------------------|
| `# requires_ffmpeg: true` | `ffmpeg` not on PATH / encode probe fails |
| `# requires_vulkan:` / `# requires_xess:` | Unrelated; liquid ACs MUST NOT require these |

Windows x64 primary for size + mp4 duration AC. Linux CI: keep liquid CPU fixtures green; soft-skip ffmpeg-dependent cases.

---

## 10. Open questions (OQ-M15-01 .. OQ-M15-10) — **ACCEPTED A**; do not reopen

Status: **FINAL**. Open questions: **none**. OQ-M15-01 .. OQ-M15-10 all **accepted A** (PM lock 2026-10-31; user lock-all-oq-a). Rejected options remain below for audit trail only.

### OQ-M15-01 — Sim model — **ACCEPTED A**

| Option | Proposal | Lock |
|--------|----------|------|
| **A (ACCEPTED)** | **2D height-field / shallow-water** (CPU); scheme §3.1 | **LOCKED** |
| B | 2D SPH-lite particles | Rejected |
| C | 3D SPH / grid NS (heavier; Arc compute) | Rejected |
| D | Particle fountain / spray only (minimal) | Rejected |

**Rationale for A:** Best “liquid” look per KiB; deterministic; no new GPU compute stack; matches 5 s shippable demo + tiny-binary spirit. Survey finds path Vulkan only — not a fluid solver.

### OQ-M15-02 — Dimensionality / axes — **ACCEPTED A**

| Option | Proposal | Lock |
|--------|----------|------|
| **A (ACCEPTED)** | 2D field on **XZ**, height **+Y** | **LOCKED** |
| B | 2D field on XY, height +Z | Rejected |
| C | Full 3D domain | Rejected |

**Rationale for A:** Matches M3/M5 gravity −Y and scene ground planes.

### OQ-M15-03 — Module path — **ACCEPTED A**

| Option | Proposal | Lock |
|--------|----------|------|
| **A (ACCEPTED)** | New **`farmos:liquid`**; do not extend `farmos:physics` | **LOCKED** |
| B | Extend `farmos:physics` with fluid types | Rejected |
| C | Fold into `farmos:scene` | Rejected |

**Rationale for A:** Honors RD-M5-04 (fluids OUT of M5) without editing M5; clean DCE boundary.

### OQ-M15-04 — API shape — **ACCEPTED A**

| Option | Proposal | Lock |
|--------|----------|------|
| **A (ACCEPTED)** | `LiquidWorld` + `HeightField` + `step` / optional `linkMesh`; CPU backend constant | **LOCKED** |
| B | Single `FluidSystem` mega-type | Rejected |
| C | Immediate-mode functions only (no classes) | Rejected |

**Rationale for A:** Mirrors M5 `World`/`RigidBody` patterns users already know.

### OQ-M15-05 — Physics integration — **ACCEPTED A**

| Option | Proposal | Lock |
|--------|----------|------|
| **A (ACCEPTED)** | Separate module; **no** two-way coupling in M15; MAY coexist | **LOCKED** |
| B | Require fluid → rigid coupling | Rejected |
| C | Forbid importing both modules | Rejected |

**Rationale for A:** Ships liquid+mp4 without inventing coupled solver scope.

### OQ-M15-06 — MP4 encoder strategy — **ACCEPTED A**

| Option | Proposal | Lock |
|--------|----------|------|
| **A (ACCEPTED)** | External **`ffmpeg`** CLI on PATH; temp frames → mux via **`Renderer.saveMP4`**; `# requires_ffmpeg` soft-skip; STRICT → trap 123-family | **LOCKED** |
| B | Tiny bundled muxer / H.264 inside PE | Rejected |
| C | PNG/image sequence only (no mp4) | Rejected |
| D | Link ffmpeg libraries statically | Rejected |

**Rationale for A:** Meets user “输出mp4” without blowing hello / liquid PE; matches “deps & size budgets matter.” C fails the user ask; B/D fight tiny-binary.

### OQ-M15-07 — Duration / fps / resolution defaults — **ACCEPTED A**

| Option | Proposal | Lock |
|--------|----------|------|
| **A (ACCEPTED)** | **5.0 s**, **30 fps**, **640×360**; M3 raster; **two** sim steps (dt=1/60) per display frame | **LOCKED** |
| B | 5.0 s, 24 fps, 1280×720 | Rejected |
| C | 5.0 s, 60 fps, 640×360 | Rejected |
| D | Leave defaults unspecified | Rejected |

**Rationale for A:** Exact user duration; 30 fps is a round 150 frames; 640×360 keeps CPU raster+sim feasible on TommyLaptop.

### OQ-M15-08 — Size budget — **ACCEPTED A**

| Option | Proposal | Lock |
|--------|----------|------|
| **A (ACCEPTED)** | hello ≤20; DCE; **`S_liq − S_scene ≤ 96 KiB`**; ffmpeg external (no H.264 in PE); prior caps unchanged | **LOCKED** |
| B | Allow ≤128 KiB liquid delta | Rejected |
| C | Tighten ≤64 KiB (M5-like; may be tight for height-field + sync) | Rejected |
| D | No new delta gate (hello only) | Rejected |

**Rationale for A:** Between M5’s 64 KiB and path demos; height-field + mesh sync likely needs more than rigid-only 64 KiB.

### OQ-M15-09 — Platforms / soft-skip — **ACCEPTED A**

| Option | Proposal | Lock |
|--------|----------|------|
| **A (ACCEPTED)** | Windows primary for mp4+size; Linux `# requires_ffmpeg` soft-skip; CPU liquid fixtures always green; no Vulkan required for liquid ACs | **LOCKED** |
| B | Require ffmpeg on Linux CI | Rejected |
| C | Windows-only (no Linux soft-skip policy) | Rejected |

**Rationale for A:** Matches M7–M14 CI reality; liquid itself is CPU.

### OQ-M15-10 — OUT list confirmation — **ACCEPTED A**

| Option | Proposal | Lock |
|--------|----------|------|
| **A (ACCEPTED)** | OUT: soft cloth; 3D SPH (unless later OQ); fluid↔rigid coupling; PE-bundled H.264; window; frame-gen; DLSS/Enabler; weaken M12–M14 floors; edit M5 body | **LOCKED** |
| B | Allow soft cloth in same milestone | Rejected |
| C | Allow weakening a perf floor if liquid demo needs GPU time | Rejected |

**Rationale for A:** Keeps M15 focused on the user ask; perf floors stay orthogonal.

---

## 11. Amendments relative to prior milestones (binding under OQ-M15-01..10 A)

- M1–M14 bodies **untouched**. M5 RD-M5-04 remains the M5 record; fluids arrive only via M15.
- New module `farmos:liquid`; additive **`Renderer.saveMP4`**.
- New trap exit **123** (family) — must not collide with 117–122.
- New harness header `# requires_ffmpeg:` (soft-skip).
- M12–M14 perf / quality / OUT lists **stand**.

---

## 12. Document history

| Date | Entry |
|------|-------|
| 2026-10-31 | **DRAFT** created. User ask: liquid sim + mp4 ~5 s. Context tip `8b19896b` / product `4d7523ec`. Cite M5 RD-M5-04 fluids OUT — reopen via new milestone only. Survey: PNG-only media; no ffmpeg pipeline; Vulkan is path not fluid. Open OQ-M15-01..10; recommend **A** (2D height-field CPU; `farmos:liquid`; external ffmpeg; 5 s / 30 fps / 640×360; ≤96 KiB liquid delta; hello ≤20; OUT cloth/3D SPH/bundled H.264/perf-floor weaken). Status **DRAFT** — not FINAL. poteto **HOLD**. No implementation code. M1–M14 bodies untouched. |
| 2026-10-31 | **FINAL**. PM/user lock-all-oq-a: OQ-M15-01..10 all **A**. Refined: shallow-water §3.1; **two** `step()` per display frame at dt=1/60; **`Renderer.saveMP4`**; STRICT env `FARMOS_VIDEO_STRICT=1`; video glue ≤32 KiB if needed. poteto **HOLD** until PM assigns. No implementation code. M1–M14 / M5 bodies untouched. |

---

## Appendix A — Demo shape (AC sketch; binding loop)

```
import { Scene, Mesh, PlaneGeometry, MeshBasicMaterial,
         PerspectiveCamera, Renderer } from "farmos:scene";
import { LiquidWorld, HeightField } from "farmos:liquid";

function main(): int {
  // build scene + HeightField, linkMesh, setSize(640, 360)
  // for i in 0 .. 149:          // 150 display frames @ 30 fps
  //   world.step();             // dt = 1/60
  //   world.step();             // second step — binding two-steps-per-frame
  //   renderer.render(scene, camera);
  //   // capture frame (implementation-defined buffer grab)
  // renderer.saveMP4("liquid_5s.mp4");
  return 0;
}
```

Stripped size of a program shaped like the above (without requiring ffmpeg link) is `S_liq` for AC-M15-04.

---

## Appendix B — Gate summary (binding under OQ-M15-01..10 A)

- **Sim:** 2D height-field / shallow-water, CPU, XZ +Y; §3.1; two steps/frame @ dt=1/60.
- **Module:** `farmos:liquid` (M5 untouched; RD-M5-04 stands for M5).
- **Video:** `Renderer.saveMP4` + external ffmpeg; ~5 s @ 30 fps @ 640×360; no MP4 SHA golden.
- **Size:** hello ≤20; `S_liq − S_scene ≤ 96 KiB`; prior PE caps unchanged.
- **OUT:** cloth; 3D SPH; bundled H.264; window; frame-gen; DLSS/Enabler; weaken M12–M14 floors.
- **Context tip:** master `8b19896b`; product `4d7523ec` (informational).
- **poteto:** **HOLD** until PM assigns after this FINAL.
