# Farmos

## M14 — performance / more speed (post-M13)

Spec: `spec/M14-performance.md` (FINAL). Fixtures: `spec/tests/M14/` (ctest `m14_*`).

- In-process `FARMOS_PATH_FRAME_MS` SHOULD ≥ **1.50×** both Pass1 and Pass2 on TommyLaptop Arc (960×540→1080p). M13 ≥ **1.20×** inproc and M12 ≥ **1.05×** process-per-frame remain floors.
- Levers: persist AS `PREFER_FAST_TRACE`; defer XeSS display host readback to `savePNG`; dummy XeSS PSO execute during path wait. No new public `.fm` API.
- Inproc `FRAME_MS` is GPU-pipeline after H2 (XeSS 1080p host RGB flushed on `savePNG`). Do not claim equal-work vs `945c13ae`.
- **OUT:** frame-gen, OS window, DLSS/Enabler/Swapper/OptiScaler. Official XeSS only. Arc CLAIM **PASS** at measured tip `d409152e` / product `4d7523ec`: inproc Pass1 **3.184×** / Pass2 **3.913×**.

Linux: `bash scripts/run_suite_linux.sh build/farmc spec/tests/M14`

---

## M12 — native Vulkan upscale (XeSS / FSR)

Spec: `spec/M12-native-upscale.md` (FINAL). Fixtures: `spec/tests/M12/` (ctest `m12_*`).

- `setPathUpscale` / `getPathUpscale` / `PATH_UPSCALE_OFF=0` / `PATH_UPSCALE_XESS=1` / `PATH_UPSCALE_FSR=2`. Default **OFF**. Invalid mode or resolution: trap **122**.
- Headless upscale-to-buffer + `savePNG` (no OS window / swapchain). Pipeline when both ON: **denoise then upscale**.
- XeSS Vulkan is primary (Arc); FSR2 optional. **OUT forever:** DLSS/NGX, DLSS-Enabler, Swapper, OptiScaler, frame-gen, OptiX.
- Vulkan compute + Vulkan RT run upscale when the SDK is present; CPU soft-fails (render-res) unless `FARMOS_PATH_UPSCALE_STRICT=1`.
- Size: upscale-glue PE ≤ **700 KiB**; vendor SDK DLLs external (`scripts/fetch_upscale_sdk.sh`); DCE when unused; hello ≤ 20 KiB; prior ≤300/400/500/600 unchanged.
- Linux without SDK: `# requires_xess:` / `# requires_fsr:` soft-skip; OFF twins + trap + roundtrip still green. Arc 960×540→1080p smoke/timing is a TommyLaptop gate.

Linux: `bash scripts/run_suite_linux.sh build/farmc spec/tests/M12`

---

## M11 — opt-in GGX/VNDF glossy continuation

Spec: `spec/M11-glossy-continuation.md` (FINAL). Fixtures: `spec/tests/M11/` (ctest `m11_*`).

- `setPathGlossyContinuation` / `getPathGlossyContinuation` / `PATH_GLOSSY_OFF=0` / `PATH_GLOSSY_VNDF=1`. Default **OFF** (M4 goldens unchanged). Invalid mode: trap **121**.
- When ON, rough metals (`metalness>0` && `roughness>0`) bounce with VNDF-GGX instead of cosine × `(1−metal)`. Dielectrics, glass, and delta-mirrors (`rough==0`) are unchanged.
- Same estimator on CPU, Vulkan compute, and Vulkan RT. No silent BSDF soft-skip. RectArea NEE stays; glossy metals MIS with the VNDF pdf (balance heuristic).
- Artwork silver AC: roughness stays **0.04**; flag ON → ball-center 40×40 mean L ≥ **80** (target ~v1 ≈106.31); flag OFF → L ≤ **5** / M4 SHA. Do not env-brighten; `setRoughness(0)` restores v1 L=106.310 exactly but is **not** the product fix.
- Size: no new PE ceiling; DCE unused glossy; prior ≤20/300/400/500/600 KiB caps.

Linux: `bash scripts/run_suite_linux.sh build/farmc spec/tests/M11`

---

## M10 — denoise / interactive

Spec: `spec/M10-denoise-interactive.md` (FINAL). Fixtures: `spec/tests/M10/` (ctest `m10_*`).

- `setPathDenoise` / `getPathDenoise` / `PATH_DENOISE_OFF=0` / `PATH_DENOISE_SPATIAL=1`. Default OFF. Invalid mode: trap **120**.
- Farmos-owned spatial denoise (A-trous). GPU compute filter on the RT path; cheap CPU fallback. No OIDN / NVIDIA DLL / OptiX / DLSS. Soft fail leaves noisy; `FARMOS_PATH_DENOISE_STRICT=1` traps 120.
- Progressive preview API (no window). Quarter-res dispatch + AS warm on begin. First useful / step wall-clock ACs are TommyLaptop HW RT gates.
- Size: denoise-linked ≤ 600 KiB; prior hello/RT/compute/glass budgets unchanged.

Linux: `bash scripts/run_suite_linux.sh build/farmc spec/tests/M10`

---

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

## M15 — liquid + MP4

`import { LiquidWorld, HeightField } from "farmos:liquid"` plus `Renderer.saveMP4`. Spec: `spec/M15-liquid-mp4.md` (FINAL). Fixtures: `spec/tests/M15/` (ctest `m15_*`).

5 s demo (needs `ffmpeg` on PATH):

```
farmc build examples/08_liquid_5s.fm -o liquid_5s
./liquid_5s
```

Writes `liquid_5s.mp4` in CWD (640×360, 30 fps, ~5.0 s). Linux CI soft-skips `# requires_ffmpeg` when ffmpeg is absent. CPU liquid fixtures stay green.

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
