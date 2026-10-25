# Farmos M12 — Native Vulkan Upscale (XeSS / FSR)

**Status:** **FINAL** (PM/user 2026-10-23: OQ-M12-12 A + OQ-M12-13 A; prior OQ-01..09 A locked 2026-10-21; OQ-10/11 A locked 2026-10-22; **2026-10-25 clarification** clear-m12-backlog — ROI pin + |ΔL| vs mean_abs_dL; gates unchanged)  
**Milestone:** M12 — opt-in native Vulkan temporal upscale for the M4 path tracer  
**Depends on:** `spec/M1-core.md` .. `spec/M11-glossy-continuation.md` (all FINAL)  
**Normative keywords:** MUST / SHOULD / MAY per RFC 2119.  
**Date:** 2026-10-21 (FINAL OQ-M12-01..09); 2026-10-22 FINAL (OQ-M12-10 A + OQ-M12-11 A); **2026-10-23 FINAL** (OQ-M12-12 A + OQ-M12-13 A; user oq-relax-again); **2026-10-25 clarification** (user clear-m12-backlog; post merge `f5b87f8f`; no new OQ; gates unchanged)

This document specifies Farmos M12: an **opt-in** native **Vulkan** upscale path (primary **Intel XeSS**; optional **AMD FSR2**) that reconstructs a higher **display** resolution from a lower **render** resolution path-trace accumulator, without DLSS/NGX, without DLSS-Enabler / Swapper / OptiScaler injection, and without rewriting M4/M10/M11 goldens when upscale is **OFF**. It does **not** specify compiler, runtime, or stdlib implementation code. It does **not** edit M1–M11 milestone markdown bodies; normative amendments relative to those milestones live in §19 of this file.

Open questions: **none**. OQ-M12-01 .. OQ-M12-09 **accepted A** (PM lock 2026-10-21; user 2026-10-21 lock-all-oq-a) — **unchanged**; do **not** reopen. OQ-M12-10 **accepted A** + OQ-M12-11 **accepted A** (PM lock 2026-10-22; user 2026-10-22 oq-relax-1.2-roi) — **unchanged as historical accepted decisions**; do **not** reopen those decision tables. OQ-M12-12 **accepted A** + OQ-M12-13 **accepted A** (PM lock 2026-10-23; user 2026-10-23 oq-relax-again). Binding under 01..09 A: headless upscale-to-buffer + `savePNG` (no OS window; M10 OQ-M10-01 **stands**); **XeSS primary** + **FSR2 optional**; camera MV + depth (object MV deferred); additive `setPathUpscale` + `PATH_UPSCALE_OFF` / `XESS` / `FSR`; default **OFF**; trap **122**; **denoise then upscale**; Vulkan compute + Vulkan RT MUST; CPU soft / STRICT 122-family; upscale-glue PE ≤ **700 KiB** (vendor DLLs external); Arc smoke + Linux soft-skip; frame-gen **OUT**. Selective reopen of M10 OQ-M10-06: native XeSS/FSR **IN**; DLSS/Enabler/Swapper/OptiScaler **still OUT**. Binding AC-M12-05 / AC-M12-04 under **OQ-M12-12 A** / **OQ-M12-13 A**: SHOULD ≥ **1.05×**; XeSS wall **strictly <** native (MUST); ROI L **[80, 200]** + **|ΔL| ≤ 5.0**; png_tol / max_abs / mean_abs_dL report-only. **Pinned AC-04 ROI** (same as M11 CLAIM / M11 §8.2): **40×40** at **`x=940:980,y=520:560`** on **1920×1080** (half-open slice notation as CLAIM). Tip `7f0a080` / product `fd31e46` Pass1 **1.052** / Pass2 **1.162**, ROI L ~**187.228** / **187.877**, binding **|ΔL| ≈ 0.650** (= |187.877 − 187.228|) — **PASS**; earlier printed **3.932** was **mean_abs_dL** mislabeled as |ΔL| (report-only). Historical SHOULD ≥ **1.2×** (OQ-M12-10 A) **superseded** for SHOULD floor only; historical absolute band **[80, 140]** (OQ-M12-11 A) **superseded** for absolute band only. Body §10 / §11 / AC-M12-04 / AC-M12-05 / Appendix D are **binding** under OQ-12 A / OQ-13 A. **2026-10-25 clarification** (clear-m12-backlog): ROI pin + |ΔL| vs mean_abs_dL — Status remains **FINAL**; no gate number change; no new OQ.

