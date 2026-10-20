# Farmos M11 — Opt-in GGX/VNDF Glossy Continuation (metal-black fix)

**Status:** **FINAL** (PM/user 2026-10-20: OQ-M11-01 .. OQ-M11-08 all A)  
**Milestone:** M11 — opt-in glossy specular continuation for rough metals  
**Depends on:** `spec/M1-core.md` .. `spec/M10-denoise-interactive.md` (all FINAL)  
**Normative keywords:** MUST / SHOULD / MAY per RFC 2119.  
**Date:** 2026-10-20 (FINAL)

This document specifies Farmos M11: an **opt-in** path-tracer bounce mode that samples a **specular / GGX–VNDF reflection continuation** for rough metals (`metalness > 0` after clamp, `roughness > 0`), so environment / background reflection reaches the image without changing artwork lights or forcing `setRoughness(0)`. It does **not** specify compiler, runtime, or stdlib implementation code. It does **not** edit M1–M10 milestone markdown bodies; normative amendments relative to those milestones live in §19 of this file.

Open questions: **none**. OQ-M11-01 .. OQ-M11-08 **accepted A** (PM lock 2026-10-20; user 2026-10-20 all-A). Binding: additive `setPathGlossyContinuation` + `PATH_GLOSSY_OFF` / `PATH_GLOSSY_VNDF`; default **OFF**; trap **121**; **VNDF-GGX** reflection lobe; only `metal>0` && `rough>0` (dielectrics stay M4); keep RectArea NEE + balance-heuristic MIS; no required firefly clamp; default stays OFF for all of M11; no new PE ceiling (DCE; prior ≤300/400/500/600); full CPU/Vulkan/RT parity (no silent BSDF soft-skip). ACs: OFF → ball-center mean L **≤ 5** / M4 goldens; ON → L **≥ 80** (target ~v1 **≈106.31**); rough stays **0.04**; no env brighten; no artwork `setRoughness(0)` as product fix. Body gates / ACs are **binding** under A.

Related: `/workspace/farmos/PLAN.md` (M11 stub), `spec/M4-ray.md` §10.5–§10.8, `spec/M8-path-speed.md` (additive sampling flags), evidence `/workspace/artwork_silver_metal_black_ab.md`, `/workspace/metal_black_ab/`, `/workspace/investigate_metal_black_claim.md`.

**Binding product intent (PM / user GO implement-glossy-continuation; lock 2026-10-20 all-A):**

1. **Opt-in / new flag; default OFF** so existing M4 (and M7/M8/M9/M10 OFF-path) goldens stay green.
2. Same semantics on **CPU (`farm_ray`) + Vulkan compute + Vulkan RT**.
3. Do **NOT** env-brighten artwork; do **NOT** change artwork to `setRoughness(0)` as the product fix.
4. Stopgap `setRoughness(0)` restores Oct-14 v1 ball **exactly** (material, not lighting) — documented, **not** the M11 product fix.
5. Flag **ON**: silver ball-center ROI clearly lit (target order ~v1 **L ≈ 100+**, not **0**). Flag **OFF**: current M4 behavior / goldens preserved.
6. RectAreaLight NEE remains (OQ-M11-04 A: balance-heuristic MIS).
7. Denoise / RT / spp are **not** the root cause (CLAIM PASS tip `9c5eebd`).

**Target platform:** Windows x64 primary (TommyLaptop) for silver ROI / backend parity; Linux CI for CPU fixtures and soft-skip GPU when no device.

---

## 1. Scope and non-goals

### 1.1 Product split (OQ-M11-01 A / OQ-M11-03 A binding)

M11 MUST deliver:

