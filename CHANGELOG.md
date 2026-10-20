# Changelog

## Unreleased

M11 — opt-in GGX/VNDF glossy continuation (`spec/M11-glossy-continuation.md` FINAL):

- Additive `PATH_GLOSSY_OFF` / `PATH_GLOSSY_VNDF`, `setPathGlossyContinuation` / `getPathGlossyContinuation`. Default **OFF** (M4 goldens unchanged). Invalid mode traps **121** at the setter.
- When ON, rough metals (`metal>0` && `rough>0`) sample a Heitz 2018 VNDF-GGX reflection continuation. Throughput is `F * G1(L)` (unbiased Smith-GGX identity). Dielectrics / glass / `rough==0` stay M4 cosine / delta.
- CPU (`farm_ray.c`), Vulkan compute (`farm_vk_path.comp`), and Vulkan RT (`farm_vk_rt_path.comp`) share the estimator. No BSDF soft-skip. RectArea NEE kept; glossy metals use balance-heuristic MIS with the VNDF pdf.
- Optional `FARMOS_PATH_GLOSSY=1` if the setter was never called; method wins. Orthogonal to sampling / denoise / progressive.
- Fixtures: `spec/tests/M11/`. Artwork ROI: ON L≥80 / OFF L≤5; roughness stays 0.04; no env brighten. Stopgap `setRoughness(0)` → L=106.310 is documented, not shipped as the fix.
- Size: no new PE cap (OQ-M11-07 A).

M10 — denoise / interactive path tracing (`spec/M10-denoise-interactive.md` FINAL):

- Additive `PATH_DENOISE_OFF` / `PATH_DENOISE_SPATIAL`, `setPathDenoise` / `getPathDenoise` / `isPathDenoiseAvailable`. Default **OFF** (M4/M9 goldens unchanged). Invalid mode traps **120** at `setPathDenoise`.
- Farmos-owned spatial denoise (A-trous / bilateral class). Vulkan compute A-trous when HW RT + denoise are linked; cheap CPU plus-kernel fallback. No OIDN, NVIDIA denoise DLL, OptiX, DLSS, or Swapper.
- Soft denoise fail leaves the noisy path image. `FARMOS_PATH_DENOISE_STRICT=1` + fail → trap **120** (`runtime error: path denoise failed`). `FARMOS_PATH_DENOISE_FORCE_FAIL=1` test hook. M9 STRICT 117 / backends `{0,1,2}` unchanged.
- Progressive preview: `beginPathPreview` / `advancePathPreview` / `finishPathPreview` / `getPathPreviewSpp`. No OS window or swapchain; pixels via `savePNG` / framebuffer. Preview dispatches at 1/16 res with a cheap resolve; stills keep 1080p.
- Size/DCE: denoise-linked ≤ **600 KiB**; hello / glass+mirror / compute / RT budgets unchanged. `farm_denoise.c` is linked only when denoise API is used.
- Fixtures: `spec/tests/M10/`. Artwork denoise bench: `spec/tests/M10/bench/`. Artwork composition remains locked.

M9 — hardware ray tracing (`spec/M9-hardware-rt.md` FINAL):

- `PATH_BACKEND_VULKAN_RT = 2` and `Renderer.isVulkanRayTracingAvailable()`. Trap **118** valid set is `{0,1,2}`; fixtures that used `setPathBackend(2)` as invalid now use `3`.
- Stack: Vulkan BLAS/TLAS + `VK_KHR_ray_query` inside a compute shader. Not OptiX, not a full ray-tracing pipeline / SBT. Denoise / DLSS / frame-gen / Swapper / GPU raster remain out of scope.
- Soft fallback: HW RT → compute Vulkan → CPU; stored backend stays `2`. `FARMOS_PATH_GPU_STRICT=1` with backend 2 traps **117** (no silent downgrade). `FARMOS_VULKAN_RT_FORCE_FAIL=1` forces HW RT fail for fixtures; `FARMOS_VULKAN_FORCE_FAIL=1` also fails HW RT when backend is 2.
- M8 RR / MIS / adaptive apply on backend 2 when enabled. CPU compat SHA unchanged.
- Images: HW RT `# png_tol: 2` vs CPU/M4; harness `# requires_vulkan_rt:` soft-skip without a ray-query device.
- Size/DCE: hello ≤ 20 KiB with no Vulkan/RT symbols unused; CPU glass+mirror ≤ 300 KiB; compute-only ≤ 400 KiB; **RT-linked ≤ 500 KiB**.
- Fixtures: `spec/tests/M9/`. Artwork bench notes: `spec/tests/M9/bench/`.

