# Farmos M8 — Path Tracer Engine Speedup

**Status:** FINAL (PM 2026-10-15: OQ-M8-01 .. OQ-M8-05 all option A)  
**Milestone:** M8 — path tracer engine speedup (CPU M4 + Vulkan M7)  
**Depends on:** `spec/M1-core.md` (FINAL), `spec/M2-math.md` (FINAL), `spec/M3-scene.md` (FINAL), `spec/M3.5-concurrency.md` (FINAL), `spec/M4-ray.md` (FINAL), `spec/M5-physics.md` (FINAL), `spec/M6-hardening.md` (FINAL), `spec/M7-vulkan-path.md` (FINAL)  
**Normative keywords:** MUST / SHOULD / MAY per RFC 2119.  
**Date:** 2026-10-15

This document specifies Farmos M8: **engine-level** speedups for the existing M4 CPU path tracer and the M7 Vulkan compute backend for the same path, without changing the silver-still artwork composition and without forcing lower default spp for product quality. It does **not** specify compiler, runtime, or stdlib implementation code. It does **not** edit M1–M7 milestone markdown bodies; normative amendments relative to those milestones live in §18 of this file.

Open questions: none. PM accepted option A for OQ-M8-01 .. OQ-M8-05 on 2026-10-15. Also binding: artwork composition locked; denoise OUT; 256 spp quality OK; engine speedup only; CPU fallback + DCE unchanged; trap **119** at `setPathSampling` for invalid mode; RR start depth ≥ 3 when enabled. Those decisions are recorded in §17. The body below is that decision, not a draft.

Related: `/workspace/farmos/PLAN.md` (M8 section), `spec/M4-ray.md`, `spec/M7-vulkan-path.md`, `spec/tests/M8/README.md`.

**Binding product intent (PM / user 2026-10-15):**
- Speed comes from the **engine**, not from lowering artwork spp.
- Default quality for artwork MAY stay **256 spp**.
- Do **not** change silver-still artwork composition (`out/artwork_silver_still.png` is the visual target; normative bench program `examples/artwork_silver_still.fm` @ 1920×1080 — file may be added under `examples/` if missing).
- CPU fallback and M7 DCE / size spirit remain.
- **Denoise as a product feature is OUT** (cheap optional denoise MAY be noted as non-goal / future; not an M8 AC).

**Target platform (PLAN.md):** Windows x64 primary (TommyLaptop) for Vulkan wall-clock; Linux CI for CPU wall-clock and soft-skip GPU fixtures when no device.

---

## 1. Scope and non-goals

### 1.1 In scope (M8 MUST implement)

- **Russian roulette / early path termination** as an **opt-in** policy (§3); default OFF so M4 / M7 compat goldens stay bit-identical on CPU.
- **Better sampling** behind an additive sampling mode (§4): improved RectAreaLight sampling and/or MIS; default `MODE_M4` preserves the M4 estimator.
- **Vulkan occupancy / scheduling** improvements (§6): workgroup guidance, tile or scanline dispatch, spp batching inside one `renderPath`, fewer host round-trips — without breaking M7 fallback, STRICT, FORCE_FAIL, or DCE.
- **Optional progressive / adaptive spp API** that is **additive** only (§5); `renderPath` / `setPathBackend` signatures and existing semantics stay stable.
- Wall-clock benchmarks on a fixed silver-still scene @ 1920×1080 / 256 spp (§7, AC-M8-12..14).
- Regression: M4 / M7 visual fixtures still pass under default / compat mode (§8, AC-M8-01..03).
- Fixtures under `spec/tests/M8/` (§14, §15).
- Keep `# png_tol` and visual ACs from M4 / M7; CPU bit-identical where M4 requires; GPU still `# png_tol: 2` vs CPU / M4 golden where applicable.

### 1.2 Explicit non-goals