| Piece | Requirement under A |
|-------|-------------------------------|
| Opt-in glossy continuation | Additive `setPathGlossyContinuation` / `getPathGlossyContinuation` + `PATH_GLOSSY_*`; default **OFF** |
| Rough-metal bounce | When ON and `metal > 0` and `rough > 0`: sample **specular reflection** (VNDF-GGX under OQ-M11-02 A) instead of M4 cosine hemisphere × `(1−metal)` |
| Backend parity | Identical estimator semantics on CPU, `PATH_BACKEND_VULKAN`, `PATH_BACKEND_VULKAN_RT` |
| Compat | Flag OFF → M4 §10.7 rough bounce unchanged (CPU bit-identical where M4 requires) |
| Silver AC | Artwork (or equiv fixture) with `metalness=1`, `roughness=0.04`, RectAreaLights only: ball-center ROI mean L **≥ 80** (SHOULD approach ~**106** v1 order) with flag ON; flag OFF remains near-black (**≈ 0**) on that scene |

### 1.2 Explicit non-goals (OUT)

| Item | Note |
|------|------|
| Env / background brightening of artwork | **OUT** — rejected by CLAIM; do not reopen |
| Artwork `setRoughness(0)` as the product fix | **OUT** as fix; MAY remain documented stopgap only |
| Rewriting M4 goldens under default OFF | **OUT** — default must preserve SHA / contracts |
| Making glossy ON the language default in M11 | **OUT** (OQ-M11-06 A: keep OFF) |
| DLSS / vendor upscalers / Swapper | **OUT** (M10 OQ-M10-06 A stands) |
| Full PBR rewrite (multiscatter, clearcoat, anisotropy, layered BSDF) | **OUT** |
| Changing M4 delta-mirror (`rough == 0`) branch | Unchanged |
| Changing dielectric / glass refraction contract | Unchanged (OQ-M11-03 A: metals only) |
| OS window / swapchain | Still out (M10) |
| OptiX / full RT pipeline / GPU raster | OUT |
| Editing M1–M10 milestone markdown bodies | Amendments in §19 only |

### 1.3 Design constraints

- M4 CPU goldens and `path_ref.py` remain authoritative when glossy continuation is **OFF**.
- M7/M8/M9 fallback / STRICT / FORCE_FAIL / traps 117–119 / M10 denoise 120 remain binding.
- Hello ≤ 20 KiB; unused path/GPU MUST NOT grow hello (M6/M7 spirit).
- Prior size caps: glass+mirror ≤ 300 KiB; compute-linked ≤ 400; RT-linked ≤ 500; denoise-linked ≤ 600 — M11 MUST NOT blow them (OQ-M11-07 A).
- Pixel values under OFF MUST NOT depend on thread scheduling (M4 determinism).
- ON mode MAY use `# png_tol` / separate goldens; MUST document estimator.

### 1.4 Root-cause summary (normative evidence; do not invent conflicting numbers)

Confirmed CLAIM (tip `9c5eebd`, TommyLaptop A/B):

| Control | Ball-center mean L (40×40 @ ~960,540) | Result |
|---------|--------------------------------------|--------|
| Artwork `roughness=0.04`, `metalness=1` (current tip) | **0.00** | FAIL_BLACK |
| Same scene, `setRoughness(0)` only | **106.310** RGB=(99,107,121) | PASS_SILVER |
| Oct-14 v1 `artwork_silver_still.png` | **106.310** | PASS_SILVER |
| `metalness=0` (same lights) | **255** | lights OK |

M4 contract (`spec/M4-ray.md`):

- Indirect GGX on rough surfaces was **out of scope**; rough indirect = one cosine hemisphere.
- Throughput weight `(1−Fview)*(1−metal)*(1−trans)*albedo` → **0** when `metal=1`.
- Perfect mirror only when `rough == 0` exactly (no epsilon).
- Rough metal lit only by direct GGX NEE → peaky / black center + fireflies.

This failure is **shared by CPU, Vulkan compute, and Vulkan RT**. Denoise / spp / orbit-vs-still are not the root cause.

---

## 2. Relationship to prior milestones

### 2.1 M4 integrator (OFF = unchanged; ON = reopen non-goal)

From `spec/M4-ray.md` FINAL:

- Rough (`rough > 0`) bounce: cosine hemisphere; weight includes `(1−metal)` (§10.7).
- “No indirect specular on rough surfaces” (§10.8).
- OQ-M4-08 A / RD-M4-06: rough indirect is **not** a GGX sample.

