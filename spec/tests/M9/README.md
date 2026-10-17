# Farmos M9 hardware ray-tracing test fixtures

Sources use the M1 TypeScript-style surface, M2 math, M3/M4 scene + path API,
M7/M8 backend and speed controls, and the M9 HW RT opt-in
(`PATH_BACKEND_VULKAN_RT`, `isVulkanRayTracingAvailable`). See
`spec/M9-hardware-rt.md` (**FINAL**). Do not edit M1–M8 milestone bodies;
amendments live in `M9-hardware-rt.md`.

## Layout

```
spec/tests/M9/
  README.md
  NNN_name.fm
  NNN_name.expected
  bench/
  _ref/
```

Working directory for M9 fixtures: `spec/tests/M9`.

## M9 headers

| Header | Meaning |
|--------|---------|
| `# requires_vulkan: true` | Soft-skip without a Vulkan **compute** device (M7) |
| `# requires_vulkan_rt: true` | Soft-skip when HW RT / `isVulkanRayTracingAvailable()` would be false |
| `# png_tol: <int>` | Per-channel byte tolerance vs `# sha256:` (HW RT uses `2`) |
| `# env: KEY=VALUE` | Child env (`FARMOS_PATH_GPU_STRICT`, `FARMOS_VULKAN_RT_FORCE_FAIL`, …) |
| `# path_mode: compat` | Documentation aid (M8) |

Soft-skip for `# requires_vulkan_rt: true` when no HW RT device: do not fail;
record skip in the harness log; runner exit 0 for that case.

## Index

| Fixture | Kind | Notes |
|---------|------|-------|
| 001_cpu_compat_background | run_png | M4-001 twin SHA; CPU backend |
| 002_rt_availability_fallback | run | backend 2; soft fallback OK |
| 003_strict_rt_force_fail | runtime_trap | STRICT + RT_FORCE_FAIL → 117 |
| 004_trap_invalid_backend | runtime_trap | `setPathBackend(3)` → 118 |
| 005_compute_backend_still_works | run | backend 1 round-trip |
| 006_rt_png_tol | run_png | optional; `# requires_vulkan_rt`, `# png_tol: 2` |

## Environment variables

| Variable | Value `1` means |
|----------|-----------------|
| `FARMOS_PATH_GPU_STRICT` | Selected GPU/RT backend must succeed or trap 117 |
| `FARMOS_VULKAN_FORCE_FAIL` | Compute fail (M7); also forces HW RT fail when backend is 2 |
| `FARMOS_VULKAN_RT_FORCE_FAIL` | HW RT path failed; compute may still run under soft mode |

## Bench

Silver still wall-clock: `spec/tests/M9/bench/`. AC-M9-12..14 are measured on
TommyLaptop (pending); Linux CI does not invent Windows numbers.
