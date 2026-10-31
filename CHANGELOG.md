# Changelog

## Unreleased

M15 — liquid / MP4 (`spec/M15-liquid-mp4.md` FINAL):

- New builtin `farmos:liquid`: `LiquidWorld`, `HeightField`, `LIQUID_BACKEND_CPU`. Collocated shallow-water CPU (XZ +Y, g=9.81, reflective walls). Oracle `spec/tests/M15/_ref/liquid_ref.py`.
- Additive `Renderer.saveMP4(path)` via external `ffmpeg` on PATH (raw rgb24 → H.264, mpeg4 fallback). Capture on `render` when the video unit is linked. STRICT `FARMOS_VIDEO_STRICT=1` → trap **123** `MP4 encode failed`.
- Size: unused liquid / video DCE; `S_liq − S_scene ≤ 96 KiB`; video glue ≤ 32 KiB. M12–M14 floors and M5 body untouched.
- Fixtures: `spec/tests/M15/`. `# requires_ffmpeg` soft-skip. Demo: `examples/08_liquid_5s.fm`.
- CLAIM **PASS** 2026-10-31 TommyLaptop Windows @ `93b50c45` / product `4d7523ec` unchanged: hello PE **13824**; S_liq−S_scene **4096** (33792 − 29696); saveMP4 glue **512**; `liquid_5s.mp4` 5.000 s / 30 fps / 150 / 640×360 h264. Linux 15/15. HOLD until eggtooth AUDIT.