**M11 reopens** that non-goal **behind an opt-in flag** (same pattern as M10 reopening M9 denoise-OUT). When flag is OFF, M4 text remains the observable contract. When flag is ON, §5 of **this** spec amends the rough-metal bounce only.

**PLAN escalation:** PLAN M4 / M4-ray list “Indirect GGX rays on rough surfaces” as out of scope. M11 does **not** silently edit those bodies; PM/user lock 2026-10-20 **accepted** the reopen via OQ-M11-01..08 A (analogous to M10 vs M9 denoise). PLAN M11 stub records §19 reopen as opt-in-only.

### 2.2 M7 / M8 / M9 / M10 (unchanged orthogonality)

- Backends `0` / `1` / `2`; soft RT→compute→CPU; STRICT 117; trap 118.
- M8 `setPathSampling` / RR / adaptive remain independent; glossy continuation is a **separate** flag (OQ-M11-01 A binding) so M4 SHA under `PATH_SAMPLING_M4` + glossy OFF stays clean.
- M10 denoise / progressive remain orthogonal; denoise MUST NOT be required to pass M11 silver ROI AC.

### 2.3 What M11 adds

| Layer | Addition |
|-------|----------|
| API | `PATH_GLOSSY_OFF` / `PATH_GLOSSY_VNDF` (OQ-M11-01 A); `setPathGlossyContinuation` / `getPathGlossyContinuation` |
| Integrator | When ON: rough-metal specular continuation (§5) |
| Fixtures | `spec/tests/M11/` OFF twin + ON ROI smoke + trap + roundtrip |
| Artwork | Programs MAY call the flag; M11 MUST NOT require artwork mesh/light/camera edits |

---

## 3. API (OQ-M11-01 A — binding)

`farmos:scene` / `Renderer` MUST additionally export:

```
const PATH_GLOSSY_OFF: int = 0
const PATH_GLOSSY_VNDF: int = 1

setPathGlossyContinuation(mode: int): void
getPathGlossyContinuation(): int
```

Binding names under **OQ-M11-01 A** / **OQ-M11-02 A**: `PATH_GLOSSY_OFF`, `PATH_GLOSSY_VNDF`, `setPathGlossyContinuation`, `getPathGlossyContinuation`. Implementation notes MAY mention aliases only as non-normative; the exported API MUST use these names.

### 3.1 Defaults

- Newly constructed `Renderer`: `PATH_GLOSSY_OFF` (0).
- Glossy continuation MUST NOT run until the program sets a non-zero mode.
- No env var is **required** to force ON for product defaults (OQ-M11-06 A). A test-only env MAY exist (`FARMOS_PATH_GLOSSY=1`) if documented; method call wins if both set.

### 3.2 `setPathGlossyContinuation`

1. If `mode` not in the supported set under OQ-M11-02 A (`{0, 1}` for OFF / VNDF) → trap **121** `runtime error: invalid path glossy mode` **at the setter** (not deferred to `renderPath`).
2. Otherwise store mode for subsequent `renderPath` / progressive steps on this renderer.
3. MUST NOT by itself clear the accumulator or change `setPathBackend` / denoise / sampling / roulette state.

### 3.3 Orthogonality

| Other API | Interaction |
|-----------|-------------|
| `setPathBackend` | Unchanged `{0,1,2}` / trap 118 |
| `setPathSampling` | Independent; trap 119 unchanged. Glossy ON + `PATH_SAMPLING_MIS` SHOULD compose (document MIS with glossy BSDF pdf under OQ-M11-04 A) |
| `setPathRoulette` | Independent |
| `setPathDenoise` | Independent; trap 120 unchanged |
| `renderPath` / progressive M10 | Use current glossy mode each call |

### 3.4 Rejected API shapes (record)

| Option | Meaning | Status |
|--------|---------|--------|
| **A** | Additive `setPathGlossyContinuation` + `PATH_GLOSSY_*`; default OFF | **ACCEPTED** |
| B | Fold into `setPathSampling` as new `PATH_SAMPLING_GLOSSY_*` value | Rejected |
| C | Always-on for `metal>0` (breaks M4 goldens) | Rejected |

