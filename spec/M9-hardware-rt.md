# Farmos M9 — Hardware Ray Tracing

**Status:** FINAL (PM 2026-10-16: OQ-M9-01..07 all A; PM 2026-10-18: OQ-M9-08 A)  
**Milestone:** M9 — hardware ray tracing for the M4 path tracer (Vulkan RT preferred)  
**Depends on:** `spec/M1-core.md` (FINAL), `spec/M2-math.md` (FINAL), `spec/M3-scene.md` (FINAL), `spec/M3.5-concurrency.md` (FINAL), `spec/M4-ray.md` (FINAL), `spec/M5-physics.md` (FINAL), `spec/M6-hardening.md` (FINAL), `spec/M7-vulkan-path.md` (FINAL), `spec/M8-path-speed.md` (FINAL)  
**Normative keywords:** MUST / SHOULD / MAY per RFC 2119.  
**Date:** 2026-10-16 (FINAL OQ-M9-01..07); **2026-10-18 FINAL** (OQ-M9-08 A wall-clock AC relax)

This document specifies Farmos M9: an **optional hardware ray-tracing** backend that accelerates the existing M4 path tracer (and M7/M8 compute Vulkan path) using Vulkan acceleration structures / ray queries. It does **not** specify compiler, runtime, or stdlib implementation code. It does **not** edit M1–M8 milestone markdown bodies; normative amendments relative to those milestones live in §19 of this file.

Open questions: **none.** OQ-M9-01 .. OQ-M9-08 are all **accepted A** (PM 2026-10-16 for 01..07; PM 2026-10-18 for OQ-M9-08). Binding: denoise OUT (OQ-M9-07); size budgets (OQ-M9-06); STRICT behavior (OQ-M9-05); API / stack / pipeline (OQ-M9-01..03); wall-clock ≤ **5000 ms** primary / ≥ **5×** secondary (OQ-M9-08 A; OQ-M9-04 ≤4 s / ≥8× **superseded**); M7 ray-query / HW RT non-goal reopened as in-scope for M9; Vulkan AS/RT only (not OptiX); `VK_KHR_ray_query` inside compute; soft fallback RT→compute→CPU with STRICT trap 117; DLSS/frame-gen/Swapper OUT; CPU + compute fallback + DCE required. Those decisions are recorded in §18. Body wall-clock numbers in §9 / AC-M9-13..14 are **binding under OQ-M9-08 A**.

Related: `/workspace/farmos/PLAN.md` (M9 section), `spec/M4-ray.md`, `spec/M7-vulkan-path.md`, `spec/M8-path-speed.md`, `spec/tests/M9/README.md`.

**Binding product intent (PM / user 2026-10-16):**
- User chose **hardware-rt** over denoise / interactive UI as the next speed lever after M8 (commit `7acef2ed` merged).
- Pain: 1080p @ 256 spp still ~**32 s** on TommyLaptop compute path — too slow vs GPU apps that do complex 1080p in **&lt;1 s**.
- Goal: order-of-magnitude toward **interactive / seconds** at the **same** silver-still quality (1920×1080 / 256 spp), via hardware RT — **not** by forcing lower spp.
- **Vulkan ray-tracing / acceleration structures only** (OQ-M9-02 A); OptiX is **out**.
- **CPU + M7/M8 compute Vulkan fallback required**.
- DCE: unused GPU/RT MUST NOT grow hello (≤ 20 KiB; no Vulkan/RT symbols when unused).
- **Denoise as a product feature remains OUT** (OQ-M9-07 A).
- Non-goals: DLSS frame gen / Swapper-style DLL hacks; GPU raster; windowing/swapchain.

**Target platform (PLAN.md):** Windows x64 primary (TommyLaptop) for HW RT wall-clock; Linux CI for CPU + soft-skip GPU/RT fixtures when no device / no RT extensions.

**Amendment vs M7:** M7 §1.2 listed RTX / `VK_KHR_ray_query` / hardware ray-tracing pipelines as **out of scope**. M9 **reopens** hardware RT as **in scope**. M7 compute backend (`PATH_BACKEND_VULKAN=1`) remains required and unchanged.

---

## 1. Scope and non-goals

### 1.1 In scope (M9 MUST implement)

- Optional **hardware ray-tracing** path for `Renderer.renderPath` that uses Vulkan acceleration structures (BLAS/TLAS) and ray queries / ray-tracing dispatch to accelerate closest-hit and any-hit (shadow) tests of the M4 integrator (§5–§6).
- Explicit opt-in via extended path-backend API (§3): `PATH_BACKEND_VULKAN_RT=2` + `isVulkanRayTracingAvailable()`; amend trap **118** valid set to `{0, 1, 2}`.
- Fallback chain: HW RT fail → M7/M8 compute Vulkan (if available) → CPU (§4); STRICT and FORCE_FAIL hooks (§4.3–§4.4).
- Compatibility with M8 RR / MIS / adaptive: when enabled, they MUST apply on the HW RT backend the same way as on compute (§7).
- Image contract: CPU SHA; compute `# png_tol: 2`; HW RT `# png_tol: 2` vs CPU/M4 golden (§8).
- Wall-clock ACs on silver still 1920×1080 @ 256 spp on TommyLaptop with HW RT (§9).
- Size / DCE budgets including RT-linked soft budget (§10).
- Traps: amend 118; STRICT reuses **117** for selected-backend GPU failure (§11).
- Regression: M4 / M7 / M8 fixtures still pass under CPU and compute backends (§12).
- Fixtures under `spec/tests/M9/` (§15–§16); harness `# requires_vulkan_rt:` soft-skip (§14).

