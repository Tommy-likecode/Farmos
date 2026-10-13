# Farmos M7 — Optional Vulkan Path Tracing (v1.01)

**Status:** FINAL (PM 2026-10-13: OQ-M7-01 .. OQ-M7-04 all option A)  
**Milestone:** M7 / Farmos v1.01 — optional Vulkan compute path tracer  
**Depends on:** `spec/M1-core.md` (FINAL), `spec/M2-math.md` (FINAL), `spec/M3-scene.md` (FINAL), `spec/M3.5-concurrency.md` (FINAL), `spec/M4-ray.md` (FINAL), `spec/M5-physics.md` (FINAL), `spec/M6-hardening.md` (FINAL)  
**Normative keywords:** MUST / SHOULD / MAY per RFC 2119.  
**Date:** 2026-10-13

This document specifies Farmos v1.01 / M7: an **optional Vulkan compute** backend that accelerates the existing M4 CPU path tracer. It does **not** specify compiler, runtime, or stdlib implementation code. It does **not** edit M1–M6 milestone markdown bodies; normative amendments relative to those milestones live in §18 of this file.

Open questions: none. PM accepted option A for OQ-M7-01 .. OQ-M7-04 on 2026-10-13. Binding confirmations: trap 118 at `setPathBackend`; `FARMOS_VULKAN_FORCE_FAIL=1`; `# requires_vulkan` soft-skip on Linux CI. Those decisions are recorded in §17. The body below is that decision, not a draft.

Related: `/workspace/farmos/PLAN.md` (M7 section), `spec/M4-ray.md`, `spec/M6-hardening.md`, `spec/tests/M7/README.md`.

**Target platform (PLAN.md):** Windows x64 primary (TommyLaptop). Linux CI MAY soft-skip GPU fixtures when no Vulkan device is present; CPU fixtures remain required.

---

## 1. Scope and non-goals

### 1.1 In scope (M7 MUST implement, under option A)

- Optional Vulkan **compute** path tracer that implements the M4 integrator (§6) for `Renderer.renderPath`.
- Explicit GPU opt-in via path-backend API (§3); default remains CPU.
- Silent CPU fallback when Vulkan is unavailable, init fails, or a GPU path error occurs (§4), unless STRICT mode is set (§4.3).
- DCE / size gates so hello and non-GPU programs do not pull Vulkan symbols (§8).
- Version / changelog surfaces that reflect **1.01** (§9).
- Traps **117** (STRICT Vulkan failure) and **118** (invalid path backend) (§10).
- Fixtures under `spec/tests/M7/` (§14, §15).
- Harness extensions: `# requires_vulkan:`, `# png_tol:` (§13).

### 1.2 Explicit non-goals

| Not in M7 | Note |
|-----------|------|
| GPU raster / accelerating M3 `render()` | `render` stays CPU-only (§2) |
| DX12, CUDA, OpenCL, Metal, WebGPU | Vulkan only |
| RTX / `VK_KHR_ray_query` / hardware ray-tracing pipelines | compute path tracer is enough |
| Windowing, swapchain, present, interactive viewers | still `savePNG` only |
| Textures / materials beyond M4 | albedo `Texture` and M4 BSDF only |
| Changing M4 CPU goldens or integrator math | CPU path stays bit-identical to M4 |
| Editing M1–M6 milestone markdown bodies | amendments in §18 |
| New E0xxx diagnostics for wrong backend ints | runtime trap 118 only |
| Requiring a GPU for CI green on Linux | soft-skip GPU fixtures |

### 1.3 Design constraints

- PLAN M7 DECIDED bullets (§17) are binding and are **not** reopened as OQs.
- Hello world ≤ 20 KiB; no Vulkan loader symbols when path/GPU unused (extend M6 DCE spirit).
- Glass+mirror **CPU** demo stripped size remains ≤ 300 KiB (M4 / M6 B-03).
- GPU-linked demo soft budget ≤ **400 KiB** stripped under option A (OQ-M7-04).
- M4 image contract on CPU; GPU may use documented byte tolerance (OQ-M7-03).

---

## 2. Relationship to M4 and M3