**Lock context (2026-10-22; user chose oq-relax-1.2-roi) — historical FINAL OQ-10/11 A:** PR #20 tip `edeb845` Arc (TommyLaptop Vulkan RT, official XeSS 2.1.1, glossy ON): AC-M12-05 native median **4.302 s** / XeSS **3.477 s** → ratio **1.237** — binding (XeSS wall **strictly <** native) **PASS**; under **OQ-M12-10 A** SHOULD ≥ **1.2×** also **PASS** (historical SHOULD ≥ **1.5×** **superseded**). AC-M12-04 silver ROI L ~**105.8** both lanes; mean_abs ~**0.465**; max_abs ~**211** — under **OQ-M12-11 A** primary ROI gate **PASS**; `# png_tol: 12` / max_abs **report-only** (historical hard gate **superseded**). Pattern: M9 OQ-M9-08 / M10 OQ-M10-09.

**Lock context (2026-10-23; user chose oq-relax-again) — FINAL OQ-12/13 A:** PR #20 tip `7f0a080`, product `fd31e46` (prior tip `edeb845` remains historical). Glossy Arc CLAIM inputs (clean host, `PATH_GLOSSY_VNDF`): AC-M12-05 Pass1 ratio **1.052**, Pass2 **1.162** (binding XeSS wall **strictly <** native **PASS** both; SHOULD ≥ **1.05×** **PASS** under OQ-M12-12 A). AC-M12-04 ROI L **187.228** / **187.877** — **PASS** under OQ-M12-13 A band **[80, 200]**; binding **|ΔL| ≈ 0.650** (= |187.877 − 187.228|) ≤ 5.0 **PASS** (FAIL vs historical **[80, 140]** on absolute band only). Earlier CLAIM/spec lock text printed **3.932** as |ΔL|; eggtooth AUDIT PASS r2: that figure was **mean_abs_dL** mislabeled — **report-only**, MUST NOT be labeled |ΔL| (see §11 errata). Prior `edeb845`: ratio **1.237** once + ROI L~**106**; eggtooth AUDIT FAIL on 1.2× repeatability (**1.237** then **1.131**). more-headroom / more-optimize tips lost SHOULD and ROI drifted to ~**187**. OQ-M12-12 **A** + OQ-M12-13 **A** locked. Historical OQ-M12-10 A SHOULD ≥ **1.2×** **superseded** for SHOULD floor only; historical OQ-M12-11 A **[80, 140]** **superseded** for absolute band only. Pattern: M9 OQ-M9-08 / M10 OQ-M10-09 / OQ-M12-10/11. **2026-10-25 clarification:** AC-04 ROI pinned to M11 CLAIM box `x=940:980,y=520:560` @1080p (40×40); |ΔL| vs mean_abs_dL naming fixed; gates unchanged.

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

## 10. Acceptance criteria (OQ-M12-08 A base; AC-04 / AC-05 binding under OQ-M12-13 A / OQ-M12-12 A)

OQ-M12-01..09 A remain binding for present / stack / API / denoise order / backends / size / Linux soft-skip / frame-gen OUT. OQ-M12-10 A / OQ-M12-11 A remain **accepted historical** decisions (SHOULD floor and absolute band respectively **superseded** by OQ-M12-12 A / OQ-M12-13 A as noted). **Binding** AC-M12-04 / AC-M12-05 gates are **OQ-M12-13 A** / **OQ-M12-12 A**: SHOULD ≥ **1.05×**; ROI **[80, 200]** + **|ΔL| ≤ 5.0**; XeSS wall **strictly <** native; png_tol / max_abs report-only. Other ACs unchanged.