### 1.2 Explicit non-goals

| Not in M9 | Note |
|-----------|------|
| Denoise as a product feature | OUT (OQ-M9-07 A) |
| DLSS / frame generation / temporal upscaling | OUT |
| Swapper-style DLL hacks / injecting into other apps | OUT |
| GPU raster / accelerating M3 `render()` | Still CPU-only (M7 §1.2) |
| Windowing / swapchain / interactive viewers | still `savePNG` only |
| OptiX (NVIDIA-only) as the primary stack | OUT (OQ-M9-02 A: Vulkan only) |
| Breaking `renderPath` / M7 compute backend / M8 defaults | Forbidden |
| Changing silver-still artwork composition | Artwork locked (M8) |
| Forcing lower default spp for artwork quality | 256 spp remains allowed / preferred |
| Replacing M4 goldens by default | Compat / default modes preserve SHA on CPU |
| Editing M1–M8 milestone markdown bodies | amendments in §19 |
| Requiring HW RT for Linux CI green | soft-skip RT fixtures |
| New E0xxx for wrong backend ints | runtime trap 118 only |

### 1.3 Design constraints

- M4 CPU goldens and `path_ref.py` remain authoritative under **compat / default** modes.
- M7 fallback / STRICT / FORCE_FAIL / traps 117–118 / DCE spirit remain binding, with trap **118** amended to allow backend `2`.
- M8 RR / sampling / adaptive defaults and trap **119** remain binding.
- Hello ≤ 20 KiB; unused GPU/RT MUST NOT grow hello or pull Vulkan / RT symbols.
- CPU glass+mirror ≤ 300 KiB; compute-only GPU-linked ≤ 400 KiB (M7); RT-linked ≤ **500 KiB** (OQ-M9-06 A).
- Closest-hit MUST match M4 §10.2 rule within image tolerance; shadow rays remain binary visibility (any-hit; occluder identity does not matter).
- Pixel values under compat mode MUST NOT depend on thread scheduling (M4 determinism) on CPU.

---

## 2. Relationship to M4, M7, and M8

### 2.1 M4 integrator baseline (unchanged under compat)

From `spec/M4-ray.md` FINAL (normative reminder):

- Forward path tracer; under M8 defaults RR is OFF — paths run to `maxBounces`.
- NEE for Ambient (first non-delta), Point, Directional, RectAreaLight; at most one indirect ray per hit.
- Closest-hit rule §10.2: same hit as scanning visible meshes in pre-order / triangle index order; ties keep earlier `(meshIndex, triangleIndex)`.
- Shadow rays: any-hit; binary visibility; which occluder is found does not matter.
- Deterministic Hammersley; pixel values MUST NOT depend on thread scheduling on CPU.
- Defaults: `samples=1`, `maxBounces=4`.

### 2.2 M7 compute backend (unchanged; RT reopens non-goal)

From `spec/M7-vulkan-path.md` FINAL:

- `PATH_BACKEND_CPU=0`, `PATH_BACKEND_VULKAN=1`; `setPathBackend` / `getPathBackend` / `isVulkanPathAvailable`.
- Trap **118** was: backend other than `0` or `1` → invalid. **M9 amends** valid set to `{0, 1, 2}` (§3, §11, §19).
- `FARMOS_PATH_GPU_STRICT=1` → trap 117; default silent CPU fallback; backend stays selected GPU id.
- `# png_tol: 2`, `# requires_vulkan` soft-skip; GPU-linked ≤ 400 KiB; hello ≤ 20 KiB.
- M7 §1.2 non-goal “RTX / `VK_KHR_ray_query` / hardware ray-tracing pipelines” is **superseded for M9 scope**: those techniques become the M9 backend. M7 compute (`backend=1`) remains required and is not removed.

### 2.3 M8 speed features (apply on HW RT when enabled)

From `spec/M8-path-speed.md` FINAL:

- RR off by default; `setPathRoulette` / `FARMOS_PATH_ROULETTE=1`.
- `setPathSampling` default `PATH_SAMPLING_M4`; MIS via `PATH_SAMPLING_MIS`; trap **119**.
- Additive adaptive/progressive API; `renderPath` default unchanged.
- Speedup targets vs M7 baseline (≥2× Vulkan / ≥1.5× Linux CPU) remain M8 ACs; **M9 targets are stricter / new baseline** against post-M8 compute (§9).

### 2.4 What M9 adds

M9 adds backend id **`PATH_BACKEND_VULKAN_RT = 2`**: hardware-accelerated ray traversal for the same M4 integrator (plus M8 optional estimator flags). With all M8 speed features at defaults and backend CPU:

- CPU `renderPath` MUST remain **bit-identical** to M4.
- Backend `1` (compute) MUST still satisfy M7/M8 contracts.
- Backend `2` (HW RT) MUST satisfy §8 image contract and §9 wall-clock when HW RT is available.

---

## 3. API (OQ-M9-01 A)

`farmos:scene` MUST additionally export (extending M7):

```
const PATH_BACKEND_CPU: int = 0
const PATH_BACKEND_VULKAN: int = 1
const PATH_BACKEND_VULKAN_RT: int = 2

// On Renderer (same class as M3/M4/M7/M8):
setPathBackend(backend: int): void
getPathBackend(): int
isVulkanPathAvailable(): bool
isVulkanRayTracingAvailable(): bool
```