---

## 4. Material classes — exact expected behavior

Clamp rules from M4 §5.2 still apply: `metal`, `rough`, `trans` in [0,1]; metal wins over transmission.

| Case | Flag OFF (M4) | Flag ON (M11 §5) |
|------|---------------|------------------|
| `rough == 0`, any metal | Delta mirror / glass as M4 §10.6 | **Unchanged** (delta branch) |
| `rough > 0`, `metal == 0`, `trans == 0` | Cosine diffuse bounce | **Unchanged** (OQ-M11-03 A: dielectrics stay M4) |
| `rough > 0`, `trans > 0` (glass rough) | M4 rough + transmission rules | **Unchanged** (OQ-M11-03 A) |
| `rough > 0`, `metal > 0` | Cosine × `(1−metal)` → **no** env bounce; NEE-only | **Specular glossy continuation** (§5); ambient still × `(1−metal)` → 0 for full metal |
| BasicMaterial / emissive hits | M4 | Unchanged |

**Normative intent for conductors:** a rough chrome / silver ball MUST be able to reflect `scene` background and other geometry via the bounce path when the flag is ON, restoring the filled-silver look without `rough=0`.

---

## 5. Glossy continuation semantics (OQ-M11-02 A / OQ-M11-03 A / OQ-M11-04 A binding)

### 5.1 When the special branch runs

On each path hit where M4 would take the **rough** (`rough > 0`) indirect branch:

```
if getPathGlossyContinuation() != PATH_GLOSSY_OFF
   and metal > 0:
   // §5.2 specular continuation
else:
   // exact M4 §10.7 cosine + weight
```

### 5.2 Specular sample (VNDF-GGX reflection — OQ-M11-02 A)

Under **OQ-M11-02 A** (binding):

1. Build shading frame about `N` (same orthonormal construction spirit as M4 §10.7).
2. Sample a microfacet normal with **VNDF** for GGX (`alpha = rough * rough`), visible from `-dir` (incident toward surface).
3. Reflect incident about the microfacet normal → outgoing direction `d` (MUST be in the upper hemisphere relative to `N` after reflection; if invalid, MAY resample once or fall back to perfect `reflect_dir` — document choice; MUST be deterministic given RNG stream).
4. Throughput update MUST be the **specular conductor estimator** consistent with the sample pdf:
   - Use Schlick Fresnel with `F0 = 0.04*(1−metal) + albedo*metal` (M4).
   - Include GGX D / Smith G terms as required by the chosen VNDF identity so the estimator is unbiased (default MUST be unbiased aside from OQ-M11-05 A firefly policy: no required clamp).
5. Origin: `hit + N * RAY_EPS` (same as M4 reflection).
6. Do **not** multiply by `(1−metal)` on this specular path (that factor is the diffuse residual).

Exact GLSL/C formulas are implementation-owned but MUST match across CPU / compute / RT and MUST be summarized in runtime notes for eggtooth.

### 5.3 Direct lighting / RectArea NEE (OQ-M11-04 A)

Under **OQ-M11-04 A** (binding):

- Keep M4/M8 **RectAreaLight NEE** (and Point / Directional / Ambient rules) for rough hits.
- When glossy continuation is ON and the surface is a rough metal, NEE specular and BSDF specular sample MUST use **MIS** with the **balance heuristic** and documented pdfs so energy is not double-counted.
- Ambient term remains M4 (zeroed by `(1−metal)` for metals) — glossy env comes from **miss → background** along the specular bounce, not from ambient.

Options B/C (disable NEE / PM weights) are **rejected**.

### 5.4 Firefly policy (OQ-M11-05 A)

Under **OQ-M11-05 A** (binding):

- Default: **no** hard radiance clamp (preserve unbiased-ish look).
- Implementation MAY apply a documented **soft** per-sample contribution cap only when an explicit future mode or env is set; M11 valid set `{0,1}` does **not** require a clamp mode.
- Artwork spp (e.g. 64–256) remains the primary variance control; denoise MAY help fireflies but is not required for ROI AC.

### 5.5 RNG / determinism