- `Renderer.renderPath(scene, camera)` keeps M4 §4 semantics: samples, bounces, accumulator, traps 105/107/111/112/113, BVH closest-hit rule, progressive mean.
- `Renderer.render(scene, camera)` remains the M3 software rasterizer. A program that only calls `render` MUST produce PNGs **byte-identical** to published M3 goldens, even if the same binary also contains path-tracing / Vulkan code paths that are never reached.
- `render` MUST NOT use Vulkan. GPU work in M7 applies only to `renderPath` after an explicit Vulkan backend selection.
- `savePNG` still writes the display buffer left by the most recent `render` or `renderPath` (M4 §4.4).
- `FARMOS_THREADS` still applies to the **CPU** tile pool only (M4 §11). GPU workgroup size is implementation-defined (§6.3).
- M4 `path_ref.py` remains the numeric oracle for **CPU** goldens. GPU fixtures use tolerance or a separate golden (§5).

---

## 3. API (OQ-M7-01 option A)

`farmos:scene` MUST additionally export:

```
const PATH_BACKEND_CPU: int = 0
const PATH_BACKEND_VULKAN: int = 1

// On Renderer (same class as M3/M4):
setPathBackend(backend: int): void
getPathBackend(): int
isVulkanPathAvailable(): bool
```

`renderPath(scene, camera)` is **unchanged** in signature. There is **no** `renderPathVulkan` method under option A.

### 3.1 Defaults

- A newly constructed `Renderer` has path backend `PATH_BACKEND_CPU` (0).
- GPU MUST NOT be used until the program calls `setPathBackend(PATH_BACKEND_VULKAN)` (or `setPathBackend(1)`).
- `getPathBackend()` returns the last successfully stored backend (0 or 1). After a failed GPU attempt with silent fallback, the stored backend MUST remain `PATH_BACKEND_VULKAN` so a later `renderPath` retries GPU (§4.2).

### 3.2 `setPathBackend`

1. If `backend` is neither `0` nor `1` → trap **118** `runtime error: invalid path backend` **at `setPathBackend`** (not deferred to `renderPath`).
2. Otherwise store the backend on this renderer. Does not by itself load Vulkan, allocate GPU memory, or change the display / accumulator buffers.
3. Switching from Vulkan back to CPU MUST NOT require destroying the renderer; subsequent `renderPath` uses the CPU integrator.

### 3.3 `isVulkanPathAvailable`

- Returns `true` if and only if this process has **successfully probed** a usable Vulkan device for path tracing (instance + physical device + compute queue family suitable for the M7 backend).
- Probe MAY be lazy (first call to `isVulkanPathAvailable`, or first `renderPath` with Vulkan selected).
- Once `true`, it SHOULD stay `true` for the process lifetime unless the implementation documents otherwise. Fixtures treat it as sticky for the process.
- Returning `false` MUST NOT by itself trap. A `false` result with backend Vulkan causes CPU fallback on `renderPath` (unless STRICT, §4.3).

### 3.4 Module notes

- No re-export of `farmos:math`. Unknown names still `E0304`. Ordinary type mismatches use existing M1 codes. M7 adds **no** new E0xxx (§11).

---

## 4. Availability and fallback

### 4.1 When Vulkan is attempted

On `renderPath`, after M4 §4.2 steps 1–4 succeed (disposed / size / samples / bounces checks):

1. If `getPathBackend() == PATH_BACKEND_CPU` → run the M4 CPU path tracer exactly as M4. MUST NOT load the Vulkan loader for this call.
2. If `getPathBackend() == PATH_BACKEND_VULKAN`:
   - Attempt GPU path (§6).
   - On success: accumulate / quantize per §5 / §6 and return.
   - On failure (no device, init fail, shader compile fail, device lost, upload fail, forced-fail env, etc.): apply §4.2 or §4.3.

M4 steps 5–7 (matrix update, mesh walk, traps 105/111) MUST still run for GPU attempts. An invalid scene MUST trap with the same codes as CPU before or during upload; implementations MAY validate on CPU before dispatch.

### 4.2 Default silent fallback

When `FARMOS_PATH_GPU_STRICT` is unset or not exactly `1`:

- A failed Vulkan attempt MUST **silently** fall back to the M4 CPU integrator for **that** `renderPath` call.
- Exit code MUST be `0` if the CPU path succeeds.
- The stored backend MUST remain `PATH_BACKEND_VULKAN` (retry next call).
- Stderr MUST NOT be required to print a diagnostic; implementations MAY log to stderr but fixtures that expect empty stderr MUST still pass (prefer no stderr on fallback).

### 4.3 STRICT mode (OQ-M7-02 option A)

Environment variable **`FARMOS_PATH_GPU_STRICT`**:

- If the value is exactly the decimal string `1`, then a failed Vulkan attempt at `renderPath` MUST trap with exit **117** and stderr exactly:
  ```
  runtime error: vulkan path failed
  ```
  (same trailing-newline convention as other M3/M4 traps).
- MUST NOT fall back to CPU in STRICT mode when Vulkan was selected and failed.
- Unset, empty, or any value other than `1` → §4.2 silent fallback.
- STRICT does not affect `PATH_BACKEND_CPU` calls.

### 4.4 Forced-fail test hook

Environment variable **`FARMOS_VULKAN_FORCE_FAIL`**:

- If the value is exactly `1`, any `renderPath` with Vulkan selected MUST treat the GPU path as failed (before or instead of a real dispatch).
- Combined with STRICT → trap 117 (fixture `003`).
- Without STRICT → silent CPU fallback (fixture `012`).
- This hook exists for deterministic CI; product code MAY implement it as a runtime check only when the variable is set.

### 4.5 Dynamic load requirement

- A program that calls `renderPath` but never sets the Vulkan backend MUST NOT require Vulkan at **load time** (no static import of `vulkan-1.dll` / `libvulkan.so.1` required to start the process).
- First Vulkan attempt MUST use dynamic loading (`LoadLibrary` / `dlopen` or equivalent) of the Vulkan loader.
- Programs that never reference `PATH_BACKEND_VULKAN`, `setPathBackend` with a non-CPU value, or `isVulkanPathAvailable` in a reachable way SHOULD avoid even linking stub Vulkan glue when DCE can prove it unused (§8).

---

## 5. Image contract and tolerance (OQ-M7-03 option A)

### 5.1 CPU backend

With `PATH_BACKEND_CPU`, every `run_png` fixture that mirrors an M4 scene MUST be **bit-identical** to the corresponding M4 golden (same SHA-256). Existing M4 goldens and `path_ref.py` remain authoritative.

### 5.2 Vulkan backend (successful GPU)

When a fixture actually ran on GPU:

1. **Preferred:** compare the output PNG to the **same** M4 / CPU golden using harness `# png_tol: N` (default recommendation **N = 2**): every output byte may differ from the reference PNG’s corresponding byte by at most `N` in absolute value (per channel, after both are decoded as 8-bit RGB as stored in the canonical PNG). Alpha, if present, MUST match M3/M4 canonical RGB8 (no alpha) rules — M7 still writes RGB8 canonical PNG.
2. **Alternative:** a separate GPU golden regenerated by poteto; document in the fixture with `# sha256:` of that GPU image. Prefer (1) for hero demos so one golden serves both.

GPU NEED NOT match CPU float expression order or reduction order. Documented reasons for delta include float32 GPU accumulation (§6.2) vs float64 CPU (M4 §12).

### 5.3 Fallback images

When Vulkan was selected but the call fell back to CPU, the image MUST match the CPU / M4 contract (bit-identical for those scenes). Fixtures that intend to assert GPU pixels MUST use `# requires_vulkan: true` and STRICT or an availability check so CI does not silently accept CPU fallback as a GPU pass.

### 5.4 Quantization

Final display bytes still use M3/M4: clamp channel to [0,1], then `floor(c * 255.0 + 0.5)`. No gamma, no tonemap.

---

## 6. Compute backend requirements

### 6.1 Vulkan surface

- Vulkan **1.1+** compute queue.
- One or more compute shaders implementing the M4 integrator (M4 §8–§10): same sampling indices `f(x,y,sampleIndex)`, same BRDF / NEE / delta glass-mirror formulas.
- Scene uploaded as buffers (triangles and/or a GPU BVH). Implementation MAY rebuild BVH on GPU; closest-hit for fixtures MUST match M4 within §5 tolerance.
- No windowing, no swapchain presentation in M7.