`renderPath(scene, camera)` signature is **unchanged**. There is **no** separate `renderPathRayTracing` method under option A.

### 3.1 Defaults

- A newly constructed `Renderer` has path backend `PATH_BACKEND_CPU` (0).
- GPU / HW RT MUST NOT be used until the program calls `setPathBackend` with `1` or `2`.
- `getPathBackend()` returns the last successfully stored backend (`0`, `1`, or `2`). After a failed HW RT attempt with silent downgrade, the stored backend MUST remain `PATH_BACKEND_VULKAN_RT` so a later `renderPath` retries HW RT (§4.2).

### 3.2 `setPathBackend` (amended trap 118)

1. If `backend` is not in `{0, 1, 2}` → trap **118** `runtime error: invalid path backend` **at `setPathBackend`** (not deferred to `renderPath`).
2. Otherwise store the backend on this renderer. Does not by itself load Vulkan, build acceleration structures, allocate GPU memory, or change display / accumulator buffers.
3. Switching among CPU / compute / HW RT MUST NOT require destroying the renderer.

### 3.3 `isVulkanPathAvailable` (unchanged meaning)

- Returns `true` iff this process has successfully probed a usable Vulkan device for the **M7/M8 compute** path (instance + physical device + compute queue).
- Unchanged from M7. HW RT availability is a **separate** query (§3.4).

### 3.4 `isVulkanRayTracingAvailable` (new)

- Returns `true` iff this process has successfully probed a usable Vulkan device that supports the M9 hardware RT stack (OQ-M9-02 A + OQ-M9-03 A): required ray-tracing / acceleration-structure extensions and a queue suitable for building AS + dispatching ray queries via `VK_KHR_ray_query` in compute.
- Probe MAY be lazy (first call to `isVulkanRayTracingAvailable`, or first `renderPath` with HW RT selected).
- Once `true`, it SHOULD stay `true` for the process lifetime unless the implementation documents otherwise.
- Returning `false` MUST NOT by itself trap. A `false` result with backend `PATH_BACKEND_VULKAN_RT` causes the fallback chain on `renderPath` (unless STRICT, §4.3).
- `isVulkanRayTracingAvailable() == true` SHOULD imply `isVulkanPathAvailable() == true` on the same device when compute fallback is possible; implementations MAY still report compute available when RT is not (common on older GPUs).

### 3.5 Decision note (OQ-M9-01 A)

Backend enum extended with `PATH_BACKEND_VULKAN_RT=2`; trap 118 valid set `{0,1,2}`. Matches M7 pattern; one `getPathBackend` for harness / STRICT; clear fallback identity. Separate RT-only methods without a new backend id were rejected.

---

## 4. Availability and fallback (OQ-M9-05 A)

### 4.1 When each backend runs

On `renderPath`, after M4 §4.2 steps 1–4 succeed (disposed / size / samples / bounces checks):

1. If `getPathBackend() == PATH_BACKEND_CPU` → M4 CPU path tracer. MUST NOT load Vulkan for this call.
2. If `getPathBackend() == PATH_BACKEND_VULKAN` → M7/M8 compute path; on failure apply M7 §4.2 / §4.3 (unchanged).
3. If `getPathBackend() == PATH_BACKEND_VULKAN_RT`:
   - Attempt **HW RT** path (§5–§6).
   - On success: accumulate / quantize per §8 and return.
   - On failure (no RT extensions, AS build fail, device lost, shader fail, forced-fail env, etc.): apply §4.2 or §4.3.

M4 steps 5–7 (matrix update, mesh walk, traps 105/111) MUST still run for GPU/RT attempts. Invalid scenes MUST trap with the same codes as CPU before or during upload.

### 4.2 Default silent downgrade chain (soft)

When `FARMOS_PATH_GPU_STRICT` is unset or not exactly `1`, and backend is `PATH_BACKEND_VULKAN_RT`:

1. Try HW RT.
2. If HW RT fails **and** compute Vulkan is available (`isVulkanPathAvailable()` / compute path can run): **silently** run the M7/M8 **compute** integrator for **that** `renderPath` call.
3. If compute also fails or is unavailable: **silently** fall back to the M4 **CPU** integrator for that call.
4. Exit code MUST be `0` if the eventual path (compute or CPU) succeeds.
5. The stored backend MUST remain `PATH_BACKEND_VULKAN_RT` (retry HW RT next call).
6. Stderr MUST NOT be required; prefer no stderr on soft downgrade (same spirit as M7 §4.2).

### 4.3 STRICT mode (OQ-M9-05 A — selected HW RT must succeed)

Environment variable **`FARMOS_PATH_GPU_STRICT`**:

- If the value is exactly `1`, and `getPathBackend() == PATH_BACKEND_VULKAN_RT`, then a failed **HW RT** attempt at `renderPath` MUST trap with exit **117** and stderr exactly:
  ```
  runtime error: vulkan path failed
  ```
  (same trailing-newline convention as M3/M4/M7 traps).
- Under STRICT, M9 MUST **NOT** silently downgrade HW RT → compute → CPU. The **selected** backend’s hardware path must succeed, or trap 117.
- Rationale: fixtures that assert HW RT pixels / performance MUST not pass via silent compute downgrade.
- For `PATH_BACKEND_VULKAN` (compute), STRICT remains M7: compute fail → 117 (no CPU fallback).
- Unset / empty / any value other than `1` → §4.2 soft chain.
- STRICT does not affect `PATH_BACKEND_CPU` calls.

### 4.4 Forced-fail test hooks

