# Farmos M12 — Native Vulkan Upscale (XeSS / FSR)

**Status:** **FINAL** (PM/user 2026-10-21: OQ-M12-01 .. OQ-M12-09 all A)  
**Milestone:** M12 — opt-in native Vulkan temporal upscale for the M4 path tracer  
**Depends on:** `spec/M1-core.md` .. `spec/M11-glossy-continuation.md` (all FINAL)  
**Normative keywords:** MUST / SHOULD / MAY per RFC 2119.  
**Date:** 2026-10-21 (FINAL)

This document specifies Farmos M12: an **opt-in** native **Vulkan** upscale path (primary **Intel XeSS**; optional **AMD FSR2**) that reconstructs a higher **display** resolution from a lower **render** resolution path-trace accumulator, without DLSS/NGX, without DLSS-Enabler / Swapper / OptiScaler injection, and without rewriting M4/M10/M11 goldens when upscale is **OFF**. It does **not** specify compiler, runtime, or stdlib implementation code. It does **not** edit M1–M11 milestone markdown bodies; normative amendments relative to those milestones live in §19 of this file.

Open questions: **none**. OQ-M12-01 .. OQ-M12-09 **accepted A** (PM lock 2026-10-21; user 2026-10-21 lock-all-oq-a). Binding: headless upscale-to-buffer + `savePNG` (no OS window; M10 OQ-M10-01 **stands**); **XeSS primary** + **FSR2 optional**; camera MV + depth (object MV deferred); additive `setPathUpscale` + `PATH_UPSCALE_OFF` / `XESS` / `FSR`; default **OFF**; trap **122**; **denoise then upscale**; Vulkan compute + Vulkan RT MUST; CPU soft / STRICT 122-family; upscale-glue PE ≤ **700 KiB** (vendor DLLs external); Arc smoke + Linux soft-skip; frame-gen **OUT**. Selective reopen of M10 OQ-M10-06: native XeSS/FSR **IN**; DLSS/Enabler/Swapper/OptiScaler **still OUT**. Body gates / ACs are **binding** under A.

Related: `/workspace/farmos/PLAN.md` (M12 stub), `spec/M10-denoise-interactive.md` (OQ-M10-01 no-window; OQ-M10-06 vendor upscalers OUT — **reopened selectively** here), `spec/M11-glossy-continuation.md` (opt-in / §19 reopen pattern), `spec/M7-vulkan-path.md`, `spec/M9-hardware-rt.md`, `spec/tests/M12/README.md`.

**Binding product intent (PM / user GO draft-native-upscale; lock 2026-10-21 all-A):**

1. User wanted DLSS-Enabler for Intel/AMD; survey verdict: **wrong tool** (no NGX/DLSS in Farmos; offline path-trace + `PATH_DENOISE_SPATIAL`; no swapchain under M10). User chose **draft native XeSS/FSR milestone** instead.
2. **Skip forever for this milestone:** DLSS-Enabler, Swapper, OptiScaler, and any DLL-injection / foreign-process upscaler.
3. Master tip context ~`4a661e59` (M11). Target machine: **TommyLaptop Intel Arc** + Vulkan RT (HW RT path). Prefer **XeSS** as primary; FSR2/3 as alt OQ — do **not** require NVIDIA DLSS/NGX.
4. M10 FINAL locked: OQ-M10-01 A = **no** OS window/swapchain; OQ-M10-06 A = DLSS/vendor upscalers **OUT**. M12 MUST **explicitly reopen** those non-goals where needed (same pattern as M10 vs M9 denoise / M11 vs M4 glossy).
5. Opt-in API; default **OFF**; keep M4/M10/M11 goldens and denoise behavior when OFF.
6. Non-goals: frame-gen (DLSS-G / FSR-FG) unless a separate future OQ; full game engine; rewriting goldens under OFF.

**Target platform:** Windows x64 primary (TommyLaptop Arc) for XeSS smoke + timing; Linux CI soft-skip / skip when SDK or Vulkan device absent — CPU OFF-path fixtures still required.

---

## 1. Scope and non-goals

### 1.1 Product split (OQ-M12-01..09 A binding)

M12 MUST deliver:

