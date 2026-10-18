# Farmos M10 — Denoise / Interactive Path Tracing

**Status:** FINAL (PM 2026-10-18: OQ-M10-01..08 all A; user 2026-10-18 all-A)  
**Milestone:** M10 — denoise for stills + progressive interactive preview for the M4 path tracer  
**Depends on:** `spec/M1-core.md` (FINAL), `spec/M2-math.md` (FINAL), `spec/M3-scene.md` (FINAL), `spec/M3.5-concurrency.md` (FINAL), `spec/M4-ray.md` (FINAL), `spec/M5-physics.md` (FINAL), `spec/M6-hardening.md` (FINAL), `spec/M7-vulkan-path.md` (FINAL), `spec/M8-path-speed.md` (FINAL), `spec/M9-hardware-rt.md` (FINAL)  
**Normative keywords:** MUST / SHOULD / MAY per RFC 2119.  
**Date:** 2026-10-18 (FINAL)

This document specifies Farmos M10: **additive denoise** and a **progressive interactive preview** path for the existing M4 path tracer (and M7/M8 compute + M9 HW RT backends). It does **not** specify compiler, runtime, or stdlib implementation code. It does **not** edit M1–M9 milestone markdown bodies; normative amendments relative to those milestones live in §19 of this file.

Open questions: **none.** OQ-M10-01 .. OQ-M10-08 are all **accepted A** (PM lock 2026-10-18; user 2026-10-18 all-A). Binding under A: stills denoise + progressive preview with **no** OS window/swapchain; Farmos-owned spatial denoise (no OIDN/NVIDIA DLL); silver still ≤**64 spp** + denoise ≤**1500 ms** / `# png_tol: 8` vs 256-spp undenoised; first useful ≤**250 ms** / step ≤**100 ms**; additive `setPathDenoise` default OFF; DLSS/vendor upscalers OUT; denoise-linked ≤**600 KiB**; soft denoise fail / `FARMOS_PATH_DENOISE_STRICT=1` → trap **120**-family; M9 RT STRICT unchanged. Those decisions are recorded in §18. Body gates in §8–§10 / AC-M10-05..09 are **binding under A**.

Related: `/workspace/farmos/PLAN.md` (M10 section), `spec/M4-ray.md`, `spec/M7-vulkan-path.md`, `spec/M8-path-speed.md`, `spec/M9-hardware-rt.md`, `spec/tests/M10/README.md`.