| ID | Criterion |
|----|-----------|
| AC-M12-01 | Upscale OFF + CPU → M4 twin SHA / existing OFF goldens preserved (001). |
| AC-M12-02 | `setPathUpscale` / `getPathUpscale` roundtrip; default 0. |
| AC-M12-03 | `setPathUpscale(99)` → trap **122**. |
| AC-M12-04 | Quality smoke (**binding OQ-M12-13 A**): mode XeSS (or FSR if primary unavailable), render 960×540 → display 1920×1080 on TommyLaptop Arc when SDK present; exit 0. **Pinned ROI** (normative; same as M11 CLAIM / M11 §8.2): **40×40** pixels at **`x=940:980,y=520:560`** on **1920×1080** display (CLAIM half-open slice notation); **same box** for native and XeSS lanes after warm-up — do **not** use floating center±80 or floating centroid. **Primary PASS:** ROI mean luminance **L** (same L as M11 CLAIM harness) for **both** lanes in band **[80, 200]** after warm-up (≥ 8 frames), and binding **|ΔL| = |L_xess − L_native| ≤ 5.0** (absolute difference of the two ROI **mean** luminances). **Report** mean_abs (channel / full-frame as historically tip ~0.465). **Report-only:** mean_abs_dL (ROI per-pixel abs mean of L diffs — tip **3.932**; MUST NOT be labeled |ΔL|); `# png_tol: 12` / max_abs (demotion from OQ-M12-11 A stands). Tip `7f0a080` / product `fd31e46` ROI L ~**187.228** / **187.877**, binding **|ΔL| ≈ 0.650** — **PASS**. Historical absolute band **[80, 140]** (OQ-M12-11 A) **superseded** for absolute band only. |
| AC-M12-05 | Timing (script) (**binding OQ-M12-12 A**): upscaled path (render-res spp comparable) wall-clock **strictly less** than native 1920×1080 same spp / backend on TommyLaptop Arc after warm-up — **MUST** (unchanged). SHOULD display-equivalent speedup ≥ **1.05×** (native_median / xess_median) after warm-up. Report medians. Measured tip `7f0a080` / product `fd31e46`: Pass1 **1.052**, Pass2 **1.162** (binding XeSS < native **PASS** both; SHOULD **PASS**). Historical tip `edeb845` **1.237** remains documented. Historical SHOULD ≥ **1.2×** (OQ-M12-10 A) **superseded** for SHOULD floor only. |
| AC-M12-06 | Denoise ON + upscale ON: denoise-then-upscale; no crash; soft-skip OK on Linux without SDK. |
| AC-M12-07 | Hello ≤ 20 KiB; upscale unused DCE; upscale-glue-linked ≤ **700 KiB** PE. |
| AC-M12-08 | No DLSS/Enabler/Swapper/OptiScaler; no frame-gen; no OptiX; artwork composition locked. |
| AC-M12-09 | Linux without SDK: `# requires_xess:` / `# requires_fsr:` soft-skip; OFF twins + trap + roundtrip still green. |
| AC-M12-10 | M9 STRICT RT / M10 denoise STRICT semantics unchanged when upscale soft-fails. |
| AC-M12-11 | Jitter OFF when upscale OFF (CPU SHA). |
| AC-M12-12 | Invalid resolution → trap 122-family. |

### 10.1 Measured Arc tips (informational; OQ-10/11 historical + OQ-12/13 inputs)

**Historical (OQ-10/11 lock):** PR #20 tip `edeb845` (TommyLaptop Vulkan RT, official XeSS 2.1.1, glossy ON):

| Metric | Value | vs gate |
|--------|-------|---------|
| Native median | **4.302 s** | — |
| XeSS median | **3.477 s** | XeSS < native → binding **PASS** |
| Ratio native/XeSS | **1.237** | **PASS** vs OQ-10 A ≥1.2×; eggtooth later AUDIT saw **1.131** on repeat |
| Silver ROI L (both lanes) | ~**105.8** / ~**106** | in [80,140]; |ΔL| ~0 ≤ 5 → primary **PASS** (OQ-11 A) |
| mean_abs | ~**0.465** | report |
| max_abs | ~**211** | report-only under OQ-11 A |

**Latest VALID glossy Arc CLAIM (OQ-12/13 lock inputs):** tip `7f0a080`, product `fd31e46` (clean host, `PATH_GLOSSY_VNDF`):

| Metric | Value | vs FINAL OQ-12 A / OQ-13 A |
|--------|-------|----------------------------|
| AC-05 Pass1 ratio | **1.052** | **PASS** SHOULD ≥1.05× (OQ-12 A); FAIL vs historical ≥1.2× (OQ-10 A); binding XeSS < native **PASS** |
| AC-05 Pass2 ratio | **1.162** | **PASS** SHOULD ≥1.05×; FAIL vs historical ≥1.2×; binding **PASS** |
| AC-04 ROI (pinned) | `x=940:980,y=520:560` @1080p (40×40; M11 CLAIM) | same box both lanes |
| AC-04 ROI L | **187.228** / **187.877** | **PASS** [80,200] (OQ-13 A); FAIL vs historical [80,140] (OQ-11 A) |
| AC-04 **|ΔL|** (means) | **≈ 0.650** (= \|187.877 − 187.228\|) | **PASS** ≤ 5.0 under OQ-13 A (and historical OQ-11 A) |
| AC-04 mean_abs_dL | **3.932** | **report-only** — earlier mislabeled as \|ΔL\|; MUST NOT gate as \|ΔL\| |

AC-M12-04 / AC-M12-05 / AC-M12-07 are eggtooth / bench / script checks.

---

## 11. Image / quality notes (**binding OQ-M12-13 A**)

### 11.1 Pinned silver ROI (normative for AC-M12-04)

CLAIM / AUDIT MUST use the **same** pixel box — align with M11 CLAIM and M11 §8.2 / `spec/tests/M11/README.md`. Do **not** invent a different box; do **not** use vague “image center ±80” or a floating centroid for the binding AC.

| Field | Binding value |
|-------|----------------|
| Display resolution | **1920×1080** |
| Crop size | **40×40** pixels |
| Pixel box (CLAIM notation) | **`x=940:980,y=520:560`** — inclusive start, exclusive end (half-open), matching CLAIM `x=940:980,y=520:560` |
| Lanes | **Same box** for native and XeSS after warm-up (≥ 8 frames) |
| L formula | **Same L as M11 CLAIM harness** (mean luminance over the pinned ROI in 0–255 display space after `savePNG`; do not invent a new formula) |