| Piece | Requirement under A |
|-------|-------------------------------|
| Present / output path | **Headless** upscale-to-buffer + `savePNG` / readback — **no** OS window / swapchain required in M12 (OQ-M12-01 A). Aligns with M10 stills / progressive buffer path. |
| Upscaler stack | Native **Intel XeSS** (Vulkan) primary; **AMD FSR2** optional second mode (OQ-M12-02 A). |
| Contracts | Documented render-res vs display-res; Halton (or SDK-required) jitter; camera motion vectors + depth; reactive mask if the chosen SDK requires it (OQ-M12-03 A). |
| API | Additive `setPathUpscale` / `getPathUpscale` + `PATH_UPSCALE_*`; default **OFF**; trap **122** (OQ-M12-04 A). |
| Denoise relation | When both ON: **denoise then upscale** (OQ-M12-05 A); not mutually exclusive. |
| Backends | Vulkan compute + Vulkan RT MUST run upscale when mode ≠ OFF and SDK available; CPU leaves buffer at render-res / soft-fails upscale unless STRICT (OQ-M12-06 A). |
| Size | Upscale-glue-linked soft budget ≤ **700 KiB**; prior ≤300/400/500/600 unchanged; DCE when OFF; vendor SDK DLLs external (OQ-M12-07 A). |
| AC | TommyLaptop Arc smoke: quality + timing vs native 1080p; Linux soft-skip if SDK absent (OQ-M12-08 A). |

### 1.2 Explicit non-goals (OUT)

| Item | Note |
|------|------|
| DLSS / NGX / NVIDIA-only upscalers | **OUT** — do not require NVIDIA hardware or NGX |
| DLSS-Enabler / Swapper / OptiScaler / any DLL injection into foreign processes | **OUT forever for this milestone** |
| Frame generation (DLSS-G / FSR-FG / XeSS-FG) | **OUT** of M12 (OQ-M12-09 A); separate future OQ if ever needed |
| Full game engine / input loop / scene editor | **OUT** |
| Rewriting M4 / M10 / M11 goldens under upscale OFF | **OUT** |
| Always-on upscale | Default **OFF** |
| OptiX | **OUT** (M9 stands) |
| GPU raster / accelerating M3 `render()` | Still CPU-only |
| Changing silver-still artwork composition | Artwork locked |
| Editing M1–M11 milestone markdown bodies | Amendments in §19 only |
| Requiring upscale for Linux CI green | Soft-skip / skip when SDK absent |
| Requiring an OS window under A | Window remains **OUT** (OQ-M12-01 A); M10 OQ-M10-01 stands |

### 1.3 Design constraints

- M4 CPU goldens and `path_ref.py` remain authoritative when upscale is **OFF** (and denoise OFF, glossy OFF, M8 defaults, CPU).
- M7/M8/M9 fallback / STRICT / FORCE_FAIL / traps **117–119**, M10 denoise **120**, M11 glossy **121** remain binding.
- Hello ≤ 20 KiB; unused path/GPU/upscale MUST NOT grow hello (M6/M7 spirit).
- Prior size caps: glass+mirror ≤ 300 KiB; compute-linked ≤ 400; RT-linked ≤ 500; denoise-linked ≤ 600 — M12 MUST NOT blow them when upscale is OFF / unused.
- Pixel values under OFF MUST NOT depend on thread scheduling (M4 determinism) on CPU.
- ON mode MAY use `# png_tol` / separate goldens / ROI metrics; MUST document scale factor and SDK mode.
- **Do not** ship Enabler/Swapper as a “workaround” if native SDK integration is blocked — report blocker to PM instead.

### 1.4 Why not DLSS-Enabler (normative product rationale)

Farmos path tracing today is:

- Offline / progressive buffer + `savePNG` (M10: **no** swapchain).
- Farmos-owned spatial denoise (`PATH_DENOISE_SPATIAL`), not NVIDIA OIDN/DLL.
- No NGX / DLSS feature use in the engine.

DLSS-Enabler / Swapper assume a **game’s existing DLSS/NGX** present path. That does not map onto Farmos. M12 therefore integrates **native** vendor Vulkan upscalers (XeSS / FSR) that Farmos owns and calls — not injectors.

---

## 2. Relationship to prior milestones — what M12 reopens

### 2.1 M10 OQ-M10-06 A (vendor upscalers OUT) — **REOPEN (selective)**

M10 OQ-M10-06 A locked: DLSS / vendor upscalers **OUT**.

**M12 reopens** vendor **temporal upscalers** as **in scope** for:

| In scope (M12) | Still OUT |
|----------------|-----------|
| Native **Intel XeSS** (Vulkan API) | DLSS / NGX |
| Native **AMD FSR2** (Vulkan API), if OQ-M12-02 includes it | DLSS-Enabler |
| Farmos-owned glue calling those SDKs | Swapper / OptiScaler / DLL injection |
| | Frame-gen (OQ-M12-09 A) |

M10’s rejection of **DLSS** and **injection hacks** **stands**. M12 does **not** reopen OptiX or GPU raster.

### 2.2 M10 OQ-M10-01 A (no OS window / swapchain) — **stands under A**

M10 OQ-M10-01 A locked: stills denoise + progressive API; **no** OS window/swapchain.

Under **OQ-M12-01 A** (headless upscale-to-buffer + `savePNG`):