| Not in M8 | Note |
|-----------|------|
| Denoise as a product feature | OUT unless a future milestone; no denoise ACs |
| Changing silver-still mesh / light / camera / material composition | Artwork locked; only engine / API flags |
| Forcing lower default spp for artwork quality | 256 spp remains allowed / preferred for quality |
| GPU raster / accelerating M3 `render()` | Still CPU-only (M7 §1.2) |
| DX12, CUDA, OpenCL, Metal, WebGPU, RTX ray-query | Vulkan compute only (same as M7) |
| Windowing / swapchain / interactive viewers | still `savePNG` only |
| Replacing M4 goldens by default | Compat / default modes preserve SHA |
| Breaking `renderPath` / `setPathBackend` without migration | Forbidden; adaptive API is additive only (OQ-M8-03 A) |
| Editing M1–M7 milestone markdown bodies | amendments in §18 |
| Requiring a GPU for Linux CI green | soft-skip GPU fixtures (M7) |

### 1.3 Design constraints

- M4 CPU goldens and `path_ref.py` remain authoritative under **compat / default** modes.
- M7 fallback / STRICT / FORCE_FAIL / traps 117–118 / DCE budgets remain binding.
- Hello ≤ 20 KiB; unused GPU MUST NOT grow hello or pull Vulkan symbols (M6/M7 spirit).
- Glass+mirror CPU ≤ 300 KiB; GPU-linked ≤ 400 KiB (M7 OQ-M7-04 A).
- Pixel values under compat mode MUST NOT depend on thread scheduling (M4 determinism).
- Speed features that change the estimator (RR, MIS, better area-light pdf) MUST be gated so goldens can stay stable.

---

## 2. Relationship to M4 and M7

### 2.1 M4 integrator baseline (unchanged under compat)

From `spec/M4-ray.md` FINAL (normative reminder, not a redefinition):

- Forward path tracer; **no Russian roulette** — paths run to `maxBounces`.
- NEE for Ambient (first non-delta), Point, Directional, RectAreaLight; at most one indirect ray per hit.
- RectAreaLight: **one uniform** point sample, pdf `1/area`.
- **No MIS** between BSDF and light.
- No importance-sampled emissive meshes.
- Deterministic Hammersley; pixel values MUST NOT depend on thread scheduling.
- Defaults: `samples=1`, `maxBounces=4`.
- Progressive accumulation via multiple `renderPath` calls + `resetAccumulation`.

### 2.2 M7 backend baseline (unchanged)

From `spec/M7-vulkan-path.md` FINAL:

- `setPathBackend` / `getPathBackend` / `isVulkanPathAvailable`; `PATH_BACKEND_CPU=0`, `PATH_BACKEND_VULKAN=1`; keep `renderPath`.
- `FARMOS_PATH_GPU_STRICT=1` → trap 117; default silent CPU fallback; backend stays VULKAN.
- Trap 118 at `setPathBackend` for invalid backend; `FARMOS_VULKAN_FORCE_FAIL=1`; `# requires_vulkan` soft-skip.
- GPU workgroup size was implementation-defined; `FARMOS_THREADS` = CPU only.
- GPU images: `# png_tol: 2` vs CPU/M4 golden; CPU bit-identical; GPU float32 OK.
- Size: GPU-linked ≤ 400 KiB; CPU glass+mirror ≤ 300 KiB.

### 2.3 What M8 adds

M8 layers **optional** estimator / scheduling improvements on top of M4+M7. With all M8 speed features at their **defaults**:

- CPU `renderPath` MUST remain **bit-identical** to M4 for existing M4 / M7 CPU twins.
- Successful Vulkan `renderPath` MUST still satisfy M7 `# png_tol: 2` against those goldens when fixtures assert GPU pixels.
- M7 fallback, STRICT, FORCE_FAIL, DCE, and size budgets MUST still hold.

---

## 3. Russian roulette / early path termination (OQ-M8-01 A)

### 3.1 Default

- Russian roulette is **OFF** by default on a newly constructed `Renderer`.
- With RR off, the bounce loop MUST match M4 §10 (depth `0 .. maxBounces` inclusive; no early termination).
- M4 / M7 golden fixtures that never enable RR MUST keep CPU SHA identity.

