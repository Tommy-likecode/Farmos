# Farmos — compiled programming language (project plan)

Source requirement (user, D:\Tommy\Farmos\task.txt):
"一个编译类编程语言，使用c++，尽可能让最后编译的结果小，占用资源少，应当集成类似Three.js的语法，以及光线追踪、反射、材质、物理等"
= A compiled language; compiler written in C++; compiled output as small and resource-light as possible;
built-in Three.js-like syntax/API; ray tracing, reflection, materials, physics.

Working name: Farmos (file extension .fm, compiler CLI `farmc`). Rename later if the user wants.

## Architecture decisions (Projects Manager)
- Compiler implemented in C++17, CMake build, no heavy deps (no LLVM required to build farmc).
- Pipeline: lexer -> parser -> AST -> semantic/type check -> Farmos IR -> backend emits portable C11
  -> invoke system C compiler (clang/gcc; optionally bundled tcc) with -Os, LTO, --gc-sections, strip.
- Statically typed, simple syntax (JS/TS-like surface so Three.js-style code reads naturally), value types
  for vec/mat, no GC (arena + RAII-style scopes) to keep runtime tiny.
- Runtime/stdlib: only linked if used (dead-code eliminated). Graphics stdlib written in C, header-only style.
- Rendering: CPU path/ray tracer first (zero GPU deps, smallest binary); output PNG (tiny encoder) and
  optional window via minimal platform layer. GPU: Farmos v1.01 / M7 = optional Vulkan path-tracing
  backend (CPU fallback required; hello/DCE budgets must not regress when GPU unused).

## Milestones and acceptance criteria
M1 Core language
- Lexer, parser, types (int, float, bool, string, arrays, structs/classes, functions), control flow, modules.
- `farmc build hello.fm` produces a native Windows executable (C backend: clang or gcc only; MSVC cl unsupported).
- AC (amended 2026-09-24: target is Windows, fully local on TommyLaptop, Linux out of scope): hello world stripped binary <= 20 KB on Windows x64; test suite of >= 30 language tests passes via local scripts\build_and_test.ps1;
  clear compile errors with line/column.
M2 Math stdlib
- Vector2/3/4, Matrix3/4, Quaternion, Color, Euler, Ray, Box3, Sphere; operator overloading.
- AC: unit tests against known values; unused math not linked (binary size unchanged for hello world).
M3 Three.js-like scene API
- Scene, Object3D (position/rotation/scale/children), PerspectiveCamera, Mesh, BoxGeometry, SphereGeometry,
  PlaneGeometry, MeshBasicMaterial, MeshStandardMaterial (color, roughness, metalness), PointLight,
  DirectionalLight, AmbientLight, Renderer with renderer.render(scene, camera).
- AC: a Three.js "rotating cube" example ports to Farmos with near-identical structure; renders to PNG.
M3.5 Multithreading (concurrent tasks) [added 2026-09-29 by user]
- Run multiple tasks concurrently. If tasks issue conflicting writes to the same variable
  (e.g. one task does a=1 while another does a=2 at the same time), that is an error and must be reported.
- DECIDED 2026-09-29: compile-time detection. Same-variable writes with identical value (a=1 & a=1) => warning, still compiles. Different values (a=1 & a=2) => error. Read in one task + write in another on same variable => error.
- AC (draft): concurrent independent tasks run correctly; conflicting a=1 / a=2 raises the defined error deterministically;
  non-conflicting shared reads OK; hello-world size unchanged; fixtures under spec/tests/M3.5/.
- Order: after M3 passes audit, before M4.
M4 Ray tracing, reflection, materials
- BVH acceleration, shadows, mirror reflection, refraction (glass, IOR), PBR (roughness/metalness),
  emissive, textures (optional), multi-threaded rendering, progressive samples.
- AC: Cornell-box and "spheres with glass + mirror" demos render correctly; demo binary <= 300 KB stripped;
  peak RAM for 800x600 render <= 64 MB.
