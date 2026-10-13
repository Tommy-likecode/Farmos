# Changelog

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