### 3.2 Opt-in API

`farmos:scene` / `Renderer` MUST additionally export:

```
setPathRoulette(enabled: bool): void
getPathRoulette(): bool
```

- Default: `getPathRoulette()` → `false`.
- `setPathRoulette(true)` enables RR for subsequent `renderPath` calls on this renderer.
- Enabling RR MUST NOT change `setPathBackend` state, accumulator contents, or display buffer by itself.
- Env override (optional, for benches / scripts): if `FARMOS_PATH_ROULETTE` is exactly `1`, implementations MAY treat RR as enabled for the process when the program never called `setPathRoulette`; if the program called `setPathRoulette`, the method wins. Fixtures that care about bit-identity MUST call `setPathRoulette(false)` explicitly or leave default and unset the env.

### 3.3 Recommended RR policy (when enabled)

When RR is enabled:

1. Paths still respect `maxBounces` as a hard cap (MUST NOT exceed M4 depth bound).
2. After **depth ≥ 3** (normative start depth), apply throughput-based roulette:
   - Let `q = clamp(max(throughput.r, throughput.g, throughput.b), 0.05, 0.95)` (clamp constants MAY be tuned slightly for quality; MUST be documented in poteto notes and fixture-stable once chosen).
   - Draw one deterministic unit float from the existing M4 RNG stream for that `(pixel, sampleIndex, bounce)` slot (MUST NOT break Hammersley indices used for BSDF / light when RR is off; when RR is on, consume one extra RNG draw per candidate bounce — document the exact RNG slot in implementation notes).
   - If `u ≥ q`, terminate the path; else divide throughput by `q` and continue.
3. Delta glass / mirror events SHOULD still run to the bounce cap or until absorption; RR on non-delta continuation MUST be documented consistently for CPU and GPU.
4. CPU with RR on NEED NOT match M4 SHA; fixtures use separate goldens or `# png_tol` / non-`run_png` kinds (§8.2).
5. GPU with RR on uses the same policy as CPU RR; compare with `# png_tol: 2` to the **RR-on CPU** reference when asserting GPU, not to M4 goldens.

### 3.4 Motivation

RR reduces average path length for high-`maxBounces` / high-spp artwork without changing the default estimator for regression fixtures.

---

## 4. Better sampling / MIS (OQ-M8-02 A)

### 4.1 Default = M4 estimator

```
const PATH_SAMPLING_M4: int = 0
const PATH_SAMPLING_MIS: int = 1
// Optional alias for area-light-only improvement without full MIS:
const PATH_SAMPLING_AREA_SOLID: int = 2

setPathSampling(mode: int): void
getPathSampling(): int
```

- Default: `PATH_SAMPLING_M4` (0).
- Invalid mode (not in the supported set) → trap **119** `runtime error: invalid path sampling mode` **at `setPathSampling`** (see §10). Supported set: `{0, 1}` required; `2` MAY be implemented as improved RectArea sampling with or without MIS — if unimplemented, `2` MUST trap 119.
- `PATH_SAMPLING_M4`: exact M4 §10.5–§10.7 behavior (uniform RectArea pdf `1/area`; NEE; no MIS).
- `PATH_SAMPLING_MIS`: enable multiple importance sampling between BSDF and light sampling for **non-delta** surfaces, and use an improved RectAreaLight sample (SHOULD use **cosine-weighted toward the shading point** or **solid-angle** sampling of the rectangle; MUST document pdf and estimator in implementation notes). Ambient / Point / Directional NEE remain; MIS weights MUST be documented (SHOULD use balance heuristic).
- Emissive meshes remain **not** importance-sampled as lights (M4 OQ-M4-02), unless a future OQ reopens that.

### 4.2 When goldens change