### 11.2 Metric definitions (normative)

| Symbol | Definition | Gate |
|--------|------------|------|
| **L** / ROI mean L | Mean luminance over the **pinned** 40×40 ROI (`x=940:980,y=520:560` @1080p); same L as M11 CLAIM harness | Both lanes ∈ **[80, 200]** (OQ-13 A) |
| **\|ΔL\|** | **\|L_xess − L_native\|** — absolute difference of the two ROI **mean** luminances | Binding ≤ **5.0** (OQ-13 A) |
| **mean_abs** | Channel mean absolute difference vs native display (historical tip `edeb845` ~**0.465**; full-frame / channel as that tip) | **Report-only** |
| **mean_abs_dL** | Mean absolute **per-pixel** luminance difference between XeSS and native **inside the same pinned ROI** (tip `7f0a080` printed **3.932**) | **Report-only** — MUST NOT be labeled \|ΔL\| |
| max_abs / `# png_tol: 12` | Unchanged from OQ-M12-11 A demotion | **Report-only** |

### 11.3 Primary gate and tip errata

- Upscale ON images are **not** required to be bit-identical to native 1080p.
- **Primary quality gate (OQ-M12-13 A):** pinned-ROI mean-L stability — both lanes L ∈ **[80, 200]** and **\|ΔL\| = \|L_xess − L_native\| ≤ 5.0** after warm-up (≥ 8). Tip `7f0a080` L ~**187** **PASS**; binding \|ΔL\| ≈ **0.650** **PASS**. See AC-M12-04.
- Historical absolute band **[80, 140]** (OQ-M12-11 A) **superseded** for absolute band only; \|ΔL\| ≤ 5.0 and report-only demotion of png_tol / max_abs **stand**.
- **Report:** mean_abs (channel mean absolute diff vs native display). Tip `edeb845` ~**0.465**.
- **Report-only (demoted; OQ-M12-11 A demotion stands):** max_abs / historical `# png_tol: 12` max-channel gate. Tip `edeb845` max_abs ~**211** (sparse outliers) MUST NOT alone FAIL AC-M12-04. Prefer demotion over inventing a huge tol (≥211) without rationale.
- **Errata (tip `7f0a080` / product `fd31e46`; eggtooth AUDIT PASS r2 / user clear-m12-backlog 2026-10-25):** ROI L **187.228** / **187.877** stand. Binding **\|ΔL\| ≈ 0.650** (= \|187.877 − 187.228\|) — **PASS** ≤ 5.0. Printed **3.932** was **mean_abs_dL** mislabeled as \|ΔL\| in earlier CLAIM / spec lock context — **report-only**; correct any table/history that called \|ΔL\| **3.932**.
- History / temporal accumulation: first frames MAY be excluded from quality gate (warmup ≥ 8).
- Native reference: same scene, `PATH_UPSCALE_OFF`, display 1920×1080, same backend, same spp (or documented spp×scale equivalence), **same pinned ROI**.

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

Status: **FINAL**. Open questions: **none**. OQ-M12-01 .. OQ-M12-13 all **accepted A** — do **not** reopen those decision tables.

PM lock **2026-10-21** (user 2026-10-21 lock-all-oq-a): **OQ-M12-01 .. OQ-M12-09** all **accepted option A**. PM lock **2026-10-22** (user 2026-10-22 oq-relax-1.2-roi): **OQ-M12-10** + **OQ-M12-11** both **accepted option A**. PM lock **2026-10-23** (user 2026-10-23 oq-relax-again): **OQ-M12-12** + **OQ-M12-13** both **accepted option A**. Rejected options remain below for audit trail only. Present / stack / API / denoise order / backends / size / STRICT / frame-gen / Linux soft-skip **unchanged**. Binding AC-04 / AC-05 under OQ-13 A / OQ-12 A. Body §10 / §11 / Appendix D **binding**.

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

### OQ-M12-10 — AC-M12-05 SHOULD speedup floor relax — **ACCEPTED A**

**Context (binding inputs):** PR #20 tip `edeb845` Arc (TommyLaptop Vulkan RT, official XeSS 2.1.1, glossy ON). Native median **4.302 s** / XeSS **3.477 s** → ratio **1.237**. Binding gate (XeSS wall-clock **strictly <** native after warm-up) **PASS**. Historical SHOULD ≥ **1.5×** display-equivalent **FAIL**. Prior RCA + opts (fill_aux→path depth/MV, persistent Vk, PSO disk cache, device borrow) already landed; process-per-frame AC-05 still caps 1.5×. User chose **oq-relax-1.2-roi** (2026-10-22). Pattern: M9 OQ-M9-08 / M10 OQ-M10-09. PM lock **2026-10-22**: option **A**.

