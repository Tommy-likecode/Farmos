# Farmos

## M9 — hardware ray tracing

Spec: `spec/M9-hardware-rt.md` (FINAL). Fixtures: `spec/tests/M9/` (ctest `m9_*`).

- `PATH_BACKEND_VULKAN_RT=2` / `isVulkanRayTracingAvailable()`. Trap 118 valid set `{0,1,2}`.
- Vulkan AS + `VK_KHR_ray_query` inside compute (not OptiX, not SBT RT pipeline). Soft fallback HW RT → compute → CPU; STRICT trap 117. `FARMOS_VULKAN_RT_FORCE_FAIL=1`.
- M8 RR/MIS/adaptive apply on backend 2 when enabled. CPU compat SHA unchanged. HW RT images: `# png_tol: 2`.
- Size: hello ≤ 20 KiB unused; compute-only ≤ 400 KiB; **RT-linked ≤ 500 KiB**.
- Linux CI: `# requires_vulkan_rt: true` soft-skips without a ray-query device. AC-M9-12..14 (≤5000 ms / ≥5×, OQ-M9-08 A; OQ-M9-04 superseded) are TommyLaptop gates.

Linux: `bash scripts/run_suite_linux.sh build/farmc spec/tests/M9`

---

## M8 — path tracer engine speedup

Spec: `spec/M8-path-speed.md` (FINAL). Fixtures: `spec/tests/M8/` (ctest `m8_*`).

- Opt-in Russian roulette (`setPathRoulette`; default off). Better RectArea sampling / MIS (`setPathSampling`; default `PATH_SAMPLING_M4`). Additive adaptive spp API (default off).
- Compat / default CPU remains bit-identical to M4 goldens. Invalid sampling mode: trap 119.
- Vulkan: 8×8 workgroups, world-space BVH, all spp in one dispatch. M7 fallback / STRICT / FORCE_FAIL unchanged.
- Artwork bench: `examples/artwork_silver_still.fm` @ 1920×1080 / 256 spp; scripts in `spec/tests/M8/bench/`.

Linux: `bash scripts/run_suite_linux.sh build/farmc spec/tests/M8`

---

## M7 / v1.01 — optional Vulkan path tracing

Spec: `spec/M7-vulkan-path.md` (FINAL). Fixtures: `spec/tests/M7/` (ctest `m7_*`).

- `Renderer.setPathBackend` / `getPathBackend` / `isVulkanPathAvailable`; `PATH_BACKEND_CPU=0`, `PATH_BACKEND_VULKAN=1`. `renderPath` is unchanged.
- Default CPU (M4, bit-identical). Vulkan compute after explicit opt-in; silent CPU fallback unless `FARMOS_PATH_GPU_STRICT=1` (trap 117). Invalid backend: trap 118.
- Dynamic load of the Vulkan loader; hello / CPU-only programs must not import Vulkan symbols.
- Linux CI: GPU fixtures with `# requires_vulkan: true` soft-skip when no device is present.

`farmc version` → `farmc 1.01`. Linux: `bash scripts/run_suite_linux.sh build/farmc spec/tests/M7`

---

## M6 — size / resource hardening + docs

Spec: `spec/M6-hardening.md` (FINAL). Fixtures: `spec/tests/M6/` (ctest `m6_*`, 15 cases).

- Language amendments: `continue` in `for` runs the update; dynamic `T[]` assign/param/return aliases; `operator !` / `=` → E0605.
- Harness: `run_approx` default epsilon **1e-10** when `# epsilon:` is omitted; `# sha256:` is the PNG gate (golden file optional).
- CI: `scripts/check_size_budgets.ps1` (B-01..B-07), `scripts/build_docs.ps1`, examples `01`..`05`, benchmarks BM-a..d.
- Docs (English normative): `docs/language-reference.md`, `docs/getting-started.md`, `docs/porting-guide.md`. `docs/使用手册.md` stays informative.

Linux: `bash scripts/run_suite_linux.sh build/farmc spec/tests/M6`

---

## M5 — physics

`import { … } from "farmos:physics"`. Spec: `spec/M5-physics.md`. Fixtures: `spec/tests/M5/` (ctest `m5_*`).

---

## M4 — path tracing

`Renderer.renderPath(scene, camera)` is a CPU path tracer on the existing M3 `Renderer`. `render()` stays the M3 rasterizer. Spec: `spec/M4-ray.md`. Fixtures: `spec/tests/M4/` (ctest `m4_*`).

The BVH/integrator/tile pool live in `runtime/farm_ray.c` and are linked only when `renderPath` is used. Unused `farmos:scene` imports still DCE. `FARMOS_THREADS` unset means 1 worker for `renderPath` (not the M3.5 CPU default).

Linux: `bash scripts/run_suite_linux.sh build/farmc spec/tests/M4`

---

## M3 — `farmos:scene`

Three.js-like scene graph and CPU rasterizer (`import { … } from "farmos:scene"`). Spec: `spec/M3-scene.md`. Fixtures: `spec/tests/M3/` (`run_png` goldens plus API tests).

Unused `farmos:scene` imports are dead-code eliminated (AC-M3-01 / AC-M3-11): they must not change hello size or M1/M2 compile flags / linked runtime. Scene programs additionally pass `-ffp-contract=off` and link `farm_math.c` + `farm_scene.c`.

Linux: `scripts/run_m3_linux.sh build/farmc` after a CMake build. DCE/size gate: `python3 scripts/test_unused_scene_dce.py build/farmc .` (Windows: `scripts/test_unused_scene_dce.ps1`).

---

# Farmos M1 — `farmc`

C++17 compiler for the Farmos M1 core language. Pipeline: lexer → parser → AST → semantic check → emit portable C11 → invoke system C compiler.

## Build (Windows)

```powershell
cd D:\Tommy\Farmos
# Ensure clang/gcc (LLVM-MinGW) and CMake are on PATH
.\scripts\build_and_test.ps1
```

Or:

```powershell
cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_C_COMPILER=clang
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

## CLI

```
farmc build hello.fm -o hello.exe
farmc run hello.fm
farmc version
```

Set `FARM_CC` to override the C compiler; set `FARM_RUNTIME` to the `runtime/` directory if needed.

## C backend requirement (important)

The C code emitted by `farmc` uses **GNU statement-expressions** (`({ ... })`) and therefore requires **clang or gcc** (e.g. LLVM-MinGW on Windows) as the C backend.

**MSVC `cl` is not supported** as the C backend for generated code. (You may still build the `farmc` compiler itself with other toolchains, but linking user programs goes through clang/gcc.)

See `docs/NOTES.md` for more implementation notes.

## Tests

- Spec conformance: `spec/tests/M1/` (49 fixtures) via ctest `m1_*`; umbrella `m1_conformance` is opt-in (`ctest -L conformance_bundle`)
- Local regressions: `tests/local/` via ctest `local_*`