| Mode | CPU vs M4 golden | GPU vs CPU |
|------|------------------|------------|
| `PATH_SAMPLING_M4` (default) | Bit-identical (SHA) | `# png_tol: 2` (M7) |
| `PATH_SAMPLING_MIS` / improved area | New goldens or `# png_tol` vs MIS-CPU reference | `# png_tol: 2` vs that MIS-CPU reference |

### 4.3 Motivation

M4 already has NEE; MIS + better area-light pdf reduce variance per sample for RectArea-heavy scenes (Cornell / silver still) without requiring lower spp.

---

## 5. Progressive / adaptive spp API (OQ-M8-03 A)

### 5.1 Additive only

`renderPath(scene, camera)` signature and M4/M7 semantics MUST remain:

- One call adds exactly `samples` new samples per pixel (M4 §12 / M7 §6.4), unless an **explicit** adaptive API is used (§5.2).
- `setSamples` / `setMaxBounces` / `resetAccumulation` / `setPathBackend` unchanged.

Additive methods:

```
setPathAdaptiveSpp(enabled: bool): void
getPathAdaptiveSpp(): bool
setPathAdaptiveTarget(varianceOrNoiseHint: float): void   // interpretation documented; default implementation-defined soft target
getPathAdaptiveTarget(): float
```

- Defaults: adaptive OFF; when OFF, behavior is identical to M4 progressive accumulation.
- When adaptive ON: **`samples` is the per-call maximum**; a single `renderPath` MAY add a **variable** number of samples per pixel (or per tile) up to that maximum and MAY stop early per pixel when a cheap variance proxy is below `getPathAdaptiveTarget()`. Exact variance proxy is implementation-defined; MUST be documented in poteto notes.
- Adaptive ON MUST NOT be required for any M4/M7 fixture. Fixtures that assert fixed sample indices (progressive mean) MUST leave adaptive OFF.
- Migration: none required for existing programs.

### 5.2 Explicit non-break

M8 MUST NOT change `renderPath` to imply adaptive behavior by default, MUST NOT remove `setSamples`, and MUST NOT require callers to pass spp differently.

---

## 6. Vulkan occupancy and scheduling

### 6.1 Goals

Improve GPU wall-clock for the same integrator / spp without breaking:

- M7 silent fallback / STRICT / FORCE_FAIL
- Dynamic Vulkan load / DCE (hello ≤ 20 KiB, no Vulkan symbols when unused)
- Image contract (`# png_tol: 2` when asserting GPU)

### 6.2 Workgroup size

- M7 left workgroup size implementation-defined. M8 SHOULD pick and document a default workgroup (recommend **8×8** or **16×16** pixels, or 1D `256` lanes mapping a scanline chunk) tuned on TommyLaptop.
- Implementations MAY expose `FARMOS_VULKAN_WORKGROUP=WxH` as a non-normative tuning hook; fixtures MUST NOT require a specific workgroup for image correctness (only for optional bench scripts).

### 6.3 Tile / scanline scheduling

- Prefer **coherent tiles** or scanline strips so neighboring pixels share material / BVH cache lines.
- CPU tile pool (M4 §11) remains controlled by `FARMOS_THREADS` only.
- GPU dispatch MUST NOT read `FARMOS_THREADS` for workgroup sizing (M7 AC-M7-12 preserved).

### 6.4 Spp batching and host round-trips

- One `renderPath` with `setSamples(n)` SHOULD dispatch GPU work that accumulates **all `n` samples** with minimal host↔device round-trips (prefer one upload, one or few dispatches, one readback of the display / accumulator means).
- Progressive multi-call accumulation (M4) remains correct: each call MAY upload dirty scene data only when needed; SHOULD avoid full scene re-upload when matrices / meshes unchanged (implementation quality; not a hard AC unless bench fails).
- MUST NOT require the application to call `renderPath` once per sample to get M8 speedups.

### 6.5 CPU scheduling wins (same milestone)

On CPU, M8 MAY:

- Improve tile grain / parallel scheduling without changing deterministic pixel math under compat mode.
- Share BVH build improvements with GPU upload path.
- Apply RR / MIS only when enabled (§3–§4).