| Option | Meaning | Status |
|--------|---------|--------|
| **A** | SHOULD display-equivalent speedup ≥ **1.2×** (native_median / xess_median) on TommyLaptop Arc after warm-up. **Binding remains:** XeSS wall-clock **strictly <** native (unchanged). Document measured tip `edeb845` **1.237** as first Arc tip (PASSES under A). Historical SHOULD ≥ **1.5×** (from OQ-M12-08 A / AC-M12-05) **superseded** for the SHOULD floor only. Present / stack / API / denoise order / backends / size / STRICT / frame-gen / Linux soft-skip **unchanged**. | **ACCEPTED** |
| B | Keep SHOULD ≥ **1.5×** (tip `edeb845` remains FAIL on SHOULD) | Rejected |
| C | PM numbers (other ratio / different machine wording) | Rejected |

**Rationale for A:** Measured **1.237** is real Arc tip under process-per-frame overhead; ≥1.2× gives ~3% headroom and lets `edeb845` PASS SHOULD without pretending 1.5× is reachable until a larger architectural change. Binding “XeSS < native” still prevents regressions that are slower than native.

### OQ-M12-11 — AC-M12-04 quality gate relax — **ACCEPTED A**

**Context (binding inputs):** Same tip `edeb845`. Silver-ball-center ROI mean L ~**105.8** on **both** native and XeSS lanes (**PASS** intent). mean_abs ~**0.465**. max_abs ~**211** → strict max-channel `# png_tol: 12` **FAIL** (sparse outliers). User chose **oq-relax-1.2-roi** (2026-10-22). PM lock **2026-10-22**: option **A**.

| Option | Meaning | Status |
|--------|---------|--------|
| **A** | **Primary PASS** for AC-M12-04: after warm-up (≥ 8), silver-ball-center ROI mean luminance **L** for **both** native and XeSS lanes in **[80, 140]**, and **\|L_xess − L_native\| ≤ 5.0**. **Report** mean_abs (informational; tip ~0.465). **Demote** strict max-channel `# png_tol: 12` / max_abs from PASS/FAIL to **report-only** (do **not** invent tol ≥211 without rationale). Tip `edeb845` **PASSES** primary. Historical hard `# png_tol: 12` interpretation of AC-M12-04 / §11 **superseded**. Speed / API / stack / other ACs **unchanged** except as OQ-M12-10. | **ACCEPTED** |
| B | Keep strict `# png_tol: 12` max-channel as PASS/FAIL (tip `edeb845` remains FAIL on quality) | Rejected |
| C | PM numbers (other ROI band / \|ΔL\| / raise png_tol to a fixed large N) | Rejected |

**Rationale for A:** ROI mean-L matches the product intent (silver ball clearly lit, not black; XeSS lane tracks native). Sparse max_abs outliers are expected for temporal upscale vs native and do not justify a huge invented `# png_tol`. Demoting max-channel to report-only preserves observability without false FAIL.

### OQ-M12-12 — AC-M12-05 SHOULD speed floor (second relax) — **ACCEPTED A**

**Context (binding inputs):** User chose **oq-relax-again** (2026-10-23). PR #20 tip `7f0a080`, product `fd31e46`. Glossy Arc CLAIM (clean host, `PATH_GLOSSY_VNDF`): AC-M12-05 Pass1 ratio **1.052**, Pass2 **1.162** (binding XeSS wall **strictly <** native **PASS** both). Prior tip `edeb845`: ratio **1.237** once; eggtooth AUDIT FAIL on 1.2× repeatability (**1.237** then **1.131**). more-headroom / more-optimize tips lost SHOULD. Historical OQ-M12-10 A SHOULD ≥ **1.2×** → tip range **FAIL SHOULD**. Pattern: M9 OQ-M9-08 / M10 OQ-M10-09 / OQ-M12-10. PM lock **2026-10-23** (user oq-relax-again): option **A**.

| Option | Meaning | Status |
|--------|---------|--------|
| **A** | SHOULD display-equivalent speedup ≥ **1.05×** (native_median / xess_median) on TommyLaptop Arc after warm-up. **Binding remains:** XeSS wall-clock **strictly <** native (unchanged). Document measured tip range Pass1 **1.052** / Pass2 **1.162** (tip `7f0a080` / product `fd31e46`). Historical SHOULD ≥ **1.2×** (OQ-M12-10 A) **superseded** for the SHOULD floor only. Present / stack / API / denoise order / backends / size / STRICT / frame-gen / Linux soft-skip **unchanged**. | **ACCEPTED** |
| B | SHOULD ≥ **1.10×** (Pass1 **1.052** would **FAIL** SHOULD; Pass2 **1.162** **PASS**) | Rejected |
| C | Binding-only — drop SHOULD speed floor entirely; XeSS wall **strictly <** native remains **MUST**; report ratio informational | Rejected |
| D | Keep SHOULD ≥ **1.2×** (current tips **FAIL** SHOULD) | Rejected |