### 6.2 Accumulator precision

- **CPU:** float64 RGB accumulator as M4 §12.
- **GPU:** float32 accumulator MUST be documented as allowed; final mean computed in float32 then quantized. This is the primary source of CPU↔GPU byte deltas covered by `# png_tol:`.
- Eggtooth MUST NOT demand bit-identity between CPU and successful GPU images.

### 6.3 Threading vs workgroups

- `FARMOS_THREADS` applies only to CPU fallback / CPU backend tile pool (M4 §11).
- GPU workgroup size is implementation-defined.
- Pixel sample stream remains a pure function of `(x, y, sampleIndex)` as M4 §8.2 / §11 so progressive accumulation order stays well-defined per sample index.

### 6.4 Progressive accumulation

- Same external API: `setSamples`, `resetAccumulation`, `setSize` clear rules as M4 §12.
- One `renderPath` adds exactly `samples` new samples per pixel.
- GPU and CPU MUST NOT share an inconsistent count: after a call completes (including after fallback), `count` increases by `samples` as on CPU.

---

## 7. Diagnostics

No new E0xxx. Wrong types on the new methods use existing M1 codes. An out-of-range backend is a **runtime** issue (trap 118), not a compile error — `setPathBackend` takes `int`.

---

## 8. DCE and size

### 8.1 Hello / unused path

Extends M6 §4 spirit:

| Program shape | Size | Vulkan symbols |
|---------------|------|----------------|
| M1 hello (no graphics / path / physics / `parallel`) | ≤ **20 KiB** stripped | MUST NOT import Vulkan loader symbols |
| Calls `render` only (raster) | M3/M6 rotating-cube budgets unchanged | MUST NOT require Vulkan at load |
| Calls `renderPath` with default / CPU backend only | M4 glass+mirror ≤ **300 KiB** still applies to that demo shape | MUST NOT require Vulkan at load; MUST NOT import `vulkan-1.dll` / `vkGetInstanceProcAddr` solely because `renderPath` exists |
| References Vulkan opt-in (`PATH_BACKEND_VULKAN` / `setPathBackend(1)` / `isVulkanPathAvailable` reachable) | MAY pull Vulkan loader glue | Dynamic load still REQUIRED at first use |

**Vulkan loader symbols** (non-exhaustive fail list for hello / CPU-only): `vkGetInstanceProcAddr`, import of `vulkan-1.dll` (Windows), `libvulkan.so.1` (Linux) as a load-time dependency of the PE/ELF when GPU was never opted into.

### 8.2 GPU-linked demo budget (OQ-M7-04 option A)

A stripped Windows x64 PE of a glass+mirror-shaped program that **reaches** Vulkan opt-in (e.g. fixture `008` shape or `examples/` GPU demo) MUST be ≤ **400 KiB** (409600 bytes). This is a soft product budget for v1.01; the CPU-only 300 KiB gate remains for programs that never opt in.

### 8.3 Measurement

Same procedures as M4 §15.2 / M6 §3.1. Symbol checks via `dumpbin /IMPORTS` or `llvm-nm` / `nm` as on TommyLaptop. Document commands in `scripts\check_size_budgets.ps1` (or M7 sibling).

---

## 9. Version 1.01

- `farmc version` MUST print a line that contains the substring `1.01`. Recommended first line exactly: `farmc 1.01`.
- `farmc -v` and/or `farmc --version` SHOULD behave the same when used as a version query (if the driver already uses `-v` for verbose build, prefer `farmc version` as the normative AC surface and document any conflict in CHANGELOG).
- Repository MUST include a `CHANGELOG.md` (or docs changelog section) entry for **v1.01** describing optional Vulkan path tracing.
- No language-level `FARMOS_VERSION` constant is required in M7; version AC is CLI / docs (fixture `010` is eggtooth).

---

## 10. Runtime traps