- M10’s **no-window** product rule **remains in force** for M12.
- M12 does **not** reopen a present window under A.
- Upscale consumes / produces **Vulkan images / host buffers** already used by path + denoise + `savePNG`.

OQ-M12-01 B (minimal window + swapchain) was **rejected**. Body and ACs MUST NOT require a window. A future milestone may reopen present if PM opens a new OQ.

**Honest note:** Some XeSS/FSR sample apps present via swapchain. That is a **demo convention**, not a hard SDK requirement for evaluating an upscaled color image. Farmos MAY feed color / depth / motion-vector / exposure images created by the path tracer without presenting. If poteto discovers a hard SDK dependency on a swapchain `VkImage` usage flag that cannot be satisfied headless, report to PM — do not silently adopt Enabler.

### 2.3 M9 / M10 / M11 orthogonality (unchanged)

- Backends `0` / `1` / `2`; soft RT→compute→CPU; STRICT 117; trap 118.
- M10 denoise / progressive remain independent flags.
- M11 glossy continuation remains independent.
- M8 RR / MIS / adaptive remain independent.
- Artwork composition locked (`examples/artwork_silver_still.fm` / `out/artwork_silver_still.png`).

### 2.4 What M12 adds

| Layer | Addition |
|-------|----------|
| API | `PATH_UPSCALE_OFF` / `PATH_UPSCALE_XESS` / (optional) `PATH_UPSCALE_FSR`; `setPathUpscale` / `getPathUpscale`; recommended `isPathUpscaleAvailable` |
| Pipeline | Render at `renderW×renderH`; upscale to `displayW×displayH`; export display buffer |
| Fixtures | `spec/tests/M12/` OFF twin + roundtrip + trap + upscale smoke |
| Contracts | Jitter, camera MV, depth, scale presets (§5) |

---

## 3. API (OQ-M12-04 A — binding)

`farmos:scene` / `Renderer` MUST additionally export:

```
const PATH_UPSCALE_OFF: int = 0
const PATH_UPSCALE_XESS: int = 1
const PATH_UPSCALE_FSR: int = 2   // present if OQ-M12-02 A (XeSS primary + FSR optional)

setPathUpscale(mode: int): void
getPathUpscale(): int
isPathUpscaleAvailable(mode: int): bool   // RECOMMENDED
setPathUpscaleResolution(renderW: int, renderH: int, displayW: int, displayH: int): void
getPathUpscaleRenderWidth(): int
getPathUpscaleRenderHeight(): int
getPathUpscaleDisplayWidth(): int
getPathUpscaleDisplayHeight(): int
```

Exact helper names for resolution MAY be refined in implementation notes, but the **additive OFF-by-default** `setPathUpscale` / `PATH_UPSCALE_*` shape is **binding** under OQ-M12-04 A.

### 3.1 Defaults

- Newly constructed `Renderer`: `PATH_UPSCALE_OFF` (0).
- Upscale MUST NOT run until the program sets a non-zero mode.
- When mode is OFF, `renderPath` / progressive / `savePNG` behave as M11 (display size = path buffer size as today).
- No env var is **required** to force ON for product defaults. A test-only env MAY exist (`FARMOS_PATH_UPSCALE=1` → XeSS if available); method call wins if both set.

### 3.2 `setPathUpscale`

1. If `mode` is not in the supported set under OQ-M12-02 → trap **122** `runtime error: invalid path upscale mode` **at the setter** (not deferred to `renderPath`).
2. Otherwise store mode for subsequent `renderPath` / progressive steps / export on this renderer.
3. MUST NOT by itself clear the path accumulator or change backend / denoise / glossy / sampling / roulette state.
4. Switching modes MUST NOT require destroying the renderer.

### 3.3 Resolution helpers

- `setPathUpscaleResolution(renderW, renderH, displayW, displayH)`:
  - All four MUST be ≥ 1.
  - `displayW` MUST be ≥ `renderW` and `displayH` ≥ `renderH` (upscale, not downscale) — else trap **122**-family with distinct stderr `runtime error: invalid path upscale resolution` (implementation MAY reuse exit **122**).
  - When upscale mode is OFF, helpers MAY be ignored for export size (path uses existing renderer size API).
- When mode ≠ OFF and resolution was never set: implementation MUST either (a) default to a documented Quality-class scale (recommend **2×**: render = half linear of current renderer size, display = current size) or (b) soft-fail upscale and leave render-res output unless STRICT — document choice in code notes; fixtures MUST set resolution explicitly.

### 3.4 Orthogonality

| Other API | Interaction |
|-----------|-------------|
| `setPathBackend` | Independent; upscale runs after path (+ optional denoise) on GPU backends when available |
| `setPathDenoise` | Independent; order under OQ-M12-05 A (binding): **denoise → upscale** |
| `setPathGlossyContinuation` | Independent |
| `setPathSampling` / roulette / adaptive | Independent; jitter for upscale is **additional** camera sub-pixel offset (§5) |
| Progressive M10 API | Upscale MAY run per progressive step when mode ≠ OFF (costly); programs MAY upscale only on export — document; fixtures define expected behavior |