**Binding product intent (PM / user 2026-10-18):**
- M9 hardware RT merged to master as `52b1aebc` (PR #15). OQ-M9-08 A: silver still 1920×1080 @ 256 spp TommyLaptop Arc CLEAN RT median **4881 ms** / compute ~**29335 ms** / **6.01×**. Denoise was **OUT** for M9 (OQ-M9-07 A).
- User chose next direction: **denoise-interactive** (not more pure speed grind, not pause).
- Pain: ~**5 s** stills are still not interactive vs mobile-game-class GPU feel.
- Goal: (1) cut still wall-clock via **lower spp + denoise** while keeping silver-still quality; (2) ship a **progressive preview** API so useful frames arrive in hundreds of ms, not seconds.
- Artwork lock: silver still **v1 composition only**; do not reopen artwork variants.
- Stack: farmc→C11; Vulkan compute (M7/M8) + `PATH_BACKEND_VULKAN_RT` (M9 AS + `VK_KHR_ray_query` in compute); soft RT→compute→CPU / STRICT 117; no OptiX; **DLSS/Swapper OUT** (OQ-M10-06 A).
- **M10 reopens denoise as a product feature** (supersedes M9 OQ-M9-07 A / M8 denoise-OUT for this milestone’s scope). M9 HW RT remains required and unchanged.
- Non-goals: full game engine; OS window/swapchain (OQ-M10-01 A); DLSS/frame-gen; Swapper DLL hacks; OptiX; GPU raster rewrite; changing silver-still composition.

**Target platform (PLAN.md):** Windows x64 primary (TommyLaptop) for denoise + progressive wall-clock; Linux CI for CPU + soft-skip GPU/RT/denoise-GPU fixtures when no device.

**Amendment vs M9:** M9 §1.2 / OQ-M9-07 A listed denoise-as-product as **out of scope**. M10 **reopens** denoise as **in scope**. M9 backends (`0`/`1`/`2`), STRICT, size RT ≤500 KiB, and wall-clock ≤5000 ms / ≥5× remain binding for undenoised 256-spp stills.

---

## 1. Scope and non-goals

### 1.1 Product split (MUST)

M10 has two product lanes. Both are **required** under OQ-M10-01 A:

| Lane | What | Quality / speed lever |
|------|------|------------------------|
| **Stills denoise** (offline / spatial) | After (or as part of) `renderPath`, apply a spatial denoise pass so programs MAY use **fewer spp** and still match a high-spp reference within documented tolerance | Cut wall-clock vs M9 256-spp undenoised floor (**4881 ms** class) |
| **Interactive / progressive** | Additive progressive accumulation / preview API so a useful low-spp frame appears quickly, then accumulates; optional denoise on preview frames | First useful frame hundreds of ms; no OS window required under OQ-M10-01 A |

Under OQ-M10-01 A, **no** OS window / swapchain / present path is required in M10. Preview is via repeated `renderPath` / progressive buffer APIs + `savePNG` or buffer readback.

### 1.2 In scope (M10 MUST implement)

- Additive denoise API on `Renderer` (§3): `setPathDenoise` / `getPathDenoise` / `PATH_DENOISE_*`; default **OFF**.
- Farmos-owned spatial denoise stack (OQ-M10-02 A): no OIDN / NVIDIA DLL dependency required (§5).
- Stills quality/speed gate on silver still with lower spp + denoise ON (§8).
- Progressive / interactive preview API additive to M8 adaptive helpers; `renderPath` default unchanged (§4).
- Compatibility: denoise MUST work with backends `0`/`1`/`2` when the chosen denoise path is available; M9 STRICT/fallback for RT/compute selection **unchanged** (§6).
- Size / DCE: hello ≤ 20 KiB unused; denoise-linked soft budget ≤ **600 KiB** (OQ-M10-07 A); prior RT ≤500 / compute ≤400 / glass+mirror ≤300 unchanged (§10).
- Trap **120** for invalid denoise mode (§11).
- Soft denoise unavailable: leave noisy unless `FARMOS_PATH_DENOISE_STRICT=1` (§6.2).
- Fixtures under `spec/tests/M10/` (§15–§16); harness notes for soft-skip (§14).

### 1.3 Explicit non-goals

| Not in M10 | Note |
|------------|------|
| Full game engine / scene editor / input loop as a product | OUT |
| DLSS / frame generation / temporal upscaling / vendor upscalers | OUT (OQ-M10-06 A) |
| Swapper-style DLL hacks / injecting into other apps | OUT |
| OptiX (NVIDIA-only) | OUT (M9 OQ-M9-02 A stands; no M10 reopen) |
| GPU raster / accelerating M3 `render()` | Still CPU-only |
| OS window / swapchain / present (under OQ-M10-01 A) | Preview via buffer / `savePNG` only |
| Changing silver-still artwork composition | Artwork locked (M8/M9) |
| Breaking `setPathBackend` / `renderPath` / M9 RT semantics | Forbidden |
| Replacing M4 goldens by default (denoise OFF) | Compat / default OFF preserves SHA on CPU |
| Editing M1–M9 milestone markdown bodies | Amendments in §19 |
| Requiring GPU denoise for Linux CI green | Soft-skip / CPU denoise path |
| Always-on denoise | Default OFF (OQ-M10-05 A) |
| Forcing 256 spp for artwork when denoise lane is used | Stills denoise lane **intends** lower spp + denoise |

### 1.4 Design constraints

- M4 CPU goldens and `path_ref.py` remain authoritative under **compat / denoise OFF**.
- M7/M8/M9 backend ids `{0,1,2}`, traps 117–119, STRICT / FORCE_FAIL, DCE spirit remain binding.
- Denoise MUST NOT change M9 STRICT RT semantics: selecting HW RT still means HW RT must succeed under STRICT, or trap 117 — denoise failure is a **separate** policy (§6).
- Hello ≤ 20 KiB; unused denoise / GPU / RT MUST NOT grow hello.
- Pixel values under compat (denoise OFF, M8 defaults, CPU) MUST remain bit-identical to M4.
- Artwork composition locked to `out/artwork_silver_still.png` / `examples/artwork_silver_still.fm`.

---

## 2. Relationship to M4, M7, M8, and M9

### 2.1 M4 integrator baseline (unchanged under compat)

From `spec/M4-ray.md` FINAL (normative reminder):

- Forward path tracer; under M8 defaults RR is OFF — paths run to `maxBounces`.
- NEE for Ambient / Point / Directional / RectAreaLight; closest-hit §10.2; shadow any-hit binary.
- Deterministic Hammersley on CPU; defaults `samples=1`, `maxBounces=4`.

### 2.2 M7 / M8 backends and speed features (unchanged)

- `PATH_BACKEND_CPU=0`, `PATH_BACKEND_VULKAN=1`; trap 118 (as amended by M9 to `{0,1,2}`).
- M8 RR / MIS / adaptive additive APIs; trap **119**; `renderPath` default unchanged.

### 2.3 M9 hardware RT (unchanged; denoise non-goal reopened)

From `spec/M9-hardware-rt.md` FINAL:

- `PATH_BACKEND_VULKAN_RT=2`; `isVulkanRayTracingAvailable()`; soft RT→compute→CPU; STRICT 117.
- Wall-clock undenoised 256 spp: ≤ **5000 ms** / ≥ **5×** (OQ-M9-08 A); measured floor `eaeb0c6` **4881 ms** / **6.01×**.
- OQ-M9-07 A denoise OUT is **superseded for M10 scope**: denoise becomes in-scope here. M9 ACs for undenoised 256-spp stills remain.

### 2.4 What M10 adds

M10 adds:

1. **Denoise mode** on the renderer (default OFF) — spatial denoise applied to the path accumulator / final LDR buffer per §5.
2. **Progressive preview** helpers (additive) so callers can request low-spp updates without breaking `renderPath` defaults (§4).

With denoise OFF and M8 defaults and backend CPU: CPU `renderPath` MUST remain **bit-identical** to M4.

---

## 3. API — denoise (OQ-M10-05 A)

`farmos:scene` MUST additionally export (extending M7/M8/M9):

```
const PATH_DENOISE_OFF: int = 0
const PATH_DENOISE_SPATIAL: int = 1

// On Renderer:
setPathDenoise(mode: int): void
getPathDenoise(): int
isPathDenoiseAvailable(): bool   // optional but RECOMMENDED; true if current mode can run
```

Names MAY be refined in implementation notes (`PATH_DENOISE_ATROUS`, etc.), but the **additive OFF-by-default** shape is binding under OQ-M10-05 A.

### 3.1 Defaults

- A newly constructed `Renderer` has denoise mode `PATH_DENOISE_OFF` (0).
- Denoise MUST NOT run until the program calls `setPathDenoise` with a non-zero mode (or an env hook documented in §6 — none required for default OFF).
- `getPathDenoise()` returns the last successfully stored mode.

### 3.2 `setPathDenoise`

1. If `mode` is not in `{0, 1}` (valid set under OQ-M10-02 A spatial-only) → trap **120** `runtime error: invalid path denoise mode` **at `setPathDenoise`** (not deferred to `renderPath`).
2. Otherwise store the mode on this renderer. Does not by itself allocate denoise buffers.
3. Switching denoise modes MUST NOT require destroying the renderer or changing `getPathBackend()`.

### 3.3 When denoise runs

On a successful `renderPath` (after path accumulation, before or as part of LDR quantize / `savePNG` export — implementation-defined order as long as fixtures see denoised pixels when mode ≠ OFF):

1. If `getPathDenoise() == PATH_DENOISE_OFF` → no denoise; image contract = M4/M7/M9 as selected by backend.
2. If `getPathDenoise() == PATH_DENOISE_SPATIAL` → apply §5 spatial denoise to the path color buffer (and optional albedo/normal guides if the implementation provides them internally).
3. Progressive preview APIs (§4) MUST honor the same denoise mode for each preview update unless the API documents a per-call override (M10 does not require per-call override).

### 3.4 Decision note (OQ-M10-05 A)

Additive `setPathDenoise` / `getPathDenoise` + `PATH_DENOISE_*`; default OFF so M4/M9 goldens unchanged. Always-on denoise (B) and `savePNG`-only flag (C) rejected for harness clarity and backend orthogonality.

### 3.5 `setPathBackend` unchanged

M10 MUST NOT change `setPathBackend` / valid set `{0,1,2}` / trap 118. Denoise is orthogonal to backend selection.

---

## 4. Progressive / interactive preview API (OQ-M10-01 A / OQ-M10-04 A)

### 4.1 Relationship to M8 adaptive

M8 already allows additive adaptive/progressive helpers without changing `renderPath` defaults. M10 **extends** that lane:

- `renderPath(scene, camera)` default behavior remains a full frame at `getSamples()` / current backend / current denoise mode.
- M10 MUST provide an additive progressive preview entry point. Recommended shape (names MAY refine):

```
// On Renderer — additive; exact names MAY refine in implementation notes:
beginPathPreview(scene, camera): void
advancePathPreview(sppStep: int): bool   // returns true if more accumulation useful / not complete
finishPathPreview(): void
getPathPreviewSpp(): int                 // spp accumulated so far in this preview session
```

Alternatively, a single `renderPathProgressive(scene, camera, sppStep: int): int` that accumulates `sppStep` samples into an internal buffer and returns total spp so far is acceptable if documented in the implementation notes and fixtures use one stable surface.

### 4.2 Semantics (assume OQ-M10-01 A — no window)

- Progressive preview MUST NOT require an OS window, swapchain, or present path.
- Callers obtain pixels via existing `savePNG` and/or a buffer readback already available to the renderer (M3/M4 pattern). M10 MAY add `copyPathPreviewRGB8(dst: …)` if needed for harness; not required if `savePNG` after each advance is sufficient for ACs.
- Camera / scene graph updates between preview sessions: if the caller mutates camera or scene after `beginPathPreview`, the implementation MUST either restart accumulation or document that results are undefined until `finishPathPreview` + new `begin`. Prefer **restart on dirty** (sound).
- Default `renderPath` MUST remain a one-shot full render (not silently progressive).

### 4.3 Interactive gate (OQ-M10-04 A — binding)

On TommyLaptop with `PATH_BACKEND_VULKAN_RT` when available (else soft-skip interactive GPU AC):

- **First useful frame:** ≤ **250 ms** median after warm-up to a preview at ≤ **8–16 spp** class (implementation chooses spp in that band).
- **Subsequent accumulation steps:** ≤ **100 ms** median per step for a documented spp step (recommend 8–16 spp/step).
- No present-window requirement under OQ-M10-01 A.

### 4.4 Decision note (OQ-M10-01 A)

Stills denoise **required** + progressive interactive API **required**, but **no** OS window/swapchain in M10. Preview via repeated path + `savePNG` / buffer readback. Stills-only (B) rejects user interactive intent; minimal present window (C) deferred.

---

## 5. Denoise algorithm class (OQ-M10-02 A)

### 5.1 Binding stack (OQ-M10-02 A)

**Farmos-owned** spatial denoise — **no** Open Image Denoise, OptiX denoiser, or NVIDIA DLL dependency required to build or run the default path.

Algorithm class for `PATH_DENOISE_SPATIAL` (MUST):

- Edge-aware spatial filter in the family of **A-trous wavelet** / **cross-bilateral** / joint bilateral on HDR (or tone-mapped) color, optionally guided by albedo and/or world/view normal buffers produced by the path tracer.
- MAY run on CPU for all backends; MAY run as a Vulkan compute pass when a GPU backend is selected. DCE: CPU-only programs that never enable denoise MUST NOT pull GPU denoise shaders.

Temporal SVGF-like (history + motion vectors) is **out of the default mode** for M10 stills under A; MAY be explored later or behind a future `PATH_DENOISE_TEMPORAL` (not required in M10 valid set `{0,1}`).

### 5.2 Rejected options (record only)

| Option | Meaning | Status |
|--------|---------|--------|
| B | Allow Open Image Denoise as an **optional** dependency (dynamic load; soft-skip if missing) | **Rejected** (OQ-M10-02 A locked) |
| C | Other (PM-defined vendor stack) | **Rejected** (OQ-M10-02 A locked) |

### 5.3 Guides

Implementations SHOULD use albedo ± normal guides when available from the path hit. If guides are unavailable (e.g. background-only), denoise MUST still be a no-crash spatial smooth / identity-safe pass.

### 5.4 Determinism

- CPU denoise under fixed mode + fixed input buffer SHOULD be deterministic across runs on the same host (bit-stable preferred for small fixtures).
- GPU denoise MAY use `# png_tol` like M7/M9 float paths.

---

## 6. Availability, STRICT, and fallback (OQ-M10-08 A)

### 6.1 M9 STRICT / RT selection unchanged

- `FARMOS_PATH_GPU_STRICT=1` + selected HW RT fail → trap **117**. Denoise MUST NOT alter this.
- Soft RT→compute→CPU chain unchanged.
- Denoise runs on whatever integrator **actually produced** the frame for that call (after soft downgrade), unless denoise itself is unavailable (§6.2).

### 6.2 Denoise unavailable (soft vs STRICT denoise)

When `getPathDenoise() != PATH_DENOISE_OFF` but the denoise implementation cannot run (OOM, shader fail, force-fail hook, etc.):

| Mode | Behavior |
|------|----------|
| Default (soft) | Leave the **noisy** path image; exit 0 if path succeeded; stored denoise mode unchanged |
| `FARMOS_PATH_DENOISE_STRICT=1` | Trap exit **120**, stderr `runtime error: path denoise failed` (same exit as invalid mode; distinct stderr REQUIRED). |

Denoise failure MUST **NOT** change M9 STRICT RT semantics (OQ-M10-08 A).

### 6.3 Test hooks

| Variable | Value `1` means |
|----------|-----------------|
| `FARMOS_PATH_DENOISE_STRICT` | Denoise failure → trap (see §6.2) |
| `FARMOS_PATH_DENOISE_FORCE_FAIL` | Treat denoise as failed for fixtures (MUST support for STRICT denoise AC) |

### 6.4 Decision note (OQ-M10-08 A)

Soft-skip denoise (leave noisy) unless denoise STRICT. Hard-fail-always (B) rejected — breaks Linux CI / hosts without GPU denoise shaders while path still works.

---

## 7. Image contract

| Lane | Contract |
|------|----------|
| Denoise OFF + CPU + M8 defaults | **Bit-identical** to M4 SHA |
| Denoise OFF + compute / HW RT | M7/M9 `# png_tol: 2` vs CPU/M4 golden |
| Denoise ON (stills) | Compare to **256-spp undenoised** reference (CPU or locked golden) with `# png_tol: 8` (OQ-M10-03 A); composition locked |
| Progressive preview frames | Smoke / tol only; not M4 SHA |

PLACEHOLDER SHA until poteto regenerates:

```
# sha256: 0000000000000000000000000000000000000000000000000000000000000000
```

---

## 8. Stills quality / speed gate (OQ-M10-03 A — binding)

### 8.1 Normative scene

- Program: `examples/artwork_silver_still.fm` (composition locked to `out/artwork_silver_still.png`).
- Resolution: **1920×1080**.
- Backend: prefer `PATH_BACKEND_VULKAN_RT` on TommyLaptop; soft-skip GPU bench on hosts without RT.

### 8.2 Gate (binding under A)

| Field | Value |
|-------|--------|
| spp | ≤ **64** with `PATH_DENOISE_SPATIAL` ON |
| Primary wall-clock | median ≤ **1500 ms** after 1 warm-up on TommyLaptop HW RT |
| Visual | `# png_tol: 8` vs **256-spp undenoised** reference / locked composition |
| Baseline to beat | M9 undenoised 256 spp floor **4881 ms** (`eaeb0c6` / OQ-M9-08 class) — speed comes from **spp↓ + denoise**, not from changing artwork |

### 8.3 Rejected options (record only)

| Option | spp | Wall-clock | Visual | Status |
|--------|-----|------------|--------|--------|
| **A (accepted)** | ≤ 64 | ≤ 1500 ms | `# png_tol: 8` vs 256-spp undenoised | **ACCEPTED** |
| B | ≤ 32 | ≤ 1000 ms | tighter / PM | Rejected |
| C | PM numbers | PM | PM | Rejected |

### 8.4 Undenoised M9 AC still stands

M10 does **not** relax M9 ≤5000 ms / ≥5× for the **256-spp denoise-OFF** lane. Stills denoise is an additional product lane.

---

## 9. Interactive scenario summary (OQ-M10-04 A)

| Metric | Gate (A) |
|--------|----------|
| First useful preview | ≤ **250 ms** median (≤8–16 spp class) TommyLaptop HW RT after warm-up |
| Subsequent step | ≤ **100 ms** median |
| Window / swapchain | **Not required** (OQ-M10-01 A) |
| Soft-skip | Hosts without HW RT: soft-skip interactive GPU AC; CPU progressive MAY still be tested for API correctness only |

Option B (present window ≤60 ms) and C (other) are **rejected** (OQ-M10-04 A locked).

---

## 10. Size / DCE (OQ-M10-07 A)

| Binary / linkage | Budget |
|------------------|--------|
| hello (no path / GPU / denoise) | ≤ **20 KiB** stripped; no Vulkan / RT / denoise symbols when unused |
| CPU glass+mirror | ≤ **300 KiB** (unchanged) |
| Compute-linked GPU path | ≤ **400 KiB** (unchanged) |
| RT-linked (M9) | ≤ **500 KiB** (unchanged) |
| Denoise-linked demo (path + denoise; GPU optional) | ≤ **600 KiB** stripped (OQ-M10-07 A) |

Option B (keep 500 total including denoise) is tighter — **rejected** (OQ-M10-07 A locked).

DCE: denoise code MUST be reachable only from path / denoise units so hello DCE still holds.

---

## 11. Runtime traps

| Exit | Stderr | When |
|------|--------|------|
| **117** | `runtime error: vulkan path failed` | M7/M9 STRICT GPU/RT failure (unchanged) |
| **118** | `runtime error: invalid path backend` | `setPathBackend` ∉ `{0,1,2}` (unchanged) |
| **119** | `runtime error: invalid path sampling mode` | M8 (unchanged) |
| **120** | `runtime error: invalid path denoise mode` | `setPathDenoise` with unsupported mode |
| **120** | `runtime error: path denoise failed` | `FARMOS_PATH_DENOISE_STRICT=1` + denoise fail — same exit, distinct stderr REQUIRED |

Existing M4 traps unchanged. Exit **109** still unused.

---

## 12. Regression

- All M4 / M7 / M8 / M9 fixtures that leave denoise at default OFF MUST continue to pass under M10.
- M9 wall-clock / size / STRICT ACs remain for the undenoised lane.
- New M10 fixtures MUST soft-skip appropriately when GPU/RT/denoise-GPU unavailable.

---

## 13. Acceptance criteria (binding under OQ A)

| ID | Criterion |
|----|-----------|
| AC-M10-01 | Denoise OFF + CPU compat: M4 twin background remains **bit-identical** SHA. |
| AC-M10-02 | `setPathDenoise` / `getPathDenoise` round-trip; default OFF; `PATH_DENOISE_SPATIAL=1` accepted. |
| AC-M10-03 | `setPathDenoise(<invalid>)` → trap **120** at `setPathDenoise`. |
| AC-M10-04 | Stills denoise smoke: small scene, denoise ON, exit 0; GPU soft-skip OK. |
| AC-M10-05 | Stills gate (OQ-M10-03 A): silver still 1920×1080 ≤64 spp + denoise; median ≤ **1500 ms** TommyLaptop HW RT; visual `# png_tol: 8` vs 256-spp undenoised reference. |
| AC-M10-06 | Progressive API exists; first useful ≤ **250 ms**; step ≤ **100 ms** (OQ-M10-04 A); no window required. |
| AC-M10-07 | M9 STRICT/fallback unchanged when denoise ON or OFF; denoise fail does not alter RT STRICT. |
| AC-M10-08 | Soft denoise unavailable → noisy image; `FARMOS_PATH_DENOISE_STRICT=1` → trap (stderr denoise failed). |
| AC-M10-09 | Hello ≤ 20 KiB unused; glass+mirror ≤ 300; compute ≤ 400; RT ≤ 500; denoise-linked ≤ **600 KiB**. |
| AC-M10-10 | No OIDN/NVIDIA DLL required (OQ-M10-02 A); no DLSS/Swapper (OQ-M10-06 A); no OptiX; no GPU raster; artwork composition locked. |
| AC-M10-11 | `setPathBackend` / M9 backends unchanged; trap 118 unchanged. |
| AC-M10-12 | Fixtures in §16 pass under their kinds; M4–M9 regression under denoise OFF. |
| AC-M10-13 | Linux CI green: soft-skip GPU/RT/denoise-GPU fixtures; CPU denoise OFF / API fixtures required. |

AC-M10-05, AC-M10-06, AC-M10-09 are eggtooth / script / bench checks.

---

## 14. Test harness

Reuse M4 / M7 / M8 / M9 kinds: `run`, `run_png`, `runtime_trap`.

| Header | Meaning |
|--------|---------|
| `# requires_vulkan: true` | Soft-skip without Vulkan compute (M7) |
| `# requires_vulkan_rt: true` | Soft-skip without HW RT (M9) |
| `# requires_denoise: true` | Soft-skip when denoise implementation unavailable (optional header; CPU spatial SHOULD usually run) |
| `# png_tol: <int>` | Per-channel byte tolerance vs `# sha256:` |
| `# env: KEY=VALUE` | Child env (M7/M8/M9 vars + `FARMOS_PATH_DENOISE_STRICT`, `FARMOS_PATH_DENOISE_FORCE_FAIL`) |
| `# path_mode: compat` | Documentation aid |

Soft-skip rules: same spirit as M9 — do not fail; log skip; runner exit 0 for that case.

Working directory for M10 fixtures: `spec/tests/M10`.

---

## 15. Fixture outline

| Fixture | Kind | What |
|---------|------|------|
| 001_denoise_off_cpu_twin | run_png | Denoise OFF; M4-001 twin SHA |
| 002_denoise_mode_roundtrip | run | `setPathDenoise` / `getPathDenoise`; print mode |
| 003_trap_invalid_denoise | runtime_trap | `setPathDenoise(99)` → 120 |
| 004_stills_denoise_smoke | run_png | Small scene; `PATH_DENOISE_SPATIAL`; exit 0; tol/smoke |
| bench / artwork | script | silver still ≤64 spp + denoise wall-clock; progressive timing |

**Counts (stub pack):** 4 `.fm` harness fixtures (001–004) + README + `_ref` + bench note. Expand as PM requests.

---

## 16. Test index (source files)

| # | File | Kind |
|---|------|------|
| 001 | `001_denoise_off_cpu_twin.fm` | run_png |
| 002 | `002_denoise_mode_roundtrip.fm` | run |
| 003 | `003_trap_invalid_denoise.fm` | runtime_trap |
| 004 | `004_stills_denoise_smoke.fm` | run_png |

Eggtooth / bench rows (no required `.fm` in stub pack): size/DCE (AC-M10-09), silver-still denoise bench (AC-M10-05), progressive timing (AC-M10-06).

---

## 17. Open questions — CLOSED

Status: **FINAL**. Open questions: **none.**

PM lock **2026-10-18** (user 2026-10-18 all-A): **OQ-M10-01 .. OQ-M10-08** all **accepted option A**. Rejected options remain below for audit trail only.

### OQ-M10-01 — Scope split — **ACCEPTED A**

| Option | Meaning | Status |
|--------|---------|--------|
| **A** | Stills denoise **required** + progressive interactive API **required**; **no** OS window/swapchain in M10 (preview via repeated path + `savePNG` / buffer readback) | **ACCEPTED** |
| B | Stills denoise only (defer progressive) | Rejected |
| C | Stills + minimal present window / swapchain | Rejected |

### OQ-M10-02 — Denoise stack — **ACCEPTED A**

| Option | Meaning | Status |
|--------|---------|--------|
| **A** | Farmos-owned Vulkan/CPU spatial denoise (A-trous / bilateral class); **no** OIDN / NVIDIA DLL dependency | **ACCEPTED** |
| B | Allow Open Image Denoise as optional dependency | Rejected |
| C | Other (PM-defined) | Rejected |

### OQ-M10-03 — Stills quality/speed gate — **ACCEPTED A**

| Option | Meaning | Status |
|--------|---------|--------|
| **A** | Silver still 1920×1080: ≤ **64 spp** + denoise ON; median ≤ **1500 ms** TommyLaptop HW RT after warm-up; visual `# png_tol: 8` vs 256-spp undenoised reference / locked composition | **ACCEPTED** |
| B | ≤ 32 spp / ≤ 1000 ms | Rejected |
| C | PM numbers | Rejected |

### OQ-M10-04 — Interactive gate — **ACCEPTED A**

| Option | Meaning | Status |
|--------|---------|--------|
| **A** | Progressive preview: first useful frame ≤ **250 ms** (≤8–16 spp class) on TommyLaptop HW RT; subsequent steps ≤ **100 ms** median; no window (OQ-01 A) | **ACCEPTED** |
| B | Requires present window ≤ 60 ms | Rejected |
| C | Other | Rejected |

### OQ-M10-05 — API shape — **ACCEPTED A**

| Option | Meaning | Status |
|--------|---------|--------|
| **A** | Additive `setPathDenoise` / `getPathDenoise` + `PATH_DENOISE_*`; default **OFF** | **ACCEPTED** |
| B | Denoise always-on for path | Rejected |
| C | Denoise only via `savePNG` flag | Rejected |

### OQ-M10-06 — DLSS / vendor upscalers — **ACCEPTED A**

| Option | Meaning | Status |
|--------|---------|--------|
| **A** | Still **OUT** | **ACCEPTED** |
| B | Allow as optional Windows path | Rejected |

### OQ-M10-07 — Size — **ACCEPTED A**

| Option | Meaning | Status |
|--------|---------|--------|
| **A** | Denoise-linked demo ≤ **600 KiB** stripped; hello ≤20; RT ≤500; compute ≤400; glass+mirror ≤300 unchanged | **ACCEPTED** |
| B | Keep **500** KiB total including denoise | Rejected |
| C | Other | Rejected |

### OQ-M10-08 — STRICT / denoise fallback — **ACCEPTED A**

| Option | Meaning | Status |
|--------|---------|--------|
| **A** | Denoise failure MUST NOT change M9 STRICT RT semantics; if denoise unavailable, soft-skip denoise (leave noisy) unless `FARMOS_PATH_DENOISE_STRICT=1` → trap (120-family) | **ACCEPTED** |
| B | Denoise fail = hard fail always | Rejected |

---

## 18. Accepted decisions (FINAL — binding)

PM lock 2026-10-18 (user 2026-10-18 all-A). All rows below are **binding**:

| ID | Decision |
|----|----------|
| OQ-M10-01 | **A** — stills denoise + progressive API; no OS window/swapchain |
| OQ-M10-02 | **A** — Farmos-owned spatial (A-trous/bilateral); no OIDN/NVIDIA DLL |
| OQ-M10-03 | **A** — ≤64 spp + denoise; ≤1500 ms; `# png_tol: 8` vs 256-spp undenoised |
| OQ-M10-04 | **A** — first useful ≤250 ms (≤8–16 spp); step ≤100 ms; no window |
| OQ-M10-05 | **A** — additive `setPathDenoise` / `getPathDenoise` + `PATH_DENOISE_*`; default OFF |
| OQ-M10-06 | **A** — DLSS/vendor upscalers OUT |
| OQ-M10-07 | **A** — denoise-linked ≤600 KiB; hello ≤20; RT ≤500; compute ≤400; glass+mirror ≤300 |
| OQ-M10-08 | **A** — denoise soft-fail (leave noisy); `FARMOS_PATH_DENOISE_STRICT=1` → trap 120-family; M9 RT STRICT unchanged |
| RD-M10-01 | M9 denoise non-goal (OQ-M9-07 A) **reopened** as in-scope for M10 |
| RD-M10-02 | Artwork composition locked; undenoised M9 256-spp AC still stands |
| RD-M10-03 | Cite M9 floor **4881 ms** as baseline stills denoise must beat via spp↓+denoise |
| RD-M10-04 | Trap **120** for invalid denoise mode; denoise-fail under STRICT uses exit 120 + distinct stderr |

---

## 19. Amendments relative to M4 / M7 / M8 / M9

M4–M9 milestone markdown bodies are not edited.

| # | Amendment |
|---|-----------|
| 1 | Reopen M9 denoise-as-product non-goal (OQ-M9-07 A) as **in scope** for M10 |
| 2 | `farmos:scene` exports `PATH_DENOISE_OFF`, `PATH_DENOISE_SPATIAL`, `setPathDenoise`, `getPathDenoise` (and recommended `isPathDenoiseAvailable`) |
| 3 | Additive progressive preview API (§4); `renderPath` default unchanged |
| 4 | Trap **120** invalid denoise mode; denoise-fail stderr under DENOISE_STRICT |
| 5 | Env `FARMOS_PATH_DENOISE_STRICT`, `FARMOS_PATH_DENOISE_FORCE_FAIL` |
| 6 | Stills wall-clock / visual gate §8 (binding OQ-M10-03 A) |
| 7 | Interactive timing gate §9 (binding OQ-M10-04 A) |
| 8 | Denoise-linked size ≤ 600 KiB (binding OQ-M10-07 A) |
| 9 | Fixtures under `spec/tests/M10/` |
| 10 | DLSS / Swapper / OptiX / GPU raster / artwork variants remain out (OQ-M10-06 A) |
| 11 | M9 backends / STRICT RT / undenoised 256-spp ACs unchanged |

---

## Appendix A — Compat denoise-off twin (001)

Same shape as `spec/tests/M4/001_background_only.fm` / M7-001 / M8-001 / M9-001 (16×16, clear `0x4488ff`). Leave backend CPU; denoise OFF; RR / sampling / adaptive at defaults.

## Appendix B — Invalid denoise trap (003)

```
renderer.setPathDenoise(99); // trap 120
```

## Appendix C — Stills denoise smoke (004)

Small resolution; `setPathDenoise(PATH_DENOISE_SPATIAL)`; `renderPath`; `savePNG`. Soft-skip GPU headers only if denoise requires GPU (under A, CPU spatial SHOULD run without `# requires_vulkan`).

## Appendix D — Artwork stills denoise bench note

Use `examples/artwork_silver_still.fm` @ 1920×1080 with ≤ **64 spp**, `PATH_DENOISE_SPATIAL`, prefer `PATH_BACKEND_VULKAN_RT`. Do not alter composition. Compare visually to 256-spp undenoised reference. Primary gate ≤ **1500 ms** after warm-up (OQ-M10-03 A). Cite M9 floor **4881 ms** as the undenoised baseline being beaten via spp↓+denoise.

---

## Document history

- 2026-10-18: DRAFT. Denoise-interactive after M9 (`52b1aebc`). Reopens denoise (was OUT in M9 OQ-M9-07 A). Product split: stills denoise + progressive preview; no window under recommend A. OQ-M10-01..08 recommend A. Stills ≤64 spp + denoise ≤1500 ms; interactive first ≤250 ms / step ≤100 ms. Farmos-owned spatial; DLSS OUT; denoise-linked ≤600 KiB; trap 120. Fixtures under `spec/tests/M10/`. Status DRAFT until PM locks OQs.
- 2026-10-18: **FINAL**. PM lock OQ-M10-01..08 all option A (user 2026-10-18 all-A). Open questions closed. Binding gates: ≤64 spp + denoise ≤1500 ms; first useful ≤250 ms / step ≤100 ms; no window; Farmos-owned spatial; default OFF; DLSS OUT; denoise-linked ≤600 KiB; soft denoise fail / STRICT trap 120-family; M9 RT STRICT unchanged.