- MUST consume the same Hammersley / `rand01` discipline spirit as M4 (extra dimensions for VNDF are allowed; document stream offsets).
- CPU OFF mode: bit-identical to pre-M11 M4 for existing fixtures.
- CPU/GPU ON mode: backends MUST agree within `# png_tol` used by fixtures (recommend GPU `# png_tol: 2` vs CPU-ON reference for small scenes; silver ROI uses mean-L gate, not SHA).

### 5.6 Depth / RR / maxBounces

- Glossy continuation still counts as the single indirect spawn per hit (M4 “one bounce ray”).
- RR (M8) applies to throughput after the glossy update when enabled.
- `maxBounces` unchanged.

---

## 6. Backend parity (binding)

| Backend | Requirement |
|---------|-------------|
| CPU `farm_ray` | MUST implement §5 when mode ≠ OFF |
| Vulkan compute `farm_vk_path` | MUST implement same semantics |
| Vulkan RT `farm_vk_rt_path` | MUST implement same semantics (traversal may differ; BSDF MUST match) |

Under **OQ-M11-08 A** (binding): there is **no** soft-skip that leaves one backend on M4 cosine while another does VNDF when the program requested glossy ON. If a selected GPU backend cannot run the glossy shader variant, follow existing M7/M9 fallback / STRICT rules for **backend** failure — do not silently change BSDF mode.

Linux CI without GPU: CPU ON/OFF fixtures required; GPU/RT fixtures soft-skip via `# requires_vulkan` / `# requires_vulkan_rt`.

---

## 7. Image contract

| Mode | Contract |
|------|----------|
| Glossy OFF | M4/M7/M8/M9/M10-OFF contracts; CPU SHA where previously required |
| Glossy ON | New goldens or mean-L / `# png_tol` gates in §8; MUST NOT rewrite default-OFF goldens |

Placeholder golden header:

```
# sha256: 0000000000000000000000000000000000000000000000000000000000000000
```

---

## 8. Acceptance — silver ball ROI (binding under A)

### 8.1 Normative scene

- Prefer `examples/artwork_silver_still.fm` (or byte-equivalent fixture): `metalness=1`, `roughness=0.04`, RectAreaLights only, background ~(0.42,0.45,0.50), 1920×1080 when measuring product ROI.
- Small harness fixtures MAY use a scaled-down sphere + background for smoke; product AC uses full still or documented equiv ROI.

### 8.2 ROI definition

- Ball-center crop: **40×40** pixels about image center / documented centroid (CLAIM used `(x=940:980, y=520:560)` on 1920×1080; centroid ~`(962,550)` on failing frames).
- Metric: mean luminance **L** in 0–255 display space after `savePNG` (same method as CLAIM).

### 8.3 Gates

| Gate | Flag | Requirement |
|------|------|-------------|
| AC black (regression witness) | OFF | On artwork still (or equiv), ball-center mean L **≤ 5** (CLAIM: **0.00**; allows tiny float/noise) **or** existing M4 SHA twin passes |
| AC silver (product) | ON | Same scene/pose/spp class (≥ **64** spp recommended; **256** preferred): ball-center mean L **≥ 80**; SHOULD be within order of v1 **≈ 106** (not require pixel-identity to rough=0) |
| AC no-artwork-cheat | — | Fixture MUST keep `roughness=0.04` and MUST NOT raise background/light intensities vs locked artwork composition |
| AC parity | ON | CPU / Vulkan / Vulkan RT: each available backend satisfies L ≥ 80 on the same scene (soft-skip missing GPU) |
| AC stopgap note | — | Document that `setRoughness(0)` → L=**106.310** pixel-identical to v1; **not** an M11 AC to change artwork |

spp for AC-silver: implementer SHOULD use the same spp as product still (256) when practical; minimum for harness PASS is **64** spp on TommyLaptop HW RT when available, else CPU.

### 8.4 Trap / API ACs

| AC | Requirement |
|----|-------------|
| Default OFF | New `Renderer` → `getPathGlossyContinuation() == 0` |
| Roundtrip | set 1 → get 1 → set 0 → get 0 |
| Invalid mode | `setPathGlossyContinuation(99)` → exit **121**, stderr `runtime error: invalid path glossy mode` |
| OFF golden | M4 twin / denoise-off twin still SHA-identical on CPU |