---

## 4. Present / output path (OQ-M12-01 A — binding)

### 4.1 Binding under A — headless

Under **OQ-M12-01 A**:

- M12 MUST produce the upscaled image in a **GPU or host buffer**.
- Programs MUST be able to `savePNG` the **display**-resolution image without creating an OS window, swapchain, or present queue.
- Progressive preview remains buffer / `savePNG` based (M10 OQ-M10-01 A stands).

### 4.2 Rejected / alternate options

| Option | Meaning |
|--------|---------|
| **A (ACCEPTED)** | Headless upscale-to-buffer + `savePNG` / readback; **no** window required |
| B | Minimal OS window + Vulkan swapchain/present for interactive display of upscaled frames |
| C | Both required in M12 |

**Rationale for A:** Reopening a present window is a large product change vs M10’s locked no-window stills path. XeSS/FSR evaluate color(+aux) images; ACs for quality and timing can be measured from saved PNGs / buffer hashes on TommyLaptop without a window. B remains available if PM wants interactive present later.

---

## 5. Upscale contracts (OQ-M12-03 A — binding)

### 5.1 Resolutions

- **Render resolution** (`renderW×renderH`): resolution at which the path tracer accumulates samples.
- **Display resolution** (`displayW×displayH`): resolution of the upscaled output exported by `savePNG` / readback when mode ≠ OFF.

Smoke preset (AC): render **960×540** → display **1920×1080** (2× linear / Quality-class). Other SDK quality presets (Ultra Quality / Balanced / Performance) MAY be exposed later; M12 does not require a full preset enum.

### 5.2 Jitter

When upscale mode ≠ OFF, the path tracer MUST apply a **per-frame (or per progressive step) sub-pixel jitter** to the camera ray generation matching the sequence expected by the active SDK (XeSS / FSR Halton or vendor-documented sequence).

- Jitter MUST be disabled (identity) when mode is OFF so M4 CPU SHA remains intact.
- Programs MUST NOT be required to call a separate jitter API for the default path; Farmos owns jitter when upscale is ON.

### 5.3 Motion vectors (OQ-M12-03 A binding)

Under **OQ-M12-03 A**:

| Buffer | Requirement |
|--------|-------------|
| Camera motion vectors | **MUST** — derived from current vs previous camera matrices / projection for static scenes |
| Object / skinned motion vectors | **MAY** defer — static silver-still AC does not require deforming meshes; document as OUT or soft-zero for M12 if not implemented |
| Depth | **MUST** — linear or device depth as required by SDK; document format |
| Reactive / transparency mask | **SHOULD** if XeSS/FSR quality depends on it; MAY be all-zero for opaque silver-still smoke |
| Exposure / history reset | **SHOULD** reset history on camera cut / resolution change / first frame |

Static artwork silver still: camera orbit benches MAY exist; stills AC MAY use a fixed camera with valid identity/near-zero MV plus correct depth + jitter history over ≥ **N** warmup frames (recommend **N ≥ 8** before measuring quality).

### 5.4 Color space

Path HDR accumulator → tonemap / LDR as today → upscale input format per SDK (typically linear or sRGB — **MUST** document). Denoise (when ON) runs **before** upscale on the render-res buffer (OQ-M12-05 A).

---

## 6. Upscaler stack (OQ-M12-02 A — binding)

### 6.1 Binding under A

| Mode | Constant | Stack |
|------|----------|-------|
| 0 | `PATH_UPSCALE_OFF` | No upscale |
| 1 | `PATH_UPSCALE_XESS` | Native **Intel XeSS** Vulkan API (primary on Arc) |
| 2 | `PATH_UPSCALE_FSR` | Native **AMD FSR2** Vulkan API (optional / alt) |

- Primary validation machine: TommyLaptop **Intel Arc** → XeSS is the **primary** PASS path for AC-M12 smoke.
- FSR2 MUST be selectable when built/linked and the device can run it; if unavailable, `isPathUpscaleAvailable(PATH_UPSCALE_FSR)` is false; selecting it soft-fails unless STRICT (§7).
- FSR3 frame-gen is **OUT** (OQ-M12-09). FSR2 upscale-only is the FSR scope for M12.

### 6.2 Explicitly OUT

- DLSS / NGX
- OptiX denoiser-as-upscaler
- DLSS-Enabler
- Swapper
- OptiScaler
- Any process-injection or “replace DXGI swapchain” hack

### 6.3 Licensing / redistribution (blocker note for poteto)