M5 Physics
- World, RigidBody (mass, velocity, restitution, friction), gravity, sphere/box/plane colliders,
  broadphase + narrowphase, fixed-step integration, sync to scene objects.
- DECIDED 2026-10-09 (PM, all option A): module `farmos:physics`; RigidBody↔Object3D link with
  colliders on body; semi-implicit Euler dt=1/60; sequential impulse 10 iters; AABB/sort-and-sweep;
  Dynamic+Static+Kinematic; full angular; size S_demo−S_scene ≤ 64 KiB and hello ≤ 20 KiB.
  Stacking AC uses spheres (not boxes).
- AC: see spec/M5-physics.md AC-M5-01..12 — stacking/bouncing demo stable for 10 s simulated time;
  deterministic fixture outputs; unused physics not linked (hello ≤ 20 KiB);
  demo - scene-hello stripped delta ≤ 64 KiB.
- Open questions: none.
- Spec: spec/M5-physics.md (FINAL); fixtures: spec/tests/M5/.
M6 Size/resource hardening + docs
- Size budget CI check (hello ≤ 20 KiB; rotating-cube ≤ 96 KiB; glass+mirror ≤ 300 KiB;
  M5 S_demo−S_scene ≤ 64 KiB; M4 800×600 peak RAM ≤ 64 MiB; unused-math hello ≤ 20 KiB;
  no thread-start imports without parallel/renderPath), benchmarks, language reference,
  getting-started + porting guides, 5 example programs under examples/.
- DECIDED 2026-10-10 (PM, all option A): for-continue runs update; T[] assign/param/return aliases;
  run_approx default epsilon 1e-10; E0605 for non-overloadable operator forms; CI DCE thread-symbol
  gate; English normative docs + keep docs/使用手册.md informative.
- AC: all budgets enforced in CI; docs build (scripts\build_docs.ps1); fixtures under spec/tests/M6/;
  see AC-M6-01..15 in spec/M6-hardening.md.
- Open questions: none.
- Spec: spec/M6-hardening.md (FINAL); fixtures: spec/tests/M6/.

M7 / Farmos v1.01 — optional Vulkan path tracing [added 2026-10-13 by user]
- Accelerate the existing M4 path tracer with an **optional Vulkan compute** backend.
  Scene raster (M3 `render`) stays CPU-only in this milestone.
- DECIDED 2026-10-13 (PM, user chose Vulkan path-trace):
  - API: keep M4 `renderPath` semantics; add an explicit opt-in for GPU (exact shape left to
    spec — prefer a clear flag/method over silent auto-only).
  - If Vulkan is unavailable, init fails, or GPU path errors: **must** fall back to the existing
    CPU path tracer with the same observable image contract as M4 (or a documented soft-fail
    mode for fixtures that assert "GPU attempted").
  - DCE / size: programs that never use path tracing or GPU MUST keep hello ≤ 20 KiB and must
    not pull Vulkan loader / GPU runtime symbols (same spirit as M3.5/M6 thread DCE).
  - Shading / sampling: match M4 normative behavior where fixtures require bit-stable or
    golden-comparable output; GPU may use a documented tolerance only where the spec allows.
  - Platform: Windows x64 primary (TommyLaptop); Linux CI may skip or soft-skip GPU fixtures
    when no Vulkan device is present — CPU fixtures still required.
  - Out of scope for v1.01: GPU raster, DX12/CUDA/OpenCL, textures beyond M4, windowing,
    full RTX hardware ray-query pipeline (compute path tracer is enough).
- DECIDED 2026-10-13 (PM, all OQ option A): setPathBackend/getPathBackend/isVulkanPathAvailable;
  STRICT env → trap 117 else silent CPU fallback; GPU `# png_tol: 2`; GPU-linked ≤ 400 KiB;
  CPU glass+mirror ≤ 300 KiB; trap 118 invalid backend; FORCE_FAIL hook; requires_vulkan soft-skip.