**Rationale for A:** Floor matches the lower measured Pass1 (**1.052**) with ~0.2% headroom; keeps a SHOULD gate; binding XeSS < native unchanged. Measured tip range **1.05–1.16**.

### OQ-M12-13 — AC-M12-04 primary quality (second relax) — **ACCEPTED A**

**Context (binding inputs):** Same tip `7f0a080` / product `fd31e46`. AC-M12-04 ROI L **187.228** / **187.877** FAIL vs historical OQ-M12-11 A band **[80, 140]**; binding **|ΔL| ≈ 0.650** OK (≤ 5.0). Lock-time text also cited **3.932** as |ΔL| — **errata (2026-10-25):** that figure was **mean_abs_dL** mislabeled; true \|ΔL\| of means = \|187.877 − 187.228\| ≈ **0.650** (still OK ≤ 5.0). Prior `edeb845` ROI L ~**106**; more-headroom / more-optimize tips drifted ROI to ~**187**. Historical OQ-M12-11 A → tip **FAIL** absolute band. PM lock **2026-10-23** (user oq-relax-again): option **A**. Option A gate numbers **unchanged** by clear-m12-backlog clarification.

| Option | Meaning | Status |
|--------|---------|--------|
| **A** | Expand ROI absolute band to **[80, 200]**; keep **|ΔL| ≤ 5.0**; both lanes must be in band; report mean_abs; png_tol / max_abs stay **report-only**. Tip L ~**187** **PASS**. Historical **[80, 140]** (OQ-M12-11 A) **superseded** for absolute band only. Speed / API / stack / other ACs **unchanged** except as OQ-M12-12. | **ACCEPTED** |
| B | Primary = **|ΔL| ≤ 5.0** only (center/relative); absolute ROI band becomes **report-only** (no PASS/FAIL on absolute L) | Rejected |
| C | Restore `# png_tol: 12` / max_abs as primary PASS/FAIL (**not recommended**; tip max_abs historically ~**211**) | Rejected |
| D | Keep **[80, 140]** (current L ~**187** **FAIL**) | Rejected |

**Rationale for A:** Keeps the same primary-gate shape as OQ-M12-11 A (absolute band + |ΔL|); expands upper bound to cover glossy Arc tip drift from ~**106→~187** without dropping absolute luminance intent.

---

## 18. Accepted decisions (OQ-01..13 FINAL binding)

PM lock 2026-10-21 (user 2026-10-21 lock-all-oq-a) for OQ-M12-01..09. PM lock 2026-10-22 (user 2026-10-22 oq-relax-1.2-roi) for OQ-M12-10 **A** + OQ-M12-11 **A**. PM lock 2026-10-23 (user 2026-10-23 oq-relax-again) for OQ-M12-12 **A** + OQ-M12-13 **A**. All rows below are **binding accepted**. OQ-M12-08's historical SHOULD ≥1.5× floor is **superseded** by OQ-M12-10 A, then OQ-M12-10 A's SHOULD ≥1.2× floor is **superseded** by OQ-M12-12 A (SHOULD floor only; binding XeSS < native unchanged). AC-M12-04 hard `# png_tol: 12` is **superseded** by OQ-M12-11 A (primary = ROI L band + |ΔL| ≤ 5.0; mean_abs report; max_abs / png_tol report-only); OQ-M12-11 A's absolute band **[80, 140]** is **superseded** by OQ-M12-13 A **[80, 200]** (absolute band only; |ΔL| ≤ 5.0 and report-only demotion stand).