| Variable | Value `1` means |
|----------|-----------------|
| `FARMOS_VULKAN_FORCE_FAIL` | Any `renderPath` with **compute** Vulkan selected (`backend=1`) MUST treat the compute GPU path as failed (M7). For `backend=2`, implementations MUST treat this as forcing **HW RT** failure as well (so STRICT+FORCE_FAIL → 117; soft → downgrade chain). |
| `FARMOS_VULKAN_RT_FORCE_FAIL` | (M9) If exactly `1`, any `renderPath` with `PATH_BACKEND_VULKAN_RT` selected MUST treat the **HW RT** path as failed, without necessarily forcing compute failure. Soft mode then MAY still succeed via compute (§4.2 step 2). STRICT → trap 117. |

Combined with STRICT → trap 117 (fixture `003`). Soft + RT force-fail → compute or CPU success (fixture `002` spirit).

### 4.5 Dynamic load requirement

- A program that never selects backend `1` or `2` MUST NOT require Vulkan at **load time**.
- First Vulkan / RT attempt MUST use dynamic loading of the Vulkan loader.
- Programs that never reference `PATH_BACKEND_VULKAN`, `PATH_BACKEND_VULKAN_RT`, `setPathBackend` with non-CPU, `isVulkanPathAvailable`, or `isVulkanRayTracingAvailable` in a reachable way SHOULD avoid linking Vulkan/RT glue when DCE can prove it unused (§10).

---

## 5. Acceleration structures

### 5.1 Structure

Under OQ-M9-02 A (Vulkan AS/RT only):

- Build **bottom-level** acceleration structures (BLAS) for triangle geometry of visible `Mesh` objects participating in path tracing.
- Build a **top-level** acceleration structure (TLAS) over instances with current `matrixWorld` transforms.
- Implementation MAY use `VkAccelerationStructureKHR` (or equivalent Vulkan AS API available with the chosen extensions).

### 5.2 Rebuild policy

| Event | Requirement |
|-------|-------------|
| First HW RT `renderPath` after backend select | MUST build AS if not present |
| Scene graph / mesh / material / light dirty relative to last AS build | MUST rebuild or refit enough for correct hits (full rebuild is always acceptable) |
| Every `renderPath` with no dirty bits | SHOULD skip full rebuild (reuse AS); MAY rebuild (correctness over speed) |
| Explicit API | M9 does **not** require a public `rebuildAccel()`; implementations MAY add a non-normative internal hook |

Dirty detection is implementation-defined but MUST be sound: a missed dirty bit that changes closest-hit vs M4 §10.2 within tolerance is a bug.

### 5.3 Closest-hit semantics

- Primary / bounce rays MUST return a closest hit consistent with M4 §10.2 within the image contract (§8): same hit as pre-order mesh scan + triangle index order with strict-`t` ties broken toward earlier indices, **or** an implementation-defined AS traversal that is proven / fixture-validated to produce images within `# png_tol`.
- When fixtures demand CPU SHA (compat CPU), AS is irrelevant (CPU path). When asserting HW RT pixels vs CPU golden with `# png_tol`, float32 + AS ordering deltas are covered by tolerance.

### 5.4 Any-hit / shadow semantics

- Shadow / visibility rays MUST remain **binary**: occluded or not.
- Which occluder is found MUST NOT affect the estimator (M4).
- Opaque shadows only as in M4 (no new alpha-any-hit materials in M9).

### 5.5 CPU / compute BVH relationship

- CPU continues to use the M4 BVH (or equivalent) for backend `0`.
- Compute backend `1` continues to use its M7/M8 GPU BVH / buffers.
- HW RT backend `2` uses Vulkan AS. Implementations MAY share triangle upload buffers between compute and RT; not required.

---

## 6. Vulkan RT requirements (OQ-M9-02 / OQ-M9-03 A)

### 6.1 Stack (OQ-M9-02 A)

- **Primary:** Vulkan ray-tracing extensions + acceleration structures on Windows x64 (TommyLaptop).
- Linux: same code path when the device exposes the required extensions; otherwise soft-skip RT fixtures.
- **OptiX:** OUT (OQ-M9-02 A).

### 6.2 Pipeline style (OQ-M9-03 A)

**Binding (OQ-M9-03 A): `VK_KHR_ray_query` inside compute shaders** (closer to M7 compute architecture):

- Keep a compute dispatch model similar to M7/M8.
- Trace via rayQuery against the TLAS for closest-hit and shadow tests.
- Integrator math (sampling indices, NEE, BSDF, RR/MIS when enabled) stays in compute shader code paths shared conceptually with M7/M8.

Full `VK_KHR_ray_tracing_pipeline` (raygen / closest-hit / miss / any-hit SBT) is **out** of M9 (OQ-M9-03 A).

### 6.3 Minimum capability expectations

Implementations SHOULD document the exact extension set. A typical A stack includes:

- Acceleration structure KHR (or merged Vulkan 1.3+ features where applicable)
- `VK_KHR_ray_query`
- Deferred host operations / buffer device address as required by the AS build path
- A compute queue that can wait on AS build completion

Exact minimum Vulkan version / extension list is implementation-documented; eggtooth checks **behavior** (fixtures + benches), not a fixed extension string table, unless PM later hardens that list.

### 6.4 Accumulator precision

- **CPU:** float64 RGB accumulator (M4).
- **Compute / HW RT GPU:** float32 accumulator allowed (M7 spirit); final mean then quantized. Primary source of CPU↔GPU byte deltas covered by `# png_tol`.