---

## 9. Size / DCE (OQ-M11-07 A — binding)

Under **OQ-M11-07 A** (binding):

| Budget | Cap |
|--------|-----|
| hello (no path) | ≤ **20 KiB** |
| CPU glass+mirror | ≤ **300 KiB** |
| GPU compute-linked | ≤ **400 KiB** |
| RT-linked | ≤ **500 KiB** |
| Denoise-linked | ≤ **600 KiB** |
| M11 glossy | **No new higher PE cap** — glossy code MUST DCE when unused; when used, stay within the backend-linked cap already selected by the program |

If eggtooth measures a forced overrun solely due to glossy shaders, PM may open a follow-up OQ (do not invent a new KiB number beyond OQ-M11-07 A).

---

## 10. Traps and diagnostics

| Exit | Stderr | When |
|------|--------|------|
| **121** | `runtime error: invalid path glossy mode` | `setPathGlossyContinuation` with unsupported mode |
| 117–120 | (unchanged) | M7 STRICT / backend / M8 sampling / M10 denoise |

Exit **109** still unused (M3.5). No new E0xxx required; bad types use M1 codes.

---

## 11. Test hooks / env (optional)

| Variable | Notes |
|----------|--------|
| `FARMOS_PATH_GLOSSY` | MAY map `1` → treat as VNDF if setter never called; method wins |
| Existing M7/M8/M9/M10 env | Unchanged |

---

## 12. Measurable acceptance criteria (binding under A)

| ID | Criterion |
|----|-----------|
| AC-M11-01 | Default glossy mode OFF; M4 CPU goldens / OFF twins unchanged (SHA where required). |
| AC-M11-02 | `setPathGlossyContinuation` / `getPathGlossyContinuation` roundtrip; invalid → trap **121**. |
| AC-M11-03 | Flag OFF on artwork-equiv rough metal: ball-center mean L ≤ **5** (witness black) **or** SHA twin. |
| AC-M11-04 | Flag ON on artwork-equiv `metal=1`, `rough=0.04`: ball-center mean L ≥ **80** (target order ~**106**); roughness stays 0.04; no env/light brighten. |
| AC-M11-05 | AC-M11-04 holds on each available backend among CPU / VULKAN / VULKAN_RT (soft-skip missing GPU). |
| AC-M11-06 | Delta mirror (`rough==0`) fixtures unchanged with flag ON or OFF. |
| AC-M11-07 | Dielectric / glass fixtures unchanged with flag ON (OQ-M11-03 A metals-only). |
| AC-M11-08 | RectArea NEE still present; MIS documented when glossy ON (OQ-M11-04 A). |
| AC-M11-09 | Hello ≤ 20 KiB; prior PE caps not blown (OQ-M11-07 A). |
| AC-M11-10 | M7/M9 STRICT / fallback / M10 denoise traps unaffected. |
| AC-M11-11 | Spec/docs state stopgap `setRoughness(0)` restores v1 L=106.310 but is **not** the product fix. |
| AC-M11-12 | Fixtures under `spec/tests/M11/` pack present; eggtooth can map AC-M11-01..11. |

---

## 13. Explicit OUT checklist (repeat for auditors)

- Env brightening / reflection-card artwork hacks: **OUT**
- Shipping `setRoughness(0)` as the fix: **OUT**
- Default-ON glossy in M11: **OUT** (OQ-M11-06 A)
- Full rewrite of M4 goldens under OFF: **OUT**
- DLSS / OptiX / window / GPU raster: **OUT**

---

## 14. Fixtures layout

Working directory: `spec/tests/M11`.

| Header | Meaning |
|--------|---------|
| `# requires_vulkan: true` | Soft-skip without compute device |
| `# requires_vulkan_rt: true` | Soft-skip without HW RT |
| `# png_tol: <int>` | Byte tol vs golden |
| `# env: KEY=VALUE` | Child env |
| `# path_mode: compat` | Documentation aid (OFF expected) |