Poteto MUST use the vendor’s official SDK redistribution rules (XeSS / FSR license files in tree or fetch script). Do not vendor proprietary blobs contrary to license. Missing SDK on a machine → soft-skip fixtures (§14), not a silent Enabler fallback.

---

## 7. Availability, STRICT, backends (OQ-M12-06 A — binding)

### 7.1 Backend participation

| Backend | Upscale ON behavior under A |
|---------|----------------------------------------|
| `PATH_BACKEND_CPU` (0) | Upscale **not** required to run. Soft: leave render-res (or nearest) output; log once. STRICT: trap **122**-family `path upscale failed` if mode ≠ OFF. |
| `PATH_BACKEND_VULKAN` (1) | MUST run XeSS/FSR when mode available on device; else soft / STRICT as below. |
| `PATH_BACKEND_VULKAN_RT` (2) | Same as compute — upscale is a **post** path on the color(+aux) images; RT vs compute selection unchanged. |

### 7.2 Soft-fail vs STRICT

Default (no STRICT):

- If selected upscale mode is unavailable (no SDK, no device support, init fail): leave **noisy/denoised render-res** image; do **not** change M9 STRICT RT semantics; do **not** trap.

`FARMOS_PATH_UPSCALE_STRICT=1`:

- Upscale failure → trap exit **122** with stderr containing `path upscale failed` (distinct from invalid-mode message).

Recommended test hook: `FARMOS_PATH_UPSCALE_FORCE_FAIL=1` treats upscale as failed (mirrors denoise FORCE_FAIL).

M7/M9 `FARMOS_PATH_GPU_STRICT` / RT STRICT **unchanged** — selecting HW RT still requires RT success under that STRICT, independent of upscale.

### 7.3 Trap family

| Code | When |
|------|------|
| **122** | Invalid `setPathUpscale` mode; invalid upscale resolution (MAY share code); STRICT upscale failure (distinct stderr) |

Do **not** collide with 117–121.

---

## 8. Relation to denoise (OQ-M12-05 A — binding)

Under **OQ-M12-05 A**:

1. If denoise ON and upscale ON: apply **spatial denoise at render resolution**, then **upscale** to display resolution.
2. Modes are **not** mutually exclusive.
3. Upscale MUST NOT replace M10 denoise; denoise MUST NOT be required to pass upscale OFF ACs.
4. Alternative orders (upscale then denoise; mutually exclusive) are **rejected** under A — denoise algorithms in M10 are tuned for path noise at sample grid; upscaling first would amplify residual noise incorrectly for A-trous/bilateral class filters.

---

## 9. Size / DCE (OQ-M12-07 A — binding)

| Binary class | Cap |
|--------------|-----|
| hello (no path/GPU/upscale use) | ≤ **20 KiB** stripped |
| CPU glass+mirror | ≤ **300 KiB** |
| Compute-linked | ≤ **400 KiB** |
| RT-linked | ≤ **500 KiB** |
| Denoise-linked | ≤ **600 KiB** |
| Upscale-glue-linked demo (path + optional denoise + upscale glue) | ≤ **700 KiB** stripped **Farmos PE** (OQ-M12-07 A) |

- Vendor SDK **DLLs / shared objects** loaded at runtime MUST **not** count toward the Farmos PE KiB cap (document shipped side-by-side files separately).
- When upscale APIs are unused: DCE MUST strip upscale glue and MUST NOT pull XeSS/FSR symbols into hello.
- Option B (keep ≤600 including upscale glue) **rejected**.
- Option C (≤800) **rejected** for M12; PM may open a follow-up OQ if eggtooth measures forced overrun after XeSS glue.

---

## 10. Acceptance criteria (OQ-M12-08 A — binding)

Binding under A. Exact ms / tol floors beyond the SHOULD ≥ **1.5×** report and `# png_tol` start **12** remain measurable after first Arc tip; eggtooth / PM MAY open a follow-up OQ to relax (M9-08 / M10-09 pattern) without reopening other OQs.