### 6.5 No presentation

No windowing, no swapchain, no present in M9 (same as M7/M8).

---

## 7. M8 feature compatibility

When `getPathBackend() == PATH_BACKEND_VULKAN_RT`:

| Feature | Requirement |
|---------|-------------|
| `setPathRoulette` / RR | MUST apply the same policy as CPU/compute when enabled; default OFF |
| `setPathSampling` / MIS | MUST apply the same mode; default `PATH_SAMPLING_M4`; invalid → trap **119** at `setPathSampling` (backend-independent) |
| Adaptive spp API | Additive; default OFF; when OFF, `renderPath` adds exactly `samples` spp |
| Compat defaults (RR off, sampling M4, adaptive off) | HW RT images vs CPU/M4 golden use `# png_tol` (§8); CPU SHA still required on backend CPU |

Enabling RR / MIS / adaptive MUST NOT change `setPathBackend` / `getPathBackend` contracts.

---

## 8. Image contract

### 8.1 CPU backend

With `PATH_BACKEND_CPU`, every `run_png` fixture that mirrors an M4 scene MUST be **bit-identical** to the corresponding M4 golden (SHA-256).

### 8.2 Compute Vulkan backend (backend 1)

Unchanged from M7/M8: successful GPU → `# png_tol: 2` vs CPU/M4 golden when asserting GPU pixels; fallback → CPU contract.

### 8.3 Hardware RT backend (backend 2)

When a fixture actually ran on HW RT:

1. Compare output PNG to the **same** M4 / CPU golden using `# png_tol: 2` (same as compute). Every output byte may differ from the reference by at most `2` absolute per channel (8-bit RGB canonical PNG).
2. If AS / float32 deltas prove systematically larger on TommyLaptop during implementation, poteto MAY propose `# png_tol: 3` via PM amendment; binding start is **2**.
3. Alternative: separate HW RT golden with `# sha256:` of that image — prefer (1).

Eggtooth MUST NOT demand bit-identity between CPU and successful HW RT images.

### 8.4 Fallback / downgrade images

When HW RT was selected but the call downgraded to compute or CPU under soft mode:

- Final image MUST match the **path that actually ran** (compute tol or CPU SHA).
- Fixtures that intend to assert **HW RT** pixels MUST use `# requires_vulkan_rt: true` and STRICT (or an availability check + force hooks) so CI does not silently accept compute/CPU as an RT pass.

### 8.5 Quantization

Unchanged: clamp to [0,1], then `floor(c * 255.0 + 0.5)`. No gamma, no tonemap. Denoise OUT.

---

## 9. Benchmarks (silver still) (OQ-M9-08 A binding; OQ-M9-04 A superseded)

### 9.1 Normative bench program

- **Composition target (MUST NOT change):** `out/artwork_silver_still.png`.
- **Normative program:** `examples/artwork_silver_still.fm` @ **1920×1080**, `setSamples(256)`, max bounces as in the artwork program (M8).
- Backend measured for M9 primary AC: TommyLaptop **`PATH_BACKEND_VULKAN_RT`**, successful HW RT path (not soft downgrade).
- Secondary comparison baseline: post-M8 **compute** Vulkan (`PATH_BACKEND_VULKAN`) on the same host / same scene / same spp (the ~32 s pain point).

### 9.2 Method

- Wall-clock: **median of 3 timed runs after 1 warm-up** for one `renderPath` (steady-state; AS already built / reused).
- Also record cold first-call (includes AS build) in notes — not the primary gate.
- Record: host id, backend, RR/MIS/adaptive flags, spp, resolution, elapsed ms, whether HW RT or downgrade ran.

### 9.3 Targets (**binding OQ-M9-08 A**)

| Gate | Target |
|------|--------|
| **Primary** | Silver still 1920×1080 @ 256 spp on TommyLaptop with HW RT: median ≤ **5000 ms** (5 s) after 1 warm-up |
| **Secondary** | Same setup: ≥ **5×** wall-clock vs post-M8 compute baseline when that baseline is measurable on the same host |

**Binding under OQ-M9-08 A** (PM 2026-10-18). Prior **OQ-M9-04 A** (≤ **4 s** / ≥ **8×**) is **superseded**.

**Measured floor (PM/user 2026-10-18):** TommyLaptop Arc CLEAN tip `eaeb0c6`: RT median **4881 ms** / compute ~**29335 ms** / ~**6.01×**. Under OQ-M9-08 A, that result **PASSES**. Original ≤4000 ms / ≥8× gates were not reachable with current levers without regress.

Both SHOULD be reported. AC-M9 primary wall-clock gate is the **≤ 5000 ms** median; secondary ≥5× is also an AC when baseline data exists (if compute baseline cannot be measured in the same session, document why and still enforce ≤ 5000 ms).

Context: post-M8 compute ~29–32 s class at 1080p@256spp; ≤5 s / ≥5× keeps an order-of-magnitude step toward interactive/seconds without lowering spp, aligned to the measured Arc floor.

### 9.4 Quality during bench

- Artwork spp stays 256 unless PM explicitly changes product quality.
- Visual composition MUST remain the silver-still lock (M8).
- M8 speed flags MAY be enabled for the RT bench (RR on / MIS) as documented in bench notes; a **compat** RT lane (defaults) SHOULD also be timed for apples-to-apples vs compute defaults.

---

## 10. Size / DCE (OQ-M9-06 A)