Determinism under compat: pixel values MUST still be independent of `FARMOS_THREADS` (M4).

---

## 7. Benchmarks (silver still)

### 7.1 Normative bench program

- **Composition target (MUST NOT change):** `out/artwork_silver_still.png` (existing 1920×1080 visual reference).
- **Normative program path:** `examples/artwork_silver_still.fm` — **may not exist yet** in the tree; poteto / examples owner MUST add it under `examples/` matching that composition (same meshes, materials, lights, camera). M8 does not redesign the artwork.
- Resolution: **1920×1080**.
- Quality: **`setSamples(256)`**, `setMaxBounces` as in the artwork program (document in the `.fm`; if unspecified historically, recommend **4** or the value used to produce `out/artwork_silver_still.png`).
- Backends measured:
  1. TommyLaptop **Vulkan** (`setPathBackend(PATH_BACKEND_VULKAN)`), successful GPU path (not fallback).
  2. **Linux CPU** (`PATH_BACKEND_CPU`), `FARMOS_THREADS` set to a documented host default (recommend physical core count; record in bench log).

### 7.2 Method

- Wall-clock: **median of 3 timed runs after 1 warm-up** for one `renderPath` (steady-state). Implementations MAY also record end-to-end including first call in notes.
- Record: host id, backend, RR on/off, sampling mode, adaptive on/off, spp, resolution, threads / workgroup, elapsed ms.
- Baseline: M7-era binary (or M8 binary with all speed features at defaults that match M4 estimator + M7 scheduling) on the same host. **Measure and record M7 baselines before claiming speedups.**
- Candidate: M8 with recommended speed feature set for artwork (RR on, `PATH_SAMPLING_MIS`, adaptive off, Vulkan scheduling improvements).

### 7.3 Targets (OQ-M8-04 A — binding)

| Host / backend | Target vs M7 baseline |
|----------------|----------------------|
| TommyLaptop Vulkan | ≥ **2×** wall-clock speedup |
| Linux CPU | ≥ **1.5×** wall-clock speedup |

Exact millisecond baselines are measured by poteto / eggtooth and recorded in `spec/tests/M8/bench/` (or scripts). AC-M8-12..14 gate the ratio against those recorded baselines.

### 7.4 Quality during bench

- Artwork spp stays 256 unless PM explicitly changes product quality (out of M8 engine scope).
- Visual composition MUST match `out/artwork_silver_still.png` within documented tolerance when speed modes are on (separate from M4 SHA fixtures): SHOULD use `# png_tol` or a dedicated artwork golden — **fixture goldens for M4 twins stay strict**.

---

## 8. Image contract

### 8.1 Compat / default mode

When `getPathRoulette() == false` AND `getPathSampling() == PATH_SAMPLING_M4` AND adaptive spp OFF:

- **CPU:** bit-identical to M4 goldens (SHA-256), same as M7 CPU twins.
- **GPU success:** `# png_tol: 2` vs CPU/M4 golden (M7).
- **GPU fallback:** CPU contract.

### 8.2 Speed mode (OQ-M8-05 A)

When RR and/or non-M4 sampling and/or adaptive spp is enabled:

- CPU NEED NOT match M4 SHA.
- Implementations MUST document which fixtures use speed mode.
- Regression suite keeps a **compat lane** that preserves M4 SHA.
- Speed-mode fixtures MAY use new goldens, `# png_tol`, or non-image asserts (API round-trip only).
- Demo / artwork programs MAY use speed mode with `# png_tol` or a dedicated artwork golden; they MUST NOT silently break M4 fixture SHA expectations.

---

## 9. Size / DCE

Unchanged spirit from M6 / M7:

| Program shape | Budget | Constraint |
|---------------|--------|------------|
| Hello (no path / no GPU) | ≤ **20 KiB** stripped | MUST NOT import Vulkan loader symbols |
| CPU glass+mirror `renderPath` without Vulkan opt-in | ≤ **300 KiB** | MUST NOT require Vulkan at load |
| GPU-linked demo (Vulkan opt-in reachable) | ≤ **400 KiB** | Dynamic load still required at first use |