| Exit | stderr | When |
|------|--------|------|
| 105–108, 110–113 | (unchanged) | M3 / M4 |
| 114–116 | (unchanged) | M5 |
| **117** | `runtime error: vulkan path failed` | `renderPath` with Vulkan selected, GPU failed, and `FARMOS_PATH_GPU_STRICT=1` |
| **118** | `runtime error: invalid path backend` | `setPathBackend` with value other than `0` or `1` |

Exit **109** remains unused (M3.5). Check order on `renderPath`: M4 §4.2 steps 1–4 first; then backend selection; STRICT failure yields 117 without mutating accumulator / display (same “no buffer update on early trap” spirit as M4 steps 1–4).

---

## 11. Compiler diagnostics

None new. See §7.

---

## 12. Acceptance criteria

| ID | Criterion |
|----|-----------|
| AC-M7-01 | `PATH_BACKEND_CPU` / default `renderPath` remains bit-identical to M4 for fixtures `001`, `006`, `007` (SHA match). |
| AC-M7-02 | Explicit opt-in: GPU never used until `setPathBackend(PATH_BACKEND_VULKAN)`; `getPathBackend` round-trips 0/1. |
| AC-M7-03 | Without STRICT, Vulkan failure / no device → silent CPU fallback, exit 0; backend stays VULKAN (`002`, `012`). |
| AC-M7-04 | `FARMOS_PATH_GPU_STRICT=1` + Vulkan failure → trap **117** (`003`). |
| AC-M7-05 | Successful GPU Cornell-class image within `# png_tol: 2` of CPU golden when `# requires_vulkan: true` (`008`); soft-skip without device. |
| AC-M7-06 | `setPathBackend(2)` (or other invalid) → trap **118** at setter (`009`). |
| AC-M7-07 | Hello ≤ 20 KiB and no Vulkan loader imports (§8.1); eggtooth `004`/`005`. |
| AC-M7-08 | CPU `renderPath` without Vulkan opt-in does not require Vulkan at load time (§4.5, §8.1). |
| AC-M7-09 | Glass+mirror CPU demo ≤ 300 KiB stripped (M4/M6); GPU-linked demo ≤ 400 KiB under OQ-M7-04 A (§8.2). |
| AC-M7-10 | M3 `render` goldens unchanged; fixture `011` raster clear/path API coexists. |
| AC-M7-11 | `farmc version` reflects **1.01**; CHANGELOG has v1.01 entry (eggtooth `010`). |
| AC-M7-12 | `FARMOS_THREADS` affects CPU only; GPU workgroups implementation-defined (§6.3). |
| AC-M7-13 | No DX12/CUDA/OpenCL/ray-query/windowing in M7 scope (§1.2). |
| AC-M7-14 | Harness honors `# requires_vulkan:` soft-skip and `# png_tol:` (§13). |
| AC-M7-15 | Fixtures in §15 pass under their kinds (CPU always; GPU where device present). |
| AC-M7-16 | Dynamic Vulkan load on first GPU attempt; hello DCE gate green on Windows. |

AC-M7-07, AC-M7-09, AC-M7-11 are eggtooth / script checks (no ctest image compare).

---

## 13. Test harness

Normative detail: `spec/tests/M7/README.md`.

### 13.1 Working directory

Harness MUST run each M7 program with CWD = `spec/tests/M7` (same pattern as M4).

### 13.2 New / extended headers

| Header | Meaning |
|--------|---------|
| `# requires_vulkan: true` | Soft-skip: if `isVulkanPathAvailable` would be false **or** the host has no Vulkan device, harness exits 0 as **skip** (not fail). Linux CI without a device MUST soft-skip. |
| `# png_tol: <int>` | Max absolute per-channel byte delta vs the reference PNG identified by `# sha256:` (decoded RGB8). `0` means bit-identical. Recommended GPU value: `2`. |
| `# env: KEY=VALUE` | Optional; harness SHOULD set environment variables for the child (at least `FARMOS_PATH_GPU_STRICT`, `FARMOS_VULKAN_FORCE_FAIL`). If unsupported as a header, eggtooth documents the equivalent wrapper. |

Kinds: `run`, `run_png`, `runtime_trap` as in M1/M3/M4. Size / DCE / version are eggtooth scripts like M6.

### 13.3 Placeholders