| ID | Criterion |
|----|-----------|
| AC-M12-01 | Upscale OFF + CPU → M4 twin SHA / existing OFF goldens preserved (001). |
| AC-M12-02 | `setPathUpscale` / `getPathUpscale` roundtrip; default 0. |
| AC-M12-03 | `setPathUpscale(99)` → trap **122**. |
| AC-M12-04 | Smoke: mode XeSS (or FSR if primary unavailable), render 960×540 → display 1920×1080, `# png_tol` or ROI gate vs native 1080p reference; exit 0 on TommyLaptop Arc when SDK present. |
| AC-M12-05 | Timing (script): upscaled path (render-res spp comparable) wall-clock **strictly less** than native 1920×1080 same spp / backend on TommyLaptop Arc after warm-up — report medians; exact speedup floor TBD after first CLEAN tip (SHOULD ≥ **1.5×** display-equivalent, not binding until measured). |
| AC-M12-06 | Denoise ON + upscale ON: denoise-then-upscale; no crash; soft-skip OK on Linux without SDK. |
| AC-M12-07 | Hello ≤ 20 KiB; upscale unused DCE; upscale-glue-linked ≤ **700 KiB** PE. |
| AC-M12-08 | No DLSS/Enabler/Swapper/OptiScaler; no frame-gen; no OptiX; artwork composition locked. |
| AC-M12-09 | Linux without SDK: `# requires_xess:` / `# requires_fsr:` soft-skip; OFF twins + trap + roundtrip still green. |
| AC-M12-10 | M9 STRICT RT / M10 denoise STRICT semantics unchanged when upscale soft-fails. |
| AC-M12-11 | Jitter OFF when upscale OFF (CPU SHA). |
| AC-M12-12 | Invalid resolution → trap 122-family. |

AC-M12-04 / AC-M12-05 / AC-M12-07 are eggtooth / bench / script checks.

---

## 11. Image / quality notes

- Upscale ON images are **not** required to be bit-identical to native 1080p.
- Compare with `# png_tol:` (recommend start **12** vs native 1080p same spp class) and/or silver ball-center ROI mean-L stability.
- History / temporal accumulation: first frames MAY be excluded from quality gate (warmup ≥ 8).
- Native reference: same scene, `PATH_UPSCALE_OFF`, display 1920×1080, same backend, same spp (or documented spp×scale equivalence).

---

## 12. Regression / compatibility

- M4 / M7 / M8 / M9 / M10 / M11 fixtures MUST remain green with upscale OFF.
- Enabling upscale MUST NOT flip denoise or glossy defaults.
- Selecting `PATH_UPSCALE_XESS` on a machine without XeSS MUST soft-fail by default (not hang, not inject DLLs).

---

## 13. Harness headers (additions)

| Header | Meaning |
|--------|---------|
| `# requires_xess: true` | Soft-skip when Intel XeSS SDK / device support unavailable |
| `# requires_fsr: true` | Soft-skip when FSR2 SDK / device support unavailable |
| `# requires_vulkan: true` | Existing M7 |
| `# requires_vulkan_rt: true` | Existing M9 |
| `# png_tol: <int>` | Existing |
| `# env: KEY=VALUE` | May include `FARMOS_PATH_UPSCALE_STRICT`, `FARMOS_PATH_UPSCALE_FORCE_FAIL` |

Working directory for M12 fixtures: `spec/tests/M12`.

---

## 14. Soft-skip policy

Soft-skip: do **not** fail; log skip; runner exit 0 for that case.

Linux CI without GPU/SDK: keep upscale-OFF CPU twins, roundtrip, and trap fixtures green; soft-skip XeSS/FSR smokes and Arc benches.

---

## 15. Fixture outline

| Fixture | Kind | What |
|---------|------|------|
| 001_upscale_off_cpu_twin | run_png | Upscale OFF; M4-001 twin SHA |
| 002_upscale_mode_roundtrip | run | `setPathUpscale` / `getPathUpscale`; print mode |
| 003_trap_invalid_upscale | runtime_trap | `setPathUpscale(99)` → 122 |
| 004_upscale_xess_smoke | run_png | Small scene; XeSS ON; `# requires_xess:`; display buffer / tol smoke |
| bench / artwork | script | Arc 960×540→1080p quality + timing vs native |

**Counts (stub pack):** 4 `.fm` harness fixtures (001–004) + README + `_ref` + bench note. Expand as PM requests.

---

## 16. Test index (stub sources)

| # | File | Kind |
|---|------|------|
| 001 | `001_upscale_off_cpu_twin.fm` | run_png |
| 002 | `002_upscale_mode_roundtrip.fm` | run |
| 003 | `003_trap_invalid_upscale.fm` | runtime_trap |
| 004 | `004_upscale_xess_smoke.fm` | run_png |

---

## 17. Open questions

Status: **FINAL**. Open questions: **none.**

OQ-M12-01 .. OQ-M12-09 **accepted A** (PM lock 2026-10-21; user 2026-10-21 lock-all-oq-a). Body §1–§12 / AC-M12-01..12 are **binding** under A.

### OQ-M12-01 — Present / output path — **ACCEPTED A**

| Option | Meaning | Status |
|--------|---------|--------|
| **A** | **Headless** upscale-to-buffer + `savePNG` / readback; **no** OS window/swapchain required in M12 (M10 OQ-M10-01 A no-window **stands**) | **ACCEPTED** |
| B | Minimal OS window + Vulkan swapchain/present required for interactive upscaled display | Rejected |
| C | Both headless export **and** minimal present window required in M12 | Rejected |

### OQ-M12-02 — Upscaler selection — **ACCEPTED A**