M8 speed code (RR, MIS, adaptive, GPU schedulers) MUST be reachable only from path-tracing / GPU units so hello DCE still holds.

---

## 10. Runtime traps

Existing M4 / M7 traps unchanged (105, 107, 108, 111, 112, 113, 117, 118). Exit **109** still unused.

| Exit | Stderr | When |
|------|--------|------|
| **119** | `runtime error: invalid path sampling mode` | `setPathSampling` with unsupported mode |

No new trap for RR bool or adaptive bool. Invalid adaptive target floats: implementation MAY clamp; MUST NOT trap (clamp, no trap).

---

## 11. Compiler diagnostics

No new E0xxx required for M8. Ordinary type mismatches use M1 codes. Out-of-range sampling mode is runtime trap 119, not a compile error.

---

## 12. Acceptance criteria

| ID | Criterion |
|----|-----------|
| AC-M8-01 | Default / compat mode: CPU M4 twin fixtures (at least background + Cornell-class) remain **bit-identical** SHA to M4 / M7 CPU goldens. |
| AC-M8-02 | Default / compat mode: successful GPU images still satisfy `# png_tol: 2` vs CPU/M4 golden when `# requires_vulkan: true` (M7 spirit). |
| AC-M8-03 | M7 fallback / STRICT / FORCE_FAIL / trap 118 behavior unchanged; M8 features do not break them. |
| AC-M8-04 | `setPathRoulette` / `getPathRoulette` round-trip; default `false`; with `false`, M4 bounce loop unchanged. |
| AC-M8-05 | With RR enabled, paths terminate early under §3.3 policy; hard cap `maxBounces` still enforced; documented RNG slot. |
| AC-M8-06 | `setPathSampling` / `getPathSampling` default `PATH_SAMPLING_M4`; invalid mode → trap **119**. |
| AC-M8-07 | `PATH_SAMPLING_MIS` (when enabled) uses documented MIS + improved RectArea sampling; separate fixture / golden path from M4 SHA. |
| AC-M8-08 | Adaptive spp API additive; default OFF; with OFF, `renderPath` still adds exactly `samples` spp (M4). |
| AC-M8-09 | Enabling adaptive / RR / MIS MUST NOT change `setPathBackend` / `getPathBackend` contracts. |
| AC-M8-10 | Hello ≤ 20 KiB; no Vulkan symbols when GPU unused; CPU glass+mirror ≤ 300 KiB; GPU-linked ≤ 400 KiB. |
| AC-M8-11 | Denoise is not required; no denoise AC. |
| AC-M8-12 | Bench harness exists for `examples/artwork_silver_still.fm` @ 1920×1080 / 256 spp (add `.fm` if missing); composition matches `out/artwork_silver_still.png` intent. |
| AC-M8-13 | TommyLaptop Vulkan: ≥ **2×** wall-clock vs M7 baseline (OQ-M8-04 A); record before/after ms. |
| AC-M8-14 | Linux CPU: ≥ **1.5×** wall-clock vs M7 baseline (OQ-M8-04 A); record before/after ms. |
| AC-M8-15 | Vulkan scheduling: one `renderPath` with `n` samples SHOULD avoid per-sample host round-trips (§6.4); eggtooth MAY smoke-check via bench logs / instrumentation notes. |
| AC-M8-16 | Fixtures in §15 pass under their kinds (compat always; speed-mode where marked; GPU soft-skip without device). |

AC-M8-10, AC-M8-12..15 are eggtooth / script / bench checks (not all are ctest image compares).

---

## 13. Test harness

Reuse M4 / M7 harness kinds: `run`, `run_png`, `runtime_trap`.

M7 headers still apply when relevant:

| Header | Meaning |
|--------|---------|
| `# requires_vulkan: true` | Soft-skip without Vulkan device |
| `# png_tol: <int>` | Per-channel byte tolerance vs `# sha256:` reference |
| `# env: KEY=VALUE` | Child environment (`FARMOS_PATH_ROULETTE`, M7 vars, etc.) |

M8 MAY add (optional for eggtooth):

| Header | Meaning |
|--------|---------|
| `# path_mode: compat` | Assert RR off, sampling M4, adaptive off (documentation aid) |
| `# bench: true` | Not a CI fail gate by default; used by bench scripts |

Working directory for M8 fixtures: `spec/tests/M8`.

PLACEHOLDER SHA lines use 64 zero hex digits until poteto regenerates:

```
# sha256: 0000000000000000000000000000000000000000000000000000000000000000
```

---

## 14. Fixture outline

| Fixture | Kind | What |
|---------|------|------|
| 001_compat_m4_background | run_png | Compat defaults; M4-001 / M7-001 twin; real SHA when available |
| 002_roulette_off_golden | run_png | Explicit `setPathRoulette(false)`; same SHA as 001 / M4 twin |
| 003_sampling_mode_roundtrip | run | `setPathSampling` / `getPathSampling` 0→1→0; invalid → separate trap fixture |
| 004_adaptive_api_nobreak | run | Adaptive OFF; two `renderPath` with `setSamples(1)`; progressive mean still defined; print flags |
| 005_trap_invalid_sampling | runtime_trap | `setPathSampling(99)` → 119 |
| 006_compat_cornell_cpu | run_png | Optional Cornell-class compat twin (SHA) |
| 007_roulette_on_smoke | run | RR on; small spp; exit 0; no M4 SHA assert |
| 008_mis_mode_smoke | run_png | Optional; MIS on; `# png_tol` or placeholder SHA |
| bench / artwork | script | `examples/artwork_silver_still.fm` wall-clock; see README |

**Counts (stub pack):** ≥ 5 `.fm` harness fixtures + README + bench notes. Expand as PM requests.

---

## 15. Test index (source files)

| # | File | Kind |
|---|------|------|
| 001 | `001_compat_m4_background.fm` | run_png |
| 002 | `002_roulette_off_golden.fm` | run_png |
| 003 | `003_sampling_mode_roundtrip.fm` | run |
| 004 | `004_adaptive_api_nobreak.fm` | run |
| 005 | `005_trap_invalid_sampling.fm` | runtime_trap |
| 007 | `007_roulette_on_smoke.fm` | run |

Eggtooth / bench rows (no required `.fm` in stub pack): size/DCE (AC-M8-10), silver-still bench (AC-M8-12..14).

---

## 16. Open questions

**None.** PM decided OQ-M8-01 .. OQ-M8-05, all option A, on 2026-10-15. Also binding: artwork composition locked; denoise OUT; 256 spp quality OK; engine speedup only; CPU fallback + DCE unchanged; trap **119** at `setPathSampling`; RR start depth ≥ 3 when enabled. See §17.

---

## 17. Resolved decisions

PM 2026-10-15 accepted option A for every OQ below. Binding product intent and implementation locks follow.