M14 backlog clear (post squash-merge `218f6ef1` / PR #23; product `4d7523ec` **unchanged**):

- CLAIM/NOTES: H2 inproc `FRAME_MS` is GPU-pipeline (XeSS 1080p host readback deferred to `savePNG`; GPU XeSS still timed; PNG/ROI OK). Do not claim equal-work vs M13 `945c13ae`. No flush-every-frame secondary inproc meter (proc AC-05 already includes `savePNG`).
- CMake: register `m14_*` like `m13_*`.
- NOTE only: Pass2 process-per-frame **1.139** (~8.5% over 1.05) — monitor; dirty chrome+msedge informational.
- Soft doc nits: name `FARMOS_PATH_FRAME_MS` on AC-M13-04; OQ-M13-01 inproc vs `d23acfb3` process-per-frame; M12 fixture README Quality row uses pinned CLAIM L **105.810 / 105.801** (not L~187 beside the pin). FINAL OQ locks / SHOULD ≥1.50× / floors **unchanged**. No Arc remeasure.

M14 — performance / more speed (`spec/M14-performance.md` FINAL):

- SHOULD display-equivalent ≥ **1.50×** on TommyLaptop Arc after warm-up for **both** Pass1 and Pass2 (OQ-M14-01 A / OQ-M14-02 A), in-process `FARMOS_PATH_FRAME_MS`. M13 ≥ **1.20×** inproc and M12 ≥ **1.05×** process-per-frame remain floors. Baseline product `945c13ae` Pass1 **1.455** / Pass2 **1.607** (inproc). Do **not** compare in-process ms to `d23acfb3` process-per-frame.
- Engine / host-RT / PSO (one hypothesis each): persist static AS `PREFER_FAST_TRACE`; defer XeSS 1080p host download until `savePNG`; dummy XeSS execute during `prepare_xess` on the dedicated device while path GPU is in flight. No new public `.fm` API. `FARMOS_PATH_FRAME_MS` kept.
- Arc CLAIM **PASS** 2026-10-29 (dirty chrome/msedge informational) at measured tip `d409152e` / product `4d7523ec`: inproc Pass1 **3.184×** (2552.985 / 801.892 ms) / Pass2 **3.913×** (2743.773 / 701.114 ms); proc floor Pass1 **1.235×** (4.794 / 3.883 s) / Pass2 **1.139×** (4.251 / 3.732 s); ROI L **105.810 / 105.801**, \|ΔL\| of means **0.009**; PE ac05_xess **325120**. Official SDK only. Enabler still OUT. Product **unchanged** by this docs sync.
- OUT unchanged: frame-gen, OS window, DLSS/Enabler/Swapper/OptiScaler, lower render-res, lower spp as the speed strategy.
- Fixtures: `spec/tests/M14/`. Linux `# requires_xess` soft-skip; OFF twins + trap 122 + size glue ≤700 KiB remain.

M13 — performance / more speed (`spec/M13-performance.md` FINAL):

- SHOULD display-equivalent ≥ **1.20×** on TommyLaptop Arc after warm-up for **both** Pass1 and Pass2 (OQ-M13-01 A / OQ-M13-02 A). M12 ≥ **1.05×** remains the floor. Baseline product `d23acfb3` Pass1 **1.264** / Pass2 **1.348** is **process-per-frame** (do not compare to in-process ms).
- Host amortization: in-process multi-frame Arc timing via internal `FARMOS_PATH_FRAME_MS=1` (`frame_ms=` lines). No new public `.fm` API. Dedicated XeSS `VkDevice` kept. Process-per-frame AC-05 still reports the M12 floor.
- Arc CLAIM **PASS** 2026-10-28 (dirty chrome/msedge informational) at measured tip `e4978668` / product `945c13ae`: inproc Pass1 **1.455×** (2570.275 / 1766.219 ms) / Pass2 **1.607×** (2575.415 / 1602.777 ms); proc floor Pass1 **1.288×** (4.729 / 3.671 s) / Pass2 **1.288×** (4.608 / 3.578 s); ROI L **105.810 / 105.801**, \|ΔL\| of means **0.009**; PE ac05_xess **324096**. Official SDK only. Enabler still OUT.
- OUT unchanged: frame-gen, OS window, DLSS/Enabler/Swapper/OptiScaler, lower render-res, lower spp as the speed strategy.
- Fixtures: `spec/tests/M13/`. Linux `# requires_xess` soft-skip; OFF twins + trap 122 + size glue ≤700 KiB remain.

M12 backlog clear (post `f5b87f8f`; OQ-M12-12/13 A gates unchanged):

- Spec/PLAN/README 2026-10-25 clarification: pinned AC-04 ROI `x=940:980,y=520:560` @1080p; binding \|ΔL\| is \|L_xess − L_native\| of means (baseline ≈0.650). 3.932 is mean_abs_dL report-only.
- XeSS init prefers a dedicated VkDevice so CreateContext/init cannot DeviceWaitIdle the in-flight path queue (Pass1 headroom). Official SDK only; Enabler still OUT. Glossy Arc CLAIM PASS at docs `f6c2285` / product `d23acfb`: Pass1 **1.264** / Pass2 **1.348**; ROI L **105.810 / 105.801**; \|ΔL\| of means **0.009**.
- Windows `Test-HostVulkanDevice`: temp-file probe + Process ExitCode (no `Start-Process -c` false soft-skip on Arc).

M12 — native Vulkan upscale (`spec/M12-native-upscale.md` FINAL):

- Additive `PATH_UPSCALE_OFF` / `PATH_UPSCALE_XESS` / `PATH_UPSCALE_FSR`, `setPathUpscale` / `getPathUpscale` / `isPathUpscaleAvailable`, `setPathUpscaleResolution`. Default **OFF**. Invalid mode or resolution traps **122** at the setter (STRICT fail: `path upscale failed`).
- Headless XeSS Vulkan glue (`runtime/farm_upscale.c`, dynamically loaded). No swapchain/window. FSR2 is selectable; execute soft-fails until the official ffx dispatch lib is present. **OUT:** DLSS/NGX, Enabler, Swapper, OptiScaler, frame-gen.
- When denoise and upscale are both ON: denoise then upscale. GPU backends jitter Halton(2,3) while ON; identity jitter when OFF (M4 CPU SHA). Camera MV + linearized depth; object MV deferred (zeros).
- CPU leaves render-res unless `FARMOS_PATH_UPSCALE_STRICT=1`. Vendor SDK DLLs external (`FARMOS_XESS_LIB` / `scripts/fetch_upscale_sdk.sh`). DCE: `farm_upscale.c` linked only when the upscale API is used. Glue PE ≤ **700 KiB**.
- Fixtures: `spec/tests/M12/`. Linux soft-skips `# requires_xess:` / `# requires_fsr:`. Arc 960×540→1080p quality + timing pending TommyLaptop.

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