| Option | Meaning | Status |
|--------|---------|--------|
| **A** | **XeSS primary** (`PATH_UPSCALE_XESS`); **FSR2 optional** second mode (`PATH_UPSCALE_FSR`); DLSS/Enabler/Swapper/OptiScaler **OUT** | **ACCEPTED** |
| B | XeSS only in M12 (defer FSR) | Rejected |
| C | FSR2 primary; XeSS optional | Rejected |

### OQ-M12-03 — Motion vectors / aux contracts — **ACCEPTED A**

| Option | Meaning | Status |
|--------|---------|--------|
| **A** | Camera MV + depth **required**; object MV **deferred** (zero/static OK for silver-still); reactive mask SHOULD if SDK needs | **ACCEPTED** |
| B | Camera + object MV both required in M12 | Rejected |
| C | Depth-only / spatial upscale fallback if temporal aux missing | Rejected |

### OQ-M12-04 — API shape — **ACCEPTED A**

| Option | Meaning | Status |
|--------|---------|--------|
| **A** | Additive `setPathUpscale` / `getPathUpscale` + `PATH_UPSCALE_OFF` / `XESS` / `FSR`; default **OFF**; invalid → trap **122** | **ACCEPTED** |
| B | Fold into `setPathDenoise` / new denoise modes | Rejected |
| C | Upscale only via `savePNG` flag | Rejected |

### OQ-M12-05 — Order vs denoise — **ACCEPTED A**

| Option | Meaning | Status |
|--------|---------|--------|
| **A** | **Denoise then upscale** when both ON; not mutually exclusive | **ACCEPTED** |
| B | Upscale then denoise | Rejected |
| C | Mutually exclusive (upscale forces denoise OFF) | Rejected |

### OQ-M12-06 — Backend participation / STRICT — **ACCEPTED A**

| Option | Meaning | Status |
|--------|---------|--------|
| **A** | Vulkan compute + Vulkan RT MUST upscale when available; CPU soft-fail (render-res) unless `FARMOS_PATH_UPSCALE_STRICT=1` → trap **122**-family; M9 RT STRICT unchanged | **ACCEPTED** |
| B | Hard-fail always if upscale ON and unavailable | Rejected |
| C | CPU reference spatial upscale required for parity | Rejected |

### OQ-M12-07 — Size budget — **ACCEPTED A**

| Option | Meaning | Status |
|--------|---------|--------|
| **A** | Upscale-glue-linked Farmos PE ≤ **700 KiB**; vendor DLLs external / not in PE cap; hello ≤20; prior ≤300/400/500/600 unchanged; DCE when OFF | **ACCEPTED** |
| B | Keep ≤ **600 KiB** including upscale glue | Rejected |
| C | ≤ **800 KiB** glue-linked | Rejected |

### OQ-M12-08 — AC / Linux strategy — **ACCEPTED A**

| Option | Meaning | Status |
|--------|---------|--------|
| **A** | TommyLaptop Arc XeSS smoke (960×540→1080p) quality + timing vs native; Linux `# requires_xess` / `# requires_fsr` soft-skip; exact speedup floor TBD after first tip (SHOULD ≥1.5× reported) | **ACCEPTED** |
| B | Require Linux XeSS/FSR green in CI | Rejected |
| C | PM-fixed ms / tol numbers before any Arc measurement | Rejected |

### OQ-M12-09 — Frame generation — **ACCEPTED A**

| Option | Meaning | Status |
|--------|---------|--------|
| **A** | Frame-gen (DLSS-G / FSR-FG / XeSS-FG) **OUT** of M12 | **ACCEPTED** |
| B | Allow FSR-FG as optional mode | Rejected |
| C | Defer decision | Rejected |

---

## 18. Accepted decisions (OQ-01..09 FINAL binding)

| ID | Accepted |
|----|----------|
| OQ-M12-01 | A — headless buffer + savePNG; no window; M10-01 stands |
| OQ-M12-02 | A — XeSS primary + FSR2 optional; DLSS/Enabler/Swapper/OptiScaler OUT |
| OQ-M12-03 | A — camera MV + depth; object MV deferred |
| OQ-M12-04 | A — `setPathUpscale` + PATH_UPSCALE_*; default OFF; trap **122** |
| OQ-M12-05 | A — denoise then upscale |
| OQ-M12-06 | A — vk/vk_rt must; CPU soft; STRICT 122-family |
| OQ-M12-07 | A — ≤700 KiB glue PE; DLLs external |
| OQ-M12-08 | A — Arc smoke + Linux soft-skip |
| OQ-M12-09 | A — frame-gen OUT |
| RD-M12-01 | Selective reopen M10 OQ-M10-06: native XeSS/FSR **IN**; DLSS/injection **still OUT** |
| RD-M12-02 | M10 OQ-M10-01 no-window **stands** (not reopened under A) |
| RD-M12-03 | Enabler/Swapper/OptiScaler forever OUT for this milestone |