Some `run_png` goldens are copied from M4 (real SHA). GPU-specific regenerations may use:

```
## PLACEHOLDER — poteto regenerates against CPU M4 golden / GPU tolerance
# sha256: 0000000000000000000000000000000000000000000000000000000000000000
```

only when no M4 twin exists. Prefer real M4 SHA + `# png_tol: 2` for heroes.

---

## 14. Fixture outline

| Fixture | Kind | What |
|---------|------|------|
| 001_cpu_background | run_png | CPU `renderPath`, empty scene, M4-001 twin, real SHA |
| 002_vulkan_opt_in_fallback | run | set Vulkan, `renderPath`, print availability; exit 0 (fallback OK) |
| 003_strict_force_fail | runtime_trap | STRICT + `FARMOS_VULKAN_FORCE_FAIL=1` → 117 |
| 004_hello_size | eggtooth | hello ≤ 20 KiB |
| 005_hello_no_vulkan_sym | eggtooth | hello has no vulkan-1 / `vkGetInstanceProcAddr` |
| 006_cornell_cpu | run_png | Cornell CPU, M4-008 twin, real SHA |
| 007_glass_mirror_cpu | run_png | glass+mirror CPU, M4-009 twin, real SHA |
| 008_cornell_vulkan_tol | run_png | Cornell + Vulkan + `# png_tol: 2` + `# requires_vulkan: true` |
| 009_trap_invalid_backend | runtime_trap | `setPathBackend(2)` → 118 |
| 010_version_1_01 | eggtooth | `farmc version` contains `1.01` |
| 011_raster_clear | run_png | M3-style `render` clear; imports path-backend names but uses raster only |
| 012_force_fail_fallback | run | FORCE_FAIL without STRICT → exit 0; backend stays 1 |

**Counts:** 9 `.fm` harness fixtures + 3 eggtooth script ACs = **12** index entries. (Stub pack may grow if PM requests more.)

---

## 15. Test index (source files)

| # | File | Kind |
|---|------|------|
| 001 | `001_cpu_background.fm` | run_png |
| 002 | `002_vulkan_opt_in_fallback.fm` | run |
| 003 | `003_strict_force_fail.fm` | runtime_trap |
| 006 | `006_cornell_cpu.fm` | run_png |
| 007 | `007_glass_mirror_cpu.fm` | run_png |
| 008 | `008_cornell_vulkan_tol.fm` | run_png |
| 009 | `009_trap_invalid_backend.fm` | runtime_trap |
| 011 | `011_raster_clear.fm` | run_png |
| 012 | `012_force_fail_fallback.fm` | run |

Eggtooth-only rows 004, 005, 010 have no `.fm` (see README).

---

## 16. Open questions

**None.** PM decided OQ-M7-01 .. OQ-M7-04, all option A, on 2026-10-13. Also binding: trap 118 at `setPathBackend`; `FARMOS_VULKAN_FORCE_FAIL=1`; `# requires_vulkan` soft-skip on Linux CI. See §17.

---

## 17. Resolved decisions

PM 2026-10-13 accepted option A for every OQ below. PLAN DECIDED items and binding confirmations follow.

