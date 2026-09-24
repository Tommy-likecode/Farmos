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
  optional window via minimal platform layer. GPU backend is a later stretch goal.

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
M4 Ray tracing, reflection, materials
- BVH acceleration, shadows, mirror reflection, refraction (glass, IOR), PBR (roughness/metalness),
  emissive, textures (optional), multi-threaded rendering, progressive samples.
- AC: Cornell-box and "spheres with glass + mirror" demos render correctly; demo binary <= 300 KB stripped;
  peak RAM for 800x600 render <= 64 MB.
M5 Physics
- World, RigidBody (mass, velocity, restitution, friction), gravity, sphere/box/plane colliders,
  broadphase + narrowphase, fixed-step integration, sync to scene objects.
- AC: stacking/bouncing demo stable for 10 s simulated time; deterministic test outputs.
M6 Size/resource hardening + docs
- Size budget CI check, benchmarks, language reference, getting-started guide, 5 example programs.
- AC: all budgets enforced in CI; docs build.

## Team
- Projects Manager: plan, priorities, handoffs, acceptance.
- poteto: all coding (via Cursor cloud agents), per milestone, reports status/blockers.
- eggtooth: audits each milestone claim against the ACs above.
- Language/graphics spec architect bot (requested from dr eggbot): writes the language spec + test cases per milestone ahead of coding. Does not code.

## Environment (updated 2026-09-24)
- No GitHub/cloud repo. Project lives at D:\Tommy\Farmos on TommyLaptop (Windows), local git only. All CI-style checks are local ctest runs.