---

## 19. Amendments relative to M4 / M7 / M8 / M9 / M10 / M11

M1–M11 milestone markdown bodies are **not** edited.

| # | Amendment |
|---|-----------|
| 1 | **Reopen** M10 OQ-M10-06 A vendor-upscaler non-goal **selectively**: native Intel XeSS + optional AMD FSR2 **in scope**; DLSS / Enabler / Swapper / OptiScaler / injection **remain OUT** |
| 2 | Under OQ-M12-01 **A**: do **not** reopen M10 OQ-M10-01 no-window; headless upscale only. OQ-M12-01 B rejected. |
| 3 | `farmos:scene` exports upscale API (§3) |
| 4 | Trap **122** for invalid upscale mode / STRICT upscale failure / invalid resolution |
| 5 | Env `FARMOS_PATH_UPSCALE_STRICT`, `FARMOS_PATH_UPSCALE_FORCE_FAIL` (optional `FARMOS_PATH_UPSCALE`) |
| 6 | Denoise→upscale order when both ON (OQ-M12-05 A binding) |
| 7 | Upscale-glue size ≤ 700 KiB PE (OQ-M12-07 A); vendor DLLs external |
| 8 | Fixtures under `spec/tests/M12/` |
| 9 | Frame-gen OUT (OQ-M12-09 A) |
| 10 | M9 backends / STRICT RT / M10 denoise / M11 glossy unchanged when upscale OFF |

---

## 20. Blockers for poteto (informational — not OQs)

Report to PM if any block implementation after FINAL:

1. **SDK obtain / license**: Intel XeSS SDK + AMD FSR2 SDK redistribution into Farmos tree or fetch script.
2. **Arc drivers / Vulkan**: TommyLaptop XeSS Vulkan interop; required extensions / queues.
3. **Motion vectors**: path tracer today may lack camera MV / depth export images — need aux G-buffers or compute passes.
4. **Jitter integration**: camera ray gen must accept sub-pixel offsets without breaking OFF SHA.
5. **History reset** on progressive restart / spp change.
6. **Headless image usage flags**: confirm XeSS/FSR accept non-swapchain `VkImage` usages; if not, escalate before adopting a window (OQ-01 B) — never Enabler.
7. **Size**: XeSS glue vs ≤700 KiB; dynamic load recommended.
8. **Linux CI**: no SDK → soft-skip only; do not fail tree.

---

## Appendix A — Compat upscale-off twin (001)

Same shape as M4-001 / M7-001 / … / M11-001 (16×16, clear `0x4488ff`). Backend CPU; upscale OFF; denoise OFF; glossy OFF; RR / sampling defaults.

## Appendix B — Invalid upscale trap (003)

```
renderer.setPathUpscale(99); // trap 122
```

## Appendix C — XeSS smoke (004)

Small resolution; `setPathUpscale(PATH_UPSCALE_XESS)`; set render/display sizes; `renderPath` (warmup frames as needed); `savePNG` display buffer. Header `# requires_xess: true`. Soft-skip when SDK absent.

## Appendix D — Arc bench note

TommyLaptop Arc, prefer `PATH_BACKEND_VULKAN_RT`:

- Native: 1920×1080, upscale OFF, documented spp.
- Upscale: render 960×540 → display 1920×1080, XeSS ON, same spp at render-res, denoise policy per fixture.
- Compare visual tol / ROI; record median wall-clock after warm-up.
- Do not alter silver-still composition.

---

## Document history

- 2026-10-21: **DRAFT**. Native Vulkan upscale (XeSS primary / FSR2 optional) after M11. Selectively reopens M10 OQ-M10-06 vendor upscalers (native only; DLSS/Enabler/Swapper OUT). Under recommend A, M10 OQ-M10-01 no-window stands (headless buffer + savePNG). OQ-M12-01..09 recommend A. Trap 122. Upscale-glue ≤700 KiB. Denoise then upscale. Frame-gen OUT. Fixtures under `spec/tests/M12/`. Status **DRAFT** until PM locks OQs. No implementation code.
- 2026-10-21: **FINAL**. PM/user lock OQ-M12-01 .. OQ-M12-09 all A (lock-all-oq-a). Headless buffer+savePNG; XeSS primary + FSR2 optional; camera MV+depth; `setPathUpscale` default OFF trap 122; denoise then upscale; vk/vk_rt must; ≤700 KiB glue PE; Arc smoke + Linux soft-skip; frame-gen OUT. Selective reopen M10-06 (native XeSS/FSR IN; DLSS/Enabler/Swapper/OptiScaler still OUT); M10-01 stands. Open questions: none. No implementation code.