| ID | Decision |
|----|----------|
| OQ-M7-01 | `setPathBackend` / `getPathBackend` / `isVulkanPathAvailable` on `Renderer`; keep `renderPath`. Constants `PATH_BACKEND_CPU=0`, `PATH_BACKEND_VULKAN=1` on `farmos:scene`. |
| OQ-M7-02 | `FARMOS_PATH_GPU_STRICT=1` → trap **117** on GPU failure; default silent CPU fallback; backend stays VULKAN. |
| OQ-M7-03 | Successful GPU images: `# png_tol: 2` vs CPU/M4 golden; CPU bit-identical; GPU float32 OK. |
| OQ-M7-04 | GPU-linked demo ≤ **400 KiB** stripped; CPU glass+mirror ≤ 300 KiB. |
| RD-M7-01 | Optional Vulkan **compute** path tracer accelerating existing M4 path tracer only. |
| RD-M7-02 | M3 `render()` raster stays CPU-only. |
| RD-M7-03 | Explicit GPU opt-in preferred over silent-only auto. |
| RD-M7-04 | CPU fallback required if no Vulkan / init fail / GPU error; M4 image contract unless documented tolerance. |
| RD-M7-05 | DCE: hello ≤ 20 KiB and no Vulkan symbols when path/GPU unused; extend M6 spirit to CPU-only `render`/`renderPath` without GPU opt-in. |
| RD-M7-06 | Windows primary; Linux CI soft-skip GPU fixtures without a device; CPU fixtures still required. |
| RD-M7-07 | Out of scope: GPU raster, DX12/CUDA/OpenCL, RTX ray-query HW path, windowing, textures beyond M4. |
| RD-M7-08 | Version / changelog reflect **1.01**. |
| RD-M7-09 | OQ-M7-01..04 accepted option A (PM 2026-10-13). |
| RD-M7-10 | Trap 117 STRICT-only; trap 118 at `setPathBackend` for invalid backend. |
| RD-M7-11 | GPU float32 accumulator allowed; CPU float64 retained. |
| RD-M7-12 | `FARMOS_VULKAN_FORCE_FAIL=1` test hook for deterministic failure. |
| RD-M7-13 | Dynamic load of Vulkan loader on first GPU attempt REQUIRED. |
| RD-M7-14 | `# requires_vulkan: true` → soft-skip (exit 0 skip) on Linux CI / no device. |

---

## 18. Amendments relative to M4 / M6

M4-ray.md and M6-hardening.md are not edited.

| # | Amendment |
|---|-----------|
| 1 | `farmos:scene` exports `PATH_BACKEND_CPU`, `PATH_BACKEND_VULKAN`. |
| 2 | `Renderer` gains `setPathBackend`, `getPathBackend`, `isVulkanPathAvailable`. |
| 3 | `renderPath` may execute on Vulkan compute when backend is VULKAN; otherwise M4 CPU. |
| 4 | Traps 117, 118. Exit 109 still unused. |
| 5 | Env `FARMOS_PATH_GPU_STRICT`, `FARMOS_VULKAN_FORCE_FAIL`. |
| 6 | DCE table extends M6 §4: no Vulkan symbols without GPU opt-in. |
| 7 | Size: GPU-linked demo ≤ 400 KiB (OQ-M7-04 A); CPU budgets unchanged. |
| 8 | Harness: `# requires_vulkan:`, `# png_tol:` in `spec/tests/M7/README.md`. |
| 9 | `farmc version` / changelog → 1.01. |
| 10 | M6 size script SHOULD gain Vulkan-symbol and optional 400 KiB checks. |

---

## Appendix A — Minimal CPU twin (001)

Same shape as `spec/tests/M4/001_background_only.fm` (16×16, clear `0x4488ff`). Backend left default CPU.

## Appendix B — Opt-in sketch (002)

```
import { Scene, PerspectiveCamera, Renderer,
         PATH_BACKEND_VULKAN } from "farmos:scene";
import { Color } from "farmos:math";

function main(): int {
  const scene: Scene = new Scene();
  scene.setBackground(new Color(0x4488ff));
  const camera: PerspectiveCamera = new PerspectiveCamera(60, 1, 0.1, 100);
  camera.position.z = 5;
  const renderer: Renderer = new Renderer(16, 16);
  renderer.setSamples(1);
  renderer.setPathBackend(PATH_BACKEND_VULKAN);
  renderer.renderPath(scene, camera);
  println(renderer.isVulkanPathAvailable());
  println(renderer.getPathBackend());
  return 0;
}
```

Stdout depends on the host device; see fixture `002` expected file for accepted patterns / harness notes.

## Appendix C — Invalid backend (009)

```
renderer.setPathBackend(2);  // trap 118 before renderPath
```

---

## Document history

- 2026-10-13: DRAFT. Optional Vulkan compute path tracer; option A throughout OQ-M7-01..04. Fixtures 001–012 outline.
- 2026-10-13: FINAL. PM accepted OQ-M7-01 .. OQ-M7-04, all option A. Binding: trap 118 at setPathBackend; FARMOS_VULKAN_FORCE_FAIL; # requires_vulkan soft-skip. Open questions cleared.