M8 — path tracer engine speedup (`spec/M8-path-speed.md` FINAL):

- `setPathRoulette` / `getPathRoulette` (default off). Optional `FARMOS_PATH_ROULETTE=1` if the method was never called. RR start depth ≥ 3; RNG slot `depth*16+15`; delta glass/mirror skips RR.
- `PATH_SAMPLING_M4` (0), `PATH_SAMPLING_MIS` (1), `PATH_SAMPLING_AREA_SOLID` (2); `setPathSampling` / `getPathSampling`. Invalid mode traps **119** (`runtime error: invalid path sampling mode`). Optional `FARMOS_PATH_SAMPLING` bench hook.
- `setPathAdaptiveSpp` / `getPathAdaptiveSpp` / `setPathAdaptiveTarget` / `getPathAdaptiveTarget`; default off (M4 spp count).
- CPU: local-space BLAS AABB tests (compat-safe). GPU: world-space triangle BVH, 8×8 workgroup, one dispatch per `renderPath` for all samples.
- Fixtures: `spec/tests/M8/`. Artwork bench: `spec/tests/M8/bench/`. Denoise is out of scope.

Runtime extras (not a milestone):

- `CylinderGeometry` on `farmos:scene` (Y-up, `radius` / `height` / `radialSegments`; default 32 segments). Mesh, raster, BVH, and `renderPath` consume the same triangle data as box/sphere/plane.
- `random01(): float` on `farmos:math` — `[0, 1)`, entropy-backed (`QueryPerformanceCounter` / `clock_gettime`). Unused: hello size/symbols unchanged.
- `examples/artwork_silver_still.fm` — 1920×1080 path-traced still (silver sphere + three random matte shapes). Vulkan when available, else CPU; 256 samples, 6 bounces; large overhead `RectAreaLight` (no DirectionalLight); silver roughness 0.04; writes `artwork_silver_still.png` in CWD.

## v1.01 — 2026-10-13

Optional **Vulkan compute** path-tracing backend (M7).

- `farmos:scene` exports `PATH_BACKEND_CPU` (0) and `PATH_BACKEND_VULKAN` (1).
- `Renderer.setPathBackend` / `getPathBackend` / `isVulkanPathAvailable`. `renderPath` signature is unchanged.
- Default remains the M4 CPU integrator. GPU is used only after an explicit `setPathBackend(PATH_BACKEND_VULKAN)`.
- If Vulkan is missing, init fails, or `FARMOS_VULKAN_FORCE_FAIL=1`: silent CPU fallback (backend stays VULKAN). `FARMOS_PATH_GPU_STRICT=1` traps **117** (`runtime error: vulkan path failed`) instead.
- Invalid backend at `setPathBackend` traps **118** (`runtime error: invalid path backend`).
- Vulkan loader is `LoadLibrary` / `dlopen` on first GPU attempt; hello and CPU-only `renderPath` do not take a load-time vulkan dependency.
- GPU images: `# png_tol: 2` vs CPU/M4 golden; GPU accumulator is float32. CPU goldens stay bit-identical to M4.
- Size: hello ≤ 20 KiB with no Vulkan symbols; CPU glass+mirror ≤ 300 KiB; GPU-linked demo ≤ 400 KiB.
- `farmc version` prints `farmc 1.01`.
