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

## Team
- Projects Manager: plan, priorities, handoffs, acceptance.
- poteto: all coding (via Cursor cloud agents), per milestone, reports status/blockers.
- eggtooth: audits each milestone claim against the ACs above.
- Language/graphics spec architect bot (requested from dr eggbot): writes the language spec + test cases per milestone ahead of coding. Does not code.

## Environment (updated 2026-10-13)
- GitHub repo: https://github.com/Tommy-likecode/Farmos (private). Cloud agents open PRs; sync back to
  D:\Tommy\Farmos on TommyLaptop for Windows build/test and eggtooth audit. Watt Toolkit if GitHub unreachable.
- Linux CI on cloud for non-GPU paths; GPU acceptance primarily on TommyLaptop when a Vulkan device exists.