| Program shape | Budget | Constraint |
|---------------|--------|------------|
| Hello (no path / no GPU / no RT) | ≤ **20 KiB** stripped | MUST NOT import Vulkan loader / RT symbols |
| CPU glass+mirror `renderPath` without GPU/RT opt-in | ≤ **300 KiB** | MUST NOT require Vulkan at load |
| Compute-only GPU-linked demo (backend 1 reachable, RT unused) | ≤ **400 KiB** | Dynamic load; M7 budget preserved |
| RT-linked demo (backend 2 / `isVulkanRayTracingAvailable` reachable) | ≤ **500 KiB** stripped | Dynamic load; OQ-M9-06 A |

M9 RT code MUST be reachable only from path-tracing / GPU/RT units so hello DCE still holds.

---

## 11. Runtime traps

| Exit | Stderr | When |
|------|--------|------|
| **117** | `runtime error: vulkan path failed` | STRICT + selected GPU backend failed (`backend=1` compute fail per M7; `backend=2` HW RT fail per §4.3 — no silent downgrade) |
| **118** | `runtime error: invalid path backend` | `setPathBackend` with value **not** in `{0, 1, 2}` (**amended** from M7 `{0, 1}`) |
| **119** | `runtime error: invalid path sampling mode` | `setPathSampling` unsupported mode (M8; unchanged) |

Existing M4 traps (105, 107, 108, 111, 112, 113) unchanged. Exit **109** still unused. No new trap number for RT-specific STRICT (reuse 117).

---

## 12. Regression

- All M4 / M7 / M8 fixtures that select CPU or compute backends MUST continue to pass under M9.
- M7 fixture that trapped on `setPathBackend(2)` (**old** invalid) MUST be **updated or superseded**: under M9, `2` is valid (`PATH_BACKEND_VULKAN_RT`). Invalid trap fixtures MUST use backend **`3`** (or any value ∉ `{0,1,2}`) — see M9 fixture `004`.
- M8 compat SHA lane and trap 119 remain.

---

## 13. Acceptance criteria

| ID | Criterion |
|----|-----------|
| AC-M9-01 | CPU compat: M4 twin background (and Cornell-class if present) remain **bit-identical** SHA under `PATH_BACKEND_CPU`. |
| AC-M9-02 | Compute backend `1` still works: successful GPU `# png_tol: 2`; M7 STRICT/FORCE_FAIL/fallback unchanged for backend 1. |
| AC-M9-03 | `PATH_BACKEND_VULKAN_RT=2` / `isVulkanRayTracingAvailable` round-trip; default backend remains CPU. |
| AC-M9-04 | `setPathBackend(3)` (or other ∉ `{0,1,2}`) → trap **118** at `setPathBackend`. |
| AC-M9-05 | Soft fallback: HW RT fail → compute (if available) → CPU; stored backend remains `2`; exit 0 on success. |
| AC-M9-06 | STRICT + HW RT selected + HW RT fail → trap **117**; MUST NOT silent-downgrade under STRICT. |
| AC-M9-07 | M8 RR / MIS / adaptive apply on backend 2 when enabled; defaults preserve compat estimator on CPU. |
| AC-M9-08 | Successful HW RT images satisfy `# png_tol: 2` (or later PM-amended 3) vs CPU/M4 golden when `# requires_vulkan_rt: true`. |
| AC-M9-09 | AS build/rebuild policy §5 sound for fixture scenes; closest-hit / shadow semantics §5.3–§5.4. |
| AC-M9-10 | Hello ≤ 20 KiB; no Vulkan/RT symbols when unused; CPU glass+mirror ≤ 300 KiB; compute-linked ≤ 400 KiB; RT-linked ≤ **500 KiB**. |
| AC-M9-11 | Denoise not required; no denoise AC (OQ-M9-07 A binding). |
| AC-M9-12 | Bench harness for `examples/artwork_silver_still.fm` @ 1920×1080 / 256 spp on TommyLaptop HW RT. |
| AC-M9-13 | Primary wall-clock: median ≤ **5000 ms** after 1 warm-up (**binding OQ-M9-08 A**). Prior OQ-M9-04 A ≤4 s **superseded**. |
| AC-M9-14 | Secondary: ≥ **5×** vs post-M8 compute baseline when measurable (**binding OQ-M9-08 A**). Prior OQ-M9-04 A ≥8× **superseded**. |
| AC-M9-15 | `# requires_vulkan_rt:` soft-skip on hosts without HW RT; Linux CI green without RT device. |
| AC-M9-16 | Fixtures in §16 pass under their kinds; M4/M7/M8 regression under CPU/compute. |
| AC-M9-17 | No OptiX (OQ-M9-02 A); no DLSS/frame-gen/Swapper; no GPU raster. |

AC-M9-10, AC-M9-12..14 are eggtooth / script / bench checks.

---

## 14. Test harness

Reuse M4 / M7 / M8 kinds: `run`, `run_png`, `runtime_trap`.

| Header | Meaning |
|--------|---------|
| `# requires_vulkan: true` | Soft-skip without Vulkan **compute** device (M7) |
| `# requires_vulkan_rt: true` | Soft-skip when `isVulkanRayTracingAvailable()` would be false / no HW RT device (M9) |
| `# png_tol: <int>` | Per-channel byte tolerance vs `# sha256:` reference |
| `# env: KEY=VALUE` | Child env (`FARMOS_PATH_GPU_STRICT`, `FARMOS_VULKAN_FORCE_FAIL`, `FARMOS_VULKAN_RT_FORCE_FAIL`, M8 vars, …) |
| `# path_mode: compat` | Documentation aid (M8) |