| ID | Accepted |
|----|----------|
| OQ-M12-01 | A — headless buffer + savePNG; no window; M10-01 stands |
| OQ-M12-02 | A — XeSS primary + FSR2 optional; DLSS/Enabler/Swapper/OptiScaler OUT |
| OQ-M12-03 | A — camera MV + depth; object MV deferred |
| OQ-M12-04 | A — `setPathUpscale` + PATH_UPSCALE_*; default OFF; trap **122** |
| OQ-M12-05 | A — denoise then upscale |
| OQ-M12-06 | A — vk/vk_rt must; CPU soft; STRICT 122-family |
| OQ-M12-07 | A — ≤700 KiB glue PE; DLLs external |
| OQ-M12-08 | A — Arc smoke + Linux soft-skip; historical SHOULD ≥1.5× **superseded** by OQ-M12-10 A then OQ-M12-12 A (binding XeSS < native unchanged) |
| OQ-M12-09 | A — frame-gen OUT |
| OQ-M12-10 | **A** — SHOULD ≥ **1.2×** (historical); binding XeSS wall strictly < native; tip `edeb845` **1.237** documented; historical SHOULD ≥1.5× superseded for SHOULD floor only; **SHOULD floor superseded by OQ-M12-12 A** (≥ **1.05×**) |
| OQ-M12-11 | **A** — primary AC-M12-04 = ROI L both lanes in **[80, 140]** and **|ΔL| ≤ 5.0** (historical absolute band); report mean_abs; demote `# png_tol: 12` / max_abs to report-only; historical hard png_tol:12 superseded; **absolute band superseded by OQ-M12-13 A** (**[80, 200]**) |
| OQ-M12-12 | **A** — SHOULD ≥ **1.05×**; binding XeSS wall strictly < native; tip `7f0a080` / product `fd31e46` Pass1 **1.052** / Pass2 **1.162** documented; historical OQ-10 A ≥1.2× superseded for SHOULD floor only |
| OQ-M12-13 | **A** — primary AC-M12-04 = ROI L both lanes in **[80, 200]** and **|ΔL| ≤ 5.0**; report mean_abs; png_tol / max_abs report-only; tip L ~**187** PASS; historical OQ-11 A [80, 140] superseded for absolute band only |
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
| 11 | **FINAL** OQ-M12-10 A: SHOULD ≥ **1.2×** (supersedes historical ≥1.5× SHOULD floor); binding XeSS < native unchanged; tip `edeb845` **1.237** documented |
| 12 | **FINAL** OQ-M12-11 A: AC-M12-04 primary = ROI L band + |ΔL| ≤ 5.0; demote `# png_tol: 12` / max_abs to report-only; keep mean_abs report |
| 13 | **FINAL** OQ-M12-12 A: SHOULD ≥ **1.05×** (supersedes OQ-10 A ≥1.2× SHOULD floor); binding XeSS < native unchanged; tip `7f0a080` / product `fd31e46` Pass1 **1.052** / Pass2 **1.162** documented |
| 14 | **FINAL** OQ-M12-13 A: AC-M12-04 primary = ROI L **[80, 200]** + |ΔL| ≤ 5.0; tip L ~**187** PASS; historical OQ-11 A [80, 140] superseded for absolute band only; png_tol / max_abs report-only |
| 15 | **Clarification FINAL** (2026-10-25; user clear-m12-backlog; post merge `f5b87f8f`): pin AC-04 silver ROI to M11 CLAIM box **`x=940:980,y=520:560`** @1080p (**40×40**); define **|ΔL| = |L_xess − L_native|** of ROI means; reclassify tip **3.932** as **mean_abs_dL** (report-only; was mislabeled |ΔL|); true tip |ΔL| ≈ **0.650**. OQ-12 A / OQ-13 A gate numbers **unchanged**. No new OQ. |

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
- **Pinned ROI (AC-04):** **40×40** at **`x=940:980,y=520:560`** on 1920×1080 (M11 CLAIM / M11 §8.2); same box both lanes. Not center±80 / floating centroid.
- Quality (**binding OQ-M12-13 A**): primary = pinned-ROI mean-L band **[80, 200]** + **|ΔL| = |L_xess − L_native| ≤ 5.0**; report mean_abs; **mean_abs_dL** / max_abs / `# png_tol: 12` report-only (mean_abs_dL MUST NOT be labeled |ΔL|). Tip `7f0a080` L ~**187** PASS; binding |ΔL| ≈ **0.650** PASS; tip **3.932** = mean_abs_dL (errata). Historical [80, 140] (OQ-11 A) superseded for absolute band only.
- Timing (**binding OQ-M12-12 A**): binding XeSS median < native median; SHOULD ≥ **1.05×**; document tip `7f0a080` / product `fd31e46` Pass1 **1.052** / Pass2 **1.162**; historical tip `edeb845` **1.237**. Historical SHOULD ≥ **1.2×** (OQ-10 A) superseded for SHOULD floor only.
- Do not alter silver-still composition.

---

## Document history