- DECIDED 2026-10-13 (PM, OQ-M7-01..04 all option A):
  - API: `setPathBackend` / `getPathBackend` / `isVulkanPathAvailable`; PATH_BACKEND_CPU=0,
    PATH_BACKEND_VULKAN=1; keep `renderPath`.
  - STRICT: `FARMOS_PATH_GPU_STRICT=1` → trap 117; default silent CPU fallback; backend stays VULKAN.
  - GPU images: `# png_tol: 2` vs CPU/M4 golden; CPU bit-identical; GPU float32 OK.
  - Size: GPU-linked ≤ 400 KiB stripped; CPU glass+mirror ≤ 300 KiB.
  - Also binding: trap 118 at `setPathBackend`; `FARMOS_VULKAN_FORCE_FAIL=1`; `# requires_vulkan`
    soft-skip on Linux CI.
- AC: see AC-M7-01..16 in `spec/M7-vulkan-path.md`.
- Fixtures: `spec/tests/M7/` (9 `.fm` + 3 eggtooth script ACs).
- Open questions: none.
- Order: after M6 / post-M6 residuals; coding milestone for poteto.
- Spec: `spec/M7-vulkan-path.md` (**FINAL**); poteto implements; eggtooth audits against ACs.

M8 — Path tracer engine speedup [added 2026-10-15 by PM/user]
- Goal: speed M4 CPU + M7 Vulkan path without changing silver-still artwork or forcing lower spp.
- Status: FINAL.
- DECIDED 2026-10-15 (PM, all OQ option A):
  - RR off by default; `setPathRoulette` / `FARMOS_PATH_ROULETTE=1`.
  - Additive `setPathSampling`; default `PATH_SAMPLING_M4`; MIS behind `PATH_SAMPLING_MIS`.
  - Additive adaptive/progressive API only; `renderPath` default unchanged.
  - ≥2× TommyLaptop Vulkan; ≥1.5× Linux CPU on silver still 1920×1080 @ 256 spp vs M7 baseline.
  - Compat preserves M4 SHA; speed mode may use `# png_tol` / separate goldens.
  - Also binding: artwork composition locked; denoise OUT; 256 spp OK; trap 119; RR start depth ≥ 3;
    CPU fallback + DCE unchanged.
- Out: denoise-as-product; GPU raster; breaking renderPath without migration.
- AC: see AC-M8-01..16 in `spec/M8-path-speed.md`.
- Open questions: none.
- Spec: `spec/M8-path-speed.md` (**FINAL**); fixtures: `spec/tests/M8/`.


M9 — Hardware ray tracing [added 2026-10-16 by PM/user]
- Accelerate M4 path with hardware RT (Vulkan AS / rayQuery in compute); CPU + M7/M8 compute fallback; DCE hello.
- Status: **FINAL** (PM 2026-10-16: OQ-M9-01..07 all A; PM 2026-10-18: OQ-M9-08 A).
- DECIDED 2026-10-16 (OQ-M9-01..07 all option A):
  - OQ-M9-01 A: PATH_BACKEND_VULKAN_RT=2 + isVulkanRayTracingAvailable(); trap 118 valid {0,1,2}.
  - OQ-M9-02 A: Vulkan AS/RT only (not OptiX).
  - OQ-M9-03 A: VK_KHR_ray_query inside compute (not full RT pipeline).
  - OQ-M9-04 A: silver still 1920×1080 @ 256 spp TommyLaptop: median ≤4 s + ≥8× vs post-M8 compute.
    (**Historical** — **superseded by OQ-M9-08 A**.)
  - OQ-M9-05 A: soft RT→compute→CPU; STRICT: selected HW RT or trap 117.
  - OQ-M9-06 A: RT-linked ≤500 KiB; hello ≤20; compute ≤400; CPU glass+mirror ≤300.
  - OQ-M9-07 A: denoise still OUT.
  - Also binding: M7 ray-query non-goal reopened for M9; DLSS/frame-gen/Swapper OUT; CPU+compute fallback + DCE.