Soft-skip for `# requires_vulkan_rt: true` when no HW RT:

1. Do **not** fail the fixture.
2. Record skip in the harness log.
3. Exit 0 from the test runner for that case.

Working directory for M9 fixtures: `spec/tests/M9`.

PLACEHOLDER SHA:

```
# sha256: 0000000000000000000000000000000000000000000000000000000000000000
```

---

## 15. Fixture outline

| Fixture | Kind | What |
|---------|------|------|
| 001_cpu_compat_background | run_png | Compat CPU; M4-001 twin SHA |
| 002_rt_availability_fallback | run | Select backend 2; soft fallback OK; print backend + `isVulkanRayTracingAvailable` |
| 003_strict_rt_force_fail | runtime_trap | STRICT + `FARMOS_VULKAN_RT_FORCE_FAIL=1` + backend 2 → 117 |
| 004_trap_invalid_backend | runtime_trap | `setPathBackend(3)` → 118 |
| 005_compute_backend_still_works | run | `setPathBackend(PATH_BACKEND_VULKAN)`; round-trip backend 1; exit 0 (soft OK without device) |
| 006_rt_png_tol | run_png | Optional; `# requires_vulkan_rt`, `# png_tol: 2`, small scene |
| bench / artwork | script | silver still wall-clock; see README |

**Counts (stub pack):** 5 `.fm` harness fixtures (001–005) + README + `_ref` + bench note. Expand as PM requests.

---

## 16. Test index (source files)

| # | File | Kind |
|---|------|------|
| 001 | `001_cpu_compat_background.fm` | run_png |
| 002 | `002_rt_availability_fallback.fm` | run |
| 003 | `003_strict_rt_force_fail.fm` | runtime_trap |
| 004 | `004_trap_invalid_backend.fm` | runtime_trap |
| 005 | `005_compute_backend_still_works.fm` | run |

Eggtooth / bench rows (no required `.fm` in stub pack): size/DCE (AC-M9-10), silver-still HW RT bench (AC-M9-12..14).

---

## 17. Open questions

**None.** OQ-M9-01 .. OQ-M9-08 are all **accepted A** (see §18, including OQ-M9-08 A). Binding reminders: M7 ray-query / HW RT non-goal reopened as in-scope for M9; Vulkan AS/RT only (not OptiX); `VK_KHR_ray_query` inside compute; soft fallback RT→compute→CPU with STRICT trap 117 (no silent downgrade under STRICT); wall-clock ≤ **5000 ms** / ≥ **5×** (OQ-M9-08 A; OQ-M9-04 superseded); DLSS/frame-gen/Swapper OUT; denoise OUT; size budgets; CPU + compute fallback + DCE required; artwork composition locked; 256 spp quality OK.

---

## 18. Resolved decisions

PM 2026-10-16 accepted option A for OQ-M9-01..07; PM 2026-10-18 accepted option A for OQ-M9-08. Binding product intent and implementation locks follow.

| ID | Decision |
|----|----------|
| OQ-M9-01 | `PATH_BACKEND_VULKAN_RT=2` + `isVulkanRayTracingAvailable()`; amend trap **118** valid set to `{0,1,2}`. |
| OQ-M9-02 | Vulkan AS/RT only (**not** OptiX). |
| OQ-M9-03 | `VK_KHR_ray_query` inside compute (not full ray-tracing pipeline). |
| OQ-M9-04 | Silver still 1920×1080 @ 256 spp TommyLaptop HW RT: median ≤ **4 s** after warm-up (**primary**) + ≥ **8×** vs post-M8 compute when measurable (**secondary**). **Historical** — **superseded by OQ-M9-08 A** (measured floor `eaeb0c6` 4881 ms / ~6.01×; ≤4 s / ≥8× unreachable without regress). |
| OQ-M9-05 | Soft fallback RT→compute Vulkan→CPU; STRICT: selected HW RT must succeed or trap **117** (no silent downgrade under STRICT). |
| OQ-M9-06 | RT-linked ≤ **500 KiB** stripped; hello ≤ 20; compute ≤ 400; CPU glass+mirror ≤ 300. |
| OQ-M9-07 | Denoise still **OUT**. |
| OQ-M9-08 | **Accepted A** (PM 2026-10-18): primary AC silver still 1920×1080 @ 256 spp TommyLaptop HW RT median ≤ **5000 ms** after warm-up; secondary ≥ **5×** vs post-M8 compute when measurable. Measured floor `eaeb0c6` (4881 ms / 6.01×) **PASSES**. Supersedes OQ-M9-04 A (≤4 s / ≥8×). Denoise / size / STRICT / API / stack unchanged. |
| RD-M9-01 | M7 §1.2 ray-query / HW RT non-goal **reopened** as in-scope for M9 backend 2; M7 compute backend 1 remains. |
| RD-M9-02 | DLSS / frame-gen / Swapper-style DLL hacks **OUT**. |
| RD-M9-03 | CPU + M7/M8 compute Vulkan fallback + DCE (hello ≤ 20 KiB unused) **required**. |
| RD-M9-04 | Artwork composition locked; 256 spp quality OK; speed from HW RT, not forced lower spp. |
| RD-M9-05 | HW RT image contract starts at `# png_tol: 2` vs CPU/M4 golden. |
| RD-M9-06 | Env `FARMOS_VULKAN_RT_FORCE_FAIL`; `FARMOS_VULKAN_FORCE_FAIL` also forces HW RT fail when backend 2. |
| RD-M9-07 | Harness `# requires_vulkan_rt:` soft-skip on hosts without HW RT. |
| RD-M9-08 | Invalid-backend trap fixtures use `3` (not `2`); `2` is valid. |
| RD-M9-09 | OQ-M9-01..07 accepted option A (PM 2026-10-16). |
| RD-M9-10 | **Accepted:** OQ-M9-08 A (PM 2026-10-18). Binding wall-clock ≤ **5000 ms** / ≥ **5×**. Body §9 / AC-M9-13..14 binding under A. Denoise / size / STRICT / API / stack unchanged. |