---

## 15. Fixture outline

| Fixture | Kind | What |
|---------|------|------|
| 001_glossy_off_cpu_twin | run_png | Glossy OFF; M4-001 twin SHA |
| 002_glossy_mode_roundtrip | run | set/get 0→1→0 |
| 003_trap_invalid_glossy | runtime_trap | `setPathGlossyContinuation(99)` → 121 |
| 004_glossy_on_metal_roi_smoke | run_png | Small rough metal + background; ON; assert not black / tol or stdout ROI helper |
| bench / artwork | script | full still ROI L≥80 ON / L≤5 OFF; backend matrix |

**Stub pack:** 4 `.fm` + README + `_ref` + bench note.

---

## 16. Test index (stub sources)

| # | File | Kind |
|---|------|------|
| 001 | `001_glossy_off_cpu_twin.fm` | run_png |
| 002 | `002_glossy_mode_roundtrip.fm` | run |
| 003 | `003_trap_invalid_glossy.fm` | runtime_trap |
| 004 | `004_glossy_on_metal_roi_smoke.fm` | run_png |

---

## 17. Open questions

Status: **FINAL**. Open questions: **none.**

OQ-M11-01 .. OQ-M11-08 **accepted A** (PM lock 2026-10-20; user 2026-10-20 all-A). Body §1–§12 / AC-M11-01..12 are **binding** under A.

### OQ-M11-01 — API shape — **ACCEPTED A**

| Option | Meaning | Status |
|--------|---------|--------|
| **A** | Additive `setPathGlossyContinuation` / `getPathGlossyContinuation` + `PATH_GLOSSY_OFF=0` / `PATH_GLOSSY_VNDF=1`; default OFF; invalid → trap **121** | **ACCEPTED** |
| B | New values on `setPathSampling` instead | Rejected |
| C | Bool `setPathGlossy(true)` only | Rejected |

### OQ-M11-02 — Specular lobe — **ACCEPTED A**

| Option | Meaning | Status |
|--------|---------|--------|
| **A** | **VNDF-GGX** reflection sample for rough metals (single lobe) | **ACCEPTED** |
| B | Classic GGX NDF sample (non-VNDF) | Rejected |
| C | Both modes as `PATH_GLOSSY_VNDF` and `PATH_GLOSSY_GGX` | Rejected |

### OQ-M11-03 — Which materials get the lobe — **ACCEPTED A**

| Option | Meaning | Status |
|--------|---------|--------|
| **A** | Only `metal > 0` && `rough > 0` (dielectrics / glass stay M4 cosine) | **ACCEPTED** |
| B | All rough opaque surfaces get specular+diffuse mix | Rejected |
| C | Roughness threshold (e.g. rough &lt; 0.2) only | Rejected |

### OQ-M11-04 — MIS vs RectArea NEE — **ACCEPTED A**

| Option | Meaning | Status |
|--------|---------|--------|
| **A** | Keep RectArea (and other) NEE; **balance-heuristic MIS** with glossy BSDF sample when ON | **ACCEPTED** |
| B | Glossy BSDF continuation only; disable specular NEE for rough metals when ON | Rejected |
| C | PM-specified weights / heuristic | Rejected |

### OQ-M11-05 — Firefly clamp — **ACCEPTED A**

| Option | Meaning | Status |
|--------|---------|--------|
| **A** | No required clamp in M11 `{0,1}`; variance via spp / optional denoise | **ACCEPTED** |
| B | Hard max-contribution clamp always when ON | Rejected |
| C | Separate `PATH_GLOSSY_VNDF_CLAMPED` mode | Rejected |

### OQ-M11-06 — Default permanence — **ACCEPTED A**

| Option | Meaning | Status |
|--------|---------|--------|
| **A** | Keep default **OFF** for all of M11 (artwork/programs opt in); do not flip language default | **ACCEPTED** |
| B | Plan post-M11 default ON after golden migration | Rejected (out of M11 binding) |
| C | Env default ON on TommyLaptop only | Rejected |

### OQ-M11-07 — Size budget — **ACCEPTED A**