- DECIDED 2026-10-18 (OQ-M9-08 A):
  - Measured floor TommyLaptop Arc CLEAN tip `eaeb0c6`: RT median **4881 ms** / compute ~**29335 ms** / ~**6.01×**. Original ≤4 s / ≥8× unreachable without regress.
  - **A**: primary ≤ **5000 ms** after warm-up; secondary ≥ **5×** vs post-M8 compute (`eaeb0c6` PASSES).
  - Body §9 / AC-M9-13..14 binding under OQ-M9-08 A.
- Out: denoise-as-product; OptiX; full RT pipeline; DLSS/frame-gen; Swapper-style DLL hacks; GPU raster.
- AC: see AC-M9-01..17 in `spec/M9-hardware-rt.md` (wall-clock ACs binding under OQ-M9-08 A: ≤5000 ms / ≥5×).
- Open questions: **none** (OQ-M9-01..08 all A).
- Spec: `spec/M9-hardware-rt.md` (**FINAL**); fixtures: `spec/tests/M9/`.

M10 — Denoise / interactive path tracing [added 2026-10-18 by PM/user]
- User chose denoise-interactive after M9 (52b1aebc). Reopens denoise (was OUT in M9).
- Status: **FINAL** (PM 2026-10-18: OQ-M10-01..08 A; PM 2026-10-19: OQ-M10-09 A).
- DECIDED 2026-10-18 (OQ-M10-01..08 all option A) — still binding except OQ-M10-03 wall-clock / stills-gate spp **superseded** by OQ-M10-09 A:
  - OQ-M10-01 A: stills denoise + progressive interactive API; **no** OS window/swapchain (preview via path + savePNG/readback).
  - OQ-M10-02 A: Farmos-owned Vulkan/CPU spatial (A-trous/bilateral); **no** OIDN/NVIDIA DLL.
  - OQ-M10-03 A: silver still 1920×1080 ≤**64 spp** + denoise; median ≤**1500 ms** TommyLaptop HW RT; `# png_tol: 8` vs 256-spp undenoised. (**Historical** — wall-clock / stills-gate spp **superseded** by OQ-M10-09 A; `# png_tol: 8` remains.)
  - OQ-M10-04 A: first useful ≤**250 ms** (≤8–16 spp); subsequent steps ≤**100 ms**; no window — **unchanged** by OQ-M10-09.
  - OQ-M10-05 A: additive `setPathDenoise` / `getPathDenoise` + `PATH_DENOISE_*`; default **OFF**.
  - OQ-M10-06 A: DLSS/vendor upscalers **OUT**.
  - OQ-M10-07 A: denoise-linked ≤**600 KiB**; hello ≤20; RT ≤500; compute ≤400; glass+mirror ≤300.
  - OQ-M10-08 A: denoise fail soft (leave noisy); `FARMOS_PATH_DENOISE_STRICT=1` → trap **120**-family; M9 RT STRICT unchanged.
- DECIDED 2026-10-19 (OQ-M10-09 option A; user chose relax-stills-ac):
  - Primary stills gate: ≤**8 spp** + `PATH_DENOISE_SPATIAL`; median ≤**5500 ms** TommyLaptop HW RT; `# png_tol: 8` vs 256-spp undenoised.
  - Optional secondary vs M9 4881 / denoise-OFF ~4044; tip `e15ee34` median 5046 PASSES under A.
  - **Supersedes** OQ-M10-03 A wall-clock / stills-gate spp; progressive / API / stack / size / STRICT unchanged.
- Out: DLSS/Swapper; OptiX; full game engine; OS window/swapchain; artwork variants; GPU raster.
- AC: see AC-M10-01..13 in `spec/M10-denoise-interactive.md` (AC-M10-05 binding under OQ-M10-09 A).
- Open questions: **none**.
- Spec: `spec/M10-denoise-interactive.md` (**FINAL**); fixtures: `spec/tests/M10/`.