| ID | Decision |
|----|----------|
| OQ-M8-01 | RR **off** by default; enable via `setPathRoulette(true)` and/or `FARMOS_PATH_ROULETTE=1` (method wins if called). |
| OQ-M8-02 | Additive `setPathSampling`; default `PATH_SAMPLING_M4`; MIS / better RectArea behind `PATH_SAMPLING_MIS` (optional `PATH_SAMPLING_AREA_SOLID`). |
| OQ-M8-03 | Additive adaptive/progressive API only; `renderPath` default semantics unchanged. |
| OQ-M8-04 | ≥ **2×** TommyLaptop Vulkan; ≥ **1.5×** Linux CPU on silver still 1920×1080 @ 256 spp vs M7 baseline. |
| OQ-M8-05 | Compat preserves M4 SHA; speed mode may use documented variance + `# png_tol` / separate goldens. |
| RD-M8-01 | Artwork composition locked to `out/artwork_silver_still.png`; do not change for speed. |
| RD-M8-02 | Denoise as a product feature is **OUT** of M8. |
| RD-M8-03 | Default artwork quality MAY stay **256 spp**; speed from engine, not forced lower spp. |
| RD-M8-04 | CPU fallback + M7 DCE / size budgets unchanged (hello ≤ 20 KiB; CPU glass+mirror ≤ 300 KiB; GPU-linked ≤ 400 KiB). |
| RD-M8-05 | Trap **119** at `setPathSampling` for invalid / unsupported mode. |
| RD-M8-06 | When RR enabled: start depth ≥ **3**; throughput roulette with documented clamp / RNG slot. |
| RD-M8-07 | Adaptive: `samples` is per-call maximum; default OFF. |
| RD-M8-08 | Vulkan workgroup size SHOULD be documented (recommend 8×8 / 16×16 or 1D 256); not required for image correctness. |
| RD-M8-09 | OQ-M8-01..05 accepted option A (PM 2026-10-15). |
| RD-M8-10 | Normative bench program `examples/artwork_silver_still.fm` @ 1920×1080 (add if missing). |

---

## 18. Amendments relative to M4 / M7

M4-ray.md and M7-vulkan-path.md are not edited.

| # | Amendment |
|---|-----------|
| 1 | `Renderer` gains `setPathRoulette` / `getPathRoulette` (default false). |
| 2 | `farmos:scene` exports `PATH_SAMPLING_M4`, `PATH_SAMPLING_MIS` (and optional `PATH_SAMPLING_AREA_SOLID`); `setPathSampling` / `getPathSampling`. |
| 3 | Optional adaptive spp setters/getters; default off. |
| 4 | Trap **119** invalid path sampling mode. |
| 5 | Env `FARMOS_PATH_ROULETTE` optional override (§3.2). |
| 6 | Vulkan scheduling / workgroup guidance and spp batching expectations (§6). |
| 7 | Bench program `examples/artwork_silver_still.fm` @ 1920×1080 / 256 spp; composition locked to `out/artwork_silver_still.png`. |
| 8 | Compat vs speed image contract (§8). |
| 9 | Fixtures under `spec/tests/M8/`; harness reuse M4/M7. |
| 10 | Denoise explicitly out of M8 product scope. |

---

## Appendix A — Compat background twin (001)

Same shape as `spec/tests/M4/001_background_only.fm` / `spec/tests/M7/001_cpu_background.fm` (16×16, clear `0x4488ff`). Leave RR / sampling / adaptive at defaults.

## Appendix B — Sampling round-trip sketch (003)

```
renderer.setPathSampling(PATH_SAMPLING_M4);
println(renderer.getPathSampling()); // 0
renderer.setPathSampling(PATH_SAMPLING_MIS);
println(renderer.getPathSampling()); // 1
renderer.setPathSampling(PATH_SAMPLING_M4);
println(renderer.getPathSampling()); // 0
```

## Appendix C — Artwork bench note

If `examples/artwork_silver_still.fm` is missing, add it as a port of the composition that produced `out/artwork_silver_still.png` (1920×1080). Do not alter composition for speed; enable M8 speed flags in a **bench wrapper** or documented `# env` / setters, not by deleting geometry.

---

## Document history

- 2026-10-15: DRAFT. Engine speedup for M4 CPU + M7 Vulkan path; RR / MIS / adaptive / Vulkan occupancy; OQ-M8-01..05 recommend A; denoise OUT; silver-still bench @ 1920×1080 / 256 spp; fixtures under `spec/tests/M8/`.
- 2026-10-15: FINAL. PM accepted OQ-M8-01 .. OQ-M8-05, all option A. Binding: artwork lock; denoise OUT; 256 spp OK; trap 119; RR start depth ≥ 3; wall-clock ≥2× Vulkan / ≥1.5× Linux CPU. Open questions cleared.