| Option | Meaning | Status |
|--------|---------|--------|
| **A** | No new PE ceiling; DCE unused glossy; stay within existing backend-linked caps (≤300/400/500/600 as applicable) | **ACCEPTED** |
| B | Raise RT-linked or denoise-linked by a fixed KiB (PM number) | Rejected |
| C | Other | Rejected |

### OQ-M11-08 — Backend soft-skip — **ACCEPTED A**

| Option | Meaning | Status |
|--------|---------|--------|
| **A** | All selected backends MUST honor glossy ON semantics; no BSDF soft-skip; missing GPU uses existing backend soft-skip headers only | **ACCEPTED** |
| B | Allow GPU to silently stay on M4 cosine if glossy shader missing | Rejected |
| C | CPU-only glossy OK for M11 | Rejected |

---

## 18. Accepted decisions (OQ-01..08 FINAL binding)

| ID | Accepted |
|----|----------|
| OQ-M11-01 | A — `setPathGlossyContinuation` + `PATH_GLOSSY_OFF` / `PATH_GLOSSY_VNDF`; default OFF; trap **121** |
| OQ-M11-02 | A — VNDF-GGX |
| OQ-M11-03 | A — metals only (`metal>0` && `rough>0`) |
| OQ-M11-04 | A — keep RectArea NEE + balance-heuristic MIS |
| OQ-M11-05 | A — no required firefly clamp |
| OQ-M11-06 | A — default OFF for all of M11 |
| OQ-M11-07 | A — no new PE cap; DCE; prior ≤300/400/500/600 |
| OQ-M11-08 | A — full CPU/Vulkan/RT parity; no BSDF soft-skip |
| RD-M11-01 | Reopen M4 “no indirect specular on rough” **only** when flag ON |
| RD-M11-02 | ROI L ≥ **80** ON / ≤ **5** OFF; v1 reference **106.310**; rough stays **0.04** |
| RD-M11-03 | Do not env-brighten; do not ship roughness=0 as fix |
| RD-M11-04 | Tip evidence `9c5eebd` / CLAIM PASS |

---

## 19. Amendments relative to M4 / M7 / M8 / M9 / M10

M1–M10 milestone markdown bodies are **not** edited.

| # | Amendment |
|---|-----------|
| 1 | Reopen M4 non-goal “indirect GGX / specular on rough surfaces” as **in scope when** `getPathGlossyContinuation() != OFF` |
| 2 | `farmos:scene` exports glossy API (§3) |
| 3 | Trap **121** for invalid glossy mode |
| 4 | Backend shaders/CPU share §5 estimator |
| 5 | M8 sampling / M10 denoise remain orthogonal |
| 6 | PLAN M4 out-of-scope bullet for indirect GGX is **superseded for M11 opt-in only** (PM/user lock 2026-10-20 all-A) — recorded in PLAN M11 stub / §19 reopen |

---

## 20. Non-normative notes

- Stopgap: `silverMat.setRoughness(0)` → L=106.310 pixel-identical to Oct-14 v1; acceptable only as temporary artwork hack, not engine completion.
- Preferred product path: enable `PATH_GLOSSY_VNDF` in still/orbit programs (or a future helper) once poteto implements.
- Do not merge lighting-only “fixes” that brighten `setBackground` / RectArea intensity to paint over black metals.

---

## Changelog

- 2026-10-20: **FINAL**. PM/user lock OQ-M11-01 .. OQ-M11-08 all A. Opt-in VNDF-GGX glossy continuation for rough metals; default OFF; CPU+Vulkan+RT parity; ROI L≥80 ON / witness black OFF (L≤5); rough stays 0.04; no env brighten; no artwork setRoughness(0) as product fix; fixtures under `spec/tests/M11/`. Open questions: none. No implementation code.
- 2026-10-20: **DRAFT**. Opt-in VNDF-GGX glossy continuation for rough metals; default OFF; CPU+Vulkan+RT parity; ROI L≥80 ON / witness black OFF; OQ-M11-01..08 recommend A; fixtures under `spec/tests/M11/`. No implementation code.