M11 — Opt-in GGX/VNDF glossy continuation (metal-black) [added 2026-10-20 by PM/user GO]
- Goal: restore rough-metal env/background reflection via **opt-in** specular continuation (VNDF-GGX) without env-brightening artwork or forcing `setRoughness(0)`.
- Status: **FINAL** (PM/user 2026-10-20: OQ-M11-01 .. OQ-M11-08 all A).
- Evidence: CLAIM PASS tip `9c5eebd` — artwork `metal=1`,`rough=0.04` ball-center L=**0.00**; `setRoughness(0)` → L=**106.310** (= Oct-14 v1); root cause = M4 diffuse-only rough bounce × `(1−metal)` → 0. Shared CPU / Vulkan compute / Vulkan RT.
- Binding (OQ-M11-01..08 A):
  - OQ-M11-01 A: additive `setPathGlossyContinuation` / `PATH_GLOSSY_OFF` / `PATH_GLOSSY_VNDF`; default **OFF**; trap **121**.
  - OQ-M11-02 A: VNDF-GGX reflection lobe.
  - OQ-M11-03 A: apply only when `metal>0` && `rough>0` (dielectrics unchanged).
  - OQ-M11-04 A: keep RectArea NEE + balance-heuristic MIS with BSDF sample.
  - OQ-M11-05 A: no required firefly clamp in M11.
  - OQ-M11-06 A: default stays OFF for all of M11 (no language default flip).
  - OQ-M11-07 A: no new PE ceiling; DCE; prior ≤300/400/500/600 caps.
  - OQ-M11-08 A: full backend parity; no silent BSDF soft-skip.
- Binding acceptance: flag ON → silver ball-center 40×40 mean L **≥ 80** (target order ~v1 **≈106.31**); flag OFF → near-black witness L **≤ 5** / M4 goldens preserved; roughness stays **0.04**; no artwork light/env brighten; no artwork `setRoughness(0)` as product fix.
- Out: env brighten; artwork roughness=0 as product fix; default-ON; rewriting M4 goldens under OFF; DLSS/OptiX/window/GPU raster.
- PLAN note / §19 reopen: M4 listed “Indirect GGX rays on rough surfaces” out of scope — M11 **reopens** that non-goal **behind opt-in only** (same pattern as M10 vs M9 denoise). M4 body not edited; amendments live in `spec/M11-glossy-continuation.md` §19.
- AC: see AC-M11-01..12 in `spec/M11-glossy-continuation.md` (binding under A).
- Open questions: **none**.
- Spec: `spec/M11-glossy-continuation.md` (**FINAL**); fixtures: `spec/tests/M11/`.


M12 — Native Vulkan upscale (XeSS / FSR) [added 2026-10-21 by PM/user GO draft-native-upscale]
- Goal: opt-in native Vulkan temporal upscale (Intel **XeSS** primary on Arc; optional **AMD FSR2**) after path (+ optional M10 denoise); default **OFF**.
- Status: **FINAL** (PM/user 2026-10-23: OQ-M12-12 A + OQ-M12-13 A; prior OQ-01..09 A locked 2026-10-21; OQ-10/11 A locked 2026-10-22; **2026-10-25 clarification** clear-m12-backlog — ROI pin + |ΔL| vs mean_abs_dL; gates unchanged).
- Why: user wanted DLSS-Enabler for Intel/AMD; survey = wrong tool (no NGX/DLSS; offline path + PATH_DENOISE_SPATIAL; no swapchain). User chose native XeSS/FSR milestone. Enabler/Swapper/OptiScaler **OUT forever** for this milestone.
- **Reopens from M10 (explicit):**
  - OQ-M10-06 A (DLSS/vendor upscalers OUT) → **selective reopen**: native XeSS/FSR **IN**; DLSS/NGX, Enabler, Swapper, OptiScaler injection **still OUT**.
  - OQ-M10-01 A (no OS window/swapchain) → under **OQ-M12-01 A** (**headless** upscale-to-buffer + savePNG) **stands / not reopened**. OQ-M12-01 B rejected.