---

## 19. Amendments relative to M4 / M7 / M8

M4-ray.md, M7-vulkan-path.md, and M8-path-speed.md are not edited.

| # | Amendment |
|---|-----------|
| 1 | Reopen M7 §1.2 non-goal: hardware RT / `VK_KHR_ray_query` (and AS) become **in scope** for M9 backend 2; M7 compute backend 1 remains. |
| 2 | `farmos:scene` exports `PATH_BACKEND_VULKAN_RT = 2` and `isVulkanRayTracingAvailable()`. |
| 3 | Trap **118** valid backends become `{0, 1, 2}` (was `{0, 1}` in M7). |
| 4 | Soft fallback chain for backend 2: HW RT → compute → CPU; STRICT: selected HW RT must succeed or 117. |
| 5 | Env `FARMOS_VULKAN_RT_FORCE_FAIL`; `FARMOS_VULKAN_FORCE_FAIL` also forces HW RT fail when backend 2. |
| 6 | Harness `# requires_vulkan_rt:` soft-skip. |
| 7 | HW RT image `# png_tol: 2` vs CPU/M4 golden. |
| 8 | AS BLAS/TLAS build/rebuild policy (§5). |
| 9 | Wall-clock: binding ≤ **5000 ms** median + ≥ **5×** vs post-M8 compute (**OQ-M9-08 A**); prior OQ-M9-04 A ≤4 s / ≥8× **superseded** (`eaeb0c6` floor 4881 ms / ~6.01× PASSES). |
| 10 | RT-linked size ≤ 500 KiB (OQ-M9-06 A). |
| 11 | M8 RR / MIS / adaptive MUST apply on backend 2 when enabled. |
| 12 | Fixtures under `spec/tests/M9/`; invalid-backend trap uses `3` not `2`. |
| 13 | Denoise / DLSS / Swapper / GPU raster remain out. |

---

## Appendix A — Compat background twin (001)

Same shape as `spec/tests/M4/001_background_only.fm` / M7-001 / M8-001 (16×16, clear `0x4488ff`). Leave backend CPU; RR / sampling / adaptive at defaults.

## Appendix B — Invalid backend trap (004)

```
renderer.setPathBackend(3); // trap 118 — 2 is valid in M9
```

## Appendix C — STRICT RT force-fail (003)

```
# env: FARMOS_PATH_GPU_STRICT=1
# env: FARMOS_VULKAN_RT_FORCE_FAIL=1
renderer.setPathBackend(PATH_BACKEND_VULKAN_RT);
renderer.renderPath(scene, camera); // → exit 117
```

## Appendix D — Artwork bench note

Use `examples/artwork_silver_still.fm` @ 1920×1080 / 256 spp with `setPathBackend(PATH_BACKEND_VULKAN_RT)`. Do not alter composition. Record post-M8 compute baseline (~29–32 s class) for the **5×** secondary gate (**binding OQ-M9-08 A**; was 8× under superseded OQ-M9-04 A). Primary gate ≤ **5 s** (5000 ms) after warm-up.

---

## Document history

- 2026-10-16: DRAFT. Hardware RT for M4 path (prefer Vulkan AS + rayQuery); CPU + M7/M8 compute fallback; reopen M7 ray-query non-goal; OQ-M9-01..07 recommend A; denoise OUT; silver-still ≤4 s / ≥8×; RT-linked ≤500 KiB; fixtures under `spec/tests/M9/`.
- 2026-10-16: FINAL. PM accepted OQ-M9-01 .. OQ-M9-07, all option A. Binding: PATH_BACKEND_VULKAN_RT=2; Vulkan AS/RT + ray_query in compute; soft RT→compute→CPU / STRICT 117; ≤4 s / ≥8× silver still; RT-linked ≤500 KiB; denoise OUT; DLSS/Swapper OUT. Open questions cleared.
- 2026-10-18: DRAFT amendment (OQ-M9-08). Measured TommyLaptop Arc CLEAN tip `eaeb0c6`: RT median 4881 ms / compute ~29335 ms / ~6.01× — original ≤4 s / ≥8× unreachable without regress. Open OQ-M9-08: recommend A ≤5000 ms / ≥5× (would PASS eaeb0c6); B keep 4 s / 8×; C other. Body §9 / AC-M9-13..14 provisional under A. OQ-M9-01..07 / denoise / size / STRICT unchanged. Status not FINAL until OQ-M9-08 locked.
- 2026-10-18: FINAL again. PM accepted OQ-M9-08 option A. Binding wall-clock: primary ≤ **5000 ms** after warm-up; secondary ≥ **5×** vs post-M8 compute. OQ-M9-04 (≤4 s / ≥8×) superseded. Measured floor `eaeb0c6` (4881 ms / 6.01×) PASSES. OQ-M9-01..08 all A. Denoise / size / STRICT / API / stack unchanged. Open questions none.