- 2026-10-21: **DRAFT**. Native Vulkan upscale (XeSS primary / FSR2 optional) after M11. Selectively reopens M10 OQ-M10-06 vendor upscalers (native only; DLSS/Enabler/Swapper OUT). Under recommend A, M10 OQ-M10-01 no-window stands (headless buffer + savePNG). OQ-M12-01..09 recommend A. Trap 122. Upscale-glue ≤700 KiB. Denoise then upscale. Frame-gen OUT. Fixtures under `spec/tests/M12/`. Status **DRAFT** until PM locks OQs. No implementation code.
- 2026-10-21: **FINAL**. PM/user lock OQ-M12-01 .. OQ-M12-09 all A (lock-all-oq-a). Headless buffer+savePNG; XeSS primary + FSR2 optional; camera MV+depth; `setPathUpscale` default OFF trap 122; denoise then upscale; vk/vk_rt must; ≤700 KiB glue PE; Arc smoke + Linux soft-skip; frame-gen OUT. Selective reopen M10-06 (native XeSS/FSR IN; DLSS/Enabler/Swapper/OptiScaler still OUT); M10-01 stands. Open questions: none. No implementation code.
- 2026-10-22: **DRAFT amendment** (OQ-M12-10 / OQ-M12-11). Measured PR #20 tip `edeb845` Arc: ratio **1.237** (binding XeSS < native PASS; SHOULD ≥1.5× FAIL); ROI L ~105.8 both lanes; mean_abs ~0.465; max_abs ~211 (strict `# png_tol: 12` FAIL). User chose oq-relax-1.2-roi. Open OQ-M12-10 recommend **A** SHOULD ≥ **1.2×** (binding XeSS < native unchanged; tip 1.237 documented). Open OQ-M12-11 recommend **A** primary ROI L ∈ [80,140] + \|ΔL\| ≤ 5.0; report mean_abs; demote `# png_tol: 12` / max_abs to report-only. Body §10 / §11 / AC-M12-04 / AC-M12-05 provisional under A. OQ-01..09 **not reopened**. Status **not FINAL** until OQ-10/11 locked. No implementation code.
- 2026-10-22: **FINAL**. PM/user lock OQ-M12-10 A + OQ-M12-11 A (oq-relax-1.2-roi). Binding AC-M12-05: XeSS wall strictly < native; SHOULD ≥ **1.2×**; tip `edeb845` **1.237** documented; historical SHOULD ≥1.5× superseded for SHOULD floor only. Binding AC-M12-04: primary ROI L both lanes in **[80, 140]** and **|ΔL| ≤ 5.0**; report mean_abs; demote `# png_tol: 12` / max_abs to report-only; historical hard png_tol:12 superseded. OQ-01..09 A unchanged. Open questions: none. No implementation code.
- 2026-10-23: **DRAFT amendment** (OQ-M12-12 / OQ-M12-13). User chose **oq-relax-again**. Measured tip `7f0a080` / product `fd31e46` glossy Arc CLAIM FAIL: AC-05 Pass1 **1.052** / Pass2 **1.162** (binding XeSS < native PASS); AC-04 ROI L ~**187.228** / **187.877** FAIL [80,140]; lock-time |ΔL| text **3.932** (later errata: mean_abs_dL; true |ΔL| ≈ **0.650**) OK vs ≤5. Prior `edeb845` 1.237 once + L~106; eggtooth AUDIT FAIL on 1.2× repeatability (1.237 then 1.131). Open OQ-M12-12 recommend **A** SHOULD ≥ **1.05×** (binding XeSS < native unchanged; historical OQ-10 A ≥1.2× superseded for SHOULD floor only if A locked). Open OQ-M12-13 recommend **A** ROI absolute band **[80, 200]** + |ΔL| ≤ 5.0 (historical OQ-11 A [80,140] superseded for absolute band only if A locked). Body §10 / §11 / Appendix D provisional under recommend A. OQ-01..11 **not reopened**. Status **not FINAL** until OQ-12/13 locked. poteto HOLD. No implementation code.
- 2026-10-23: **FINAL**. PM/user lock OQ-M12-12 A + OQ-M12-13 A (oq-relax-again). Binding AC-M12-05: XeSS wall strictly < native; SHOULD ≥ **1.05×**; tip `7f0a080` / product `fd31e46` Pass1 **1.052** / Pass2 **1.162** documented; historical SHOULD ≥ **1.2×** (OQ-M12-10 A) superseded for SHOULD floor only. Binding AC-M12-04: primary ROI L both lanes in **[80, 200]** and **|ΔL| ≤ 5.0**; report mean_abs; `# png_tol: 12` / max_abs report-only; tip L ~**187.228** / **187.877** PASS; lock-time |ΔL| text cited **3.932** — **corrected 2026-10-25** to binding |ΔL| ≈ **0.650** with **3.932** = mean_abs_dL mislabel; historical absolute band **[80, 140]** (OQ-M12-11 A) superseded for absolute band only. OQ-01..11 A unchanged as accepted decisions (supersession notes as above). Open questions: none. No implementation code.
- 2026-10-25: **Clarification / errata amendment** (Status remains **FINAL**). User chose **clear-m12-backlog** (post M12 merge `f5b87f8f`). Pin AC-04 silver ROI to M11 CLAIM **`x=940:980,y=520:560`** @1920×1080 (**40×40**); eliminate center±80 / floating-centroid ambiguity. Normative metric table: **|ΔL| = |L_xess − L_native|** of ROI means (binding ≤ 5.0); **mean_abs_dL** report-only (tip **3.932** was mislabeled as |ΔL|; true tip |ΔL| ≈ **0.650**). OQ-M12-12 A / OQ-M12-13 A gate numbers **unchanged** (SHOULD ≥1.05×; band [80,200]; |ΔL|≤5.0). Open questions: **none**. No new OQ. No implementation code.