- Binding (OQ-M12-01..09 A — **unchanged**):
  - OQ-M12-01 A: headless buffer + savePNG; **no** OS window; M10-01 stands.
  - OQ-M12-02 A: XeSS primary + FSR2 optional; DLSS/Enabler/Swapper/OptiScaler OUT.
  - OQ-M12-03 A: camera MV + depth; object MV deferred.
  - OQ-M12-04 A: `setPathUpscale` / `PATH_UPSCALE_OFF|XESS|FSR`; default **OFF**; trap **122**.
  - OQ-M12-05 A: denoise **then** upscale when both ON.
  - OQ-M12-06 A: Vulkan compute + Vulkan RT MUST; CPU soft; STRICT 122-family.
  - OQ-M12-07 A: upscale-glue PE ≤ **700 KiB** (vendor DLLs external); DCE when OFF.
  - OQ-M12-08 A: TommyLaptop Arc smoke (960×540→1080p) + Linux soft-skip; historical SHOULD ≥1.5× **superseded** by OQ-M12-10 A then OQ-M12-12 A (SHOULD floor only).
  - OQ-M12-09 A: frame-gen OUT.
- Binding (OQ-M12-10 A / OQ-M12-11 A — PM/user 2026-10-22 oq-relax-1.2-roi; tip `edeb845` PR #20) — **accepted historical**; SHOULD floor / absolute band **superseded** by OQ-12/13 A:
  - OQ-M12-10 **A**: historical SHOULD ≥ **1.2×**; binding XeSS wall **strictly <** native; document measured **1.237**; historical SHOULD ≥1.5× superseded for SHOULD floor only; **SHOULD floor superseded by OQ-M12-12 A**.
  - OQ-M12-11 **A**: historical AC-M12-04 absolute band **[80, 140]** + **|ΔL| ≤ 5.0**; report mean_abs; demote `# png_tol: 12` / max_abs to **report-only**; historical hard png_tol:12 superseded; **absolute band superseded by OQ-M12-13 A**.
- Binding (OQ-M12-12 A / OQ-M12-13 A — PM/user 2026-10-23 oq-relax-again; tip `7f0a080` / product `fd31e46`):
  - OQ-M12-12 **A**: SHOULD speedup ≥ **1.05×** display-equivalent on TommyLaptop Arc after warm-up; binding XeSS wall **strictly <** native; document Pass1 **1.052** / Pass2 **1.162**; historical OQ-10 A ≥1.2× superseded for SHOULD floor only.
  - OQ-M12-13 **A**: AC-M12-04 primary = silver ROI mean L both lanes in **[80, 200]** and **|ΔL| ≤ 5.0**; report mean_abs; `# png_tol: 12` / max_abs **report-only**; tip L ~**187** PASS; historical OQ-11 A [80,140] superseded for absolute band only.
- **Clarification (2026-10-25; user clear-m12-backlog; post merge `f5b87f8f`):** AC-04 ROI **pinned** to M11 CLAIM box **`x=940:980,y=520:560`** @1920×1080 (**40×40**); **|ΔL| = |L_xess − L_native|** of ROI means (tip ≈ **0.650** PASS); tip **3.932** reclassified **mean_abs_dL** report-only (was mislabeled |ΔL|). OQ-12 A / OQ-13 A gate numbers **unchanged**. Status remains **FINAL**.
- Out: DLSS/NGX; Enabler/Swapper/OptiScaler; frame-gen; OptiX; GPU raster; rewriting goldens under OFF; full game engine; OS window/swapchain.
- AC: see AC-M12-01..12 in `spec/M12-native-upscale.md` (all binding under OQ-01..13 A; AC-04/05 under OQ-13 A / OQ-12 A).
- Open questions: **none**.
- Spec: `spec/M12-native-upscale.md` (**FINAL**); fixtures: `spec/tests/M12/`. M1–M11 untouched.
- Order: after M11.


M13 — Performance / more speed (post-M12) [added 2026-10-26 by PM/user GO draft-m13-perf]
- Goal: raise display-equivalent speed on TommyLaptop Arc (XeSS path) after M12 without regressing M12 binding ACs; engine + host amortization first.
- Status: **FINAL** (PM/user 2026-10-26: OQ-M13-01..08 all **accepted A**; poteto **HOLD** until PM assigns coding).
- Baseline: master tip `affc63ea` (PR #21 clear-m12-backlog; eggtooth AUDIT PASS); Arc CLAIM product `d23acfb3` Pass1 **1.264** / Pass2 **1.348** under M12 OQ-12 A.
- M12 floor (MUST NOT regress): OQ-12 A SHOULD ≥ **1.05×**; XeSS wall strictly < native; OQ-13 A ROI L **[80, 200]** + |ΔL| of means ≤ 5.0; ROI pinned `x=940:980,y=520:560` @1080p 40×40; OFF twins; trap 122; ≤700 KiB glue; frame-gen OUT; no window.
- Soft backlog (stale tip L~187/3.932 labels; LF drift): non-blocking notes only — do not block M13.
- Binding (OQ-M13-01..08 A — PM/user 2026-10-26):
  - OQ-M13-01 **A**: SHOULD ≥ **1.20×** (reclaim historical M12 OQ-10 A as M13 SHOULD); M12 ≥1.05× remains floor; document `d23acfb3` 1.264/1.348.
  - OQ-M13-02 **A**: both Pass1 and Pass2 ≥ **1.20×**; report ms; no hard ms cap yet.
  - OQ-M13-03 **A**: host + path/RT engine + aux/PSO; keep 960×540→1080p default; XeSS quality-mode OK if AC-04 holds.
  - OQ-M13-04 **A**: no new public `.fm` API; MAY env/internal hooks.
  - OQ-M13-05 **A**: MUST NOT regress M12 binding quality / OFF / size / OUT list.
  - OQ-M13-06 **A**: no new PE ceiling; keep ≤700 KiB glue; hello ≤20; prior caps.
  - OQ-M13-07 **A**: frame-gen / window / DLSS/Enabler/Swapper remain OUT.
  - OQ-M13-08 **A**: TommyLaptop Arc primary; Linux `# requires_xess` soft-skip; CPU OFF twins green.
- Out (locked): frame-gen; OS window; DLSS/Enabler; lower render-res default; lowering spp as speed strategy; artwork change; quality band widen.
- AC: AC-M13-01..09 in `spec/M13-performance.md` (binding under OQ-01..08 A).
- Spec: `spec/M13-performance.md` (**FINAL**); fixtures: `spec/tests/M13/`.
- Order: after M12.
- Open questions: **none**.


M14 — Performance / more speed (post-M13) [added 2026-10-29 by PM/user GO draft-m14 more-perf]
- Goal: raise display-equivalent speed on TommyLaptop Arc (XeSS path) beyond M13 host amort without regressing M12/M13 binding meters; path/RT engine + aux/PSO first.
- Status: **FINAL** (PM/user 2026-10-29: OQ-M14-01..08 all **accepted A**; poteto **HOLD** until PM assigns coding).
- Baseline: master tip `47949c82` (PR #22 M13 squash; eggtooth AUDIT PASS); product `945c13ae` inproc Pass1 **1.455** / Pass2 **1.607**.
- Floors (MUST NOT regress / weaken): M12 process-per-frame ≥ **1.05×**; XeSS < native; ROI [80,200] + |ΔL|≤5 pinned `x=940:980,y=520:560`; OFF; trap 122; ≤700 KiB; frame-gen OUT; M13 inproc `FARMOS_PATH_FRAME_MS` ≥ **1.20×** both Pass1+Pass2.
- Soft backlog (post #23 squash `218f6ef1`; product `4d7523ec` unchanged): H2 meter-integrity note (inproc GPU-pipeline vs `945c13ae` equal-work); Pass2 proc 1.139 thin headroom — monitor; dirty chrome+msedge informational. M13 AC-04 / OQ-01 meter naming + M12 pinned-ROI L~187 fixture-README mixup addressed as doc clarity only — do not reopen OQs / weaken ≥1.50×.
- Binding (OQ-M14-01..08 A — PM/user 2026-10-29):
  - OQ-M14-01 **A**: SHOULD ≥ **1.50×** both Pass1+Pass2 inproc `FARMOS_PATH_FRAME_MS`; document `945c13ae` 1.455/1.607; M13 ≥1.20× + M12 ≥1.05× floors stand.
  - OQ-M14-02 **A**: both Pass1+Pass2 ≥ **1.50×**; report ms; no hard ms cap yet.
  - OQ-M14-03 **A**: path/RT engine + aux/PSO primary; keep 960×540→1080p; XeSS quality OK if AC-04 holds.
  - OQ-M14-04 **A**: no new public `.fm` API; MAY env/internal hooks; keep `FARMOS_PATH_FRAME_MS`.
  - OQ-M14-05 **A**: MUST NOT regress M12 binding or weaken M13 ≥1.20×.
  - OQ-M14-06 **A**: no new PE ceiling; keep ≤700 KiB glue; hello ≤20; prior caps.
  - OQ-M14-07 **A**: frame-gen / window / DLSS/Enabler/Swapper remain OUT.
  - OQ-M14-08 **A**: TommyLaptop Arc primary; Linux `# requires_xess` soft-skip; CPU OFF twins green.
- Out (locked): frame-gen; OS window; DLSS/Enabler; lower render-res default; lowering spp as speed strategy; artwork change; quality band widen.
- AC: AC-M14-01..10 in `spec/M14-performance.md` (binding under OQ-01..08 A).
- Spec: `spec/M14-performance.md` (**FINAL**); fixtures: `spec/tests/M14/`.
- Order: after M13.
- Open questions: **none**.

M15 — Liquid / fluid simulation + MP4 export (~5 s) [added 2026-10-31 by PM/user GO draft-m15 liquid-mp4]
- Goal: CPU-first 2D height-field / shallow-water (`farmos:liquid`) plus `Renderer.saveMP4` via external ffmpeg; ~5 s @ 30 fps @ 640×360.
- Status: **FINAL** spec (`spec/M15-liquid-mp4.md`; OQ-M15-01..10 all **accepted A**). Implemented on `cursor/m15-liquid-mp4-7365`. CLAIM **PASS** 2026-10-31 TommyLaptop Windows @ `93b50c45` (hello PE **13824**; S_liq−S_scene **4096**; glue **512**; mp4 5.000 s / 30 fps / 150 / 640×360). Linux 15/15 + size/DCE green. Product `4d7523ec` unchanged. HOLD merge until eggtooth AUDIT PASS.
- Binding: XZ +Y; `LiquidWorld` + `HeightField` + `step` / optional `linkMesh`; two `step()` per display frame at dt=1/60; trap **123**-family; `# requires_ffmpeg`; `S_liq − S_scene ≤ 96 KiB`; hello ≤20; ffmpeg external.
- OUT: cloth; 3D SPH; fluid↔rigid coupling; PE-bundled H.264; window; frame-gen; DLSS/Enabler; weaken M12–M14; edit M5 body (RD-M5-04 stands for M5).
- Spec: `spec/M15-liquid-mp4.md` (**FINAL**); fixtures: `spec/tests/M15/`.
- Order: after M14.
- Open questions: **none**.

## Team
- Projects Manager: plan, priorities, handoffs, acceptance.
- poteto: all coding (via Cursor cloud agents), per milestone, reports status/blockers.
- eggtooth: audits each milestone claim against the ACs above.
- Language/graphics spec architect bot (requested from dr eggbot): writes the language spec + test cases per milestone ahead of coding. Does not code.

## Environment (updated 2026-10-13)
- GitHub repo: https://github.com/Tommy-likecode/Farmos (private). Cloud agents open PRs; sync back to
  D:\Tommy\Farmos on TommyLaptop for Windows build/test and eggtooth audit. Watt Toolkit if GitHub unreachable.
- Linux CI on cloud for non-GPU paths; GPU acceptance primarily on TommyLaptop when a Vulkan device exists.
