# Farmos M7 optional Vulkan path-tracing test fixtures

Sources use the M1 TypeScript-style surface, M2 math, M3/M4 scene + path API,
and the M7 backend opt-in (`setPathBackend`, `PATH_BACKEND_*`,
`isVulkanPathAvailable`). See `spec/M7-vulkan-path.md`. Do not edit M1–M6
milestone bodies to absorb these rules; amendments live in `M7-vulkan-path.md`.

This file documents **M7-only** harness extensions. It does not replace
`spec/tests/README.md` or the M4 README. Kinds `run`, `runtime_trap` keep their
M1 meaning. `run_png` keeps the M3/M4 meaning plus the headers below.

## Layout

```
spec/tests/M7/
  README.md
  NNN_name.fm
  NNN_name.expected
  _ref/
    README.md
    goldens_sha256.txt   (CPU twins copied from M4 where applicable)
```

No `_ref/path_ref.py` in M7: CPU goldens reuse M4’s oracle. GPU tolerance uses
`# png_tol:` against those SHA references.

## Working directory

The harness MUST run each M7 program with CWD = `spec/tests/M7`.
`savePNG("out.png")` writes `spec/tests/M7/out.png`.

## M7 headers

| Header | Meaning |
|--------|---------|
| `# requires_vulkan: true` | Soft-skip (treat as pass/skip, exit 0) when the host has no usable Vulkan device. Linux CI without a device MUST soft-skip. |
| `# png_tol: <int>` | Max abs per-channel byte delta vs the PNG identified by `# sha256:`. `0` = bit-identical. GPU heroes use `2`. |
| `# env: KEY=VALUE` | Child environment. At least `FARMOS_PATH_GPU_STRICT` and `FARMOS_VULKAN_FORCE_FAIL`. |
| `# stdout_alt:` | Additional accepted stdout block (fixture `002` availability line). |

Header order suggestion: `# kind:`, `# exit:`, `# requires_vulkan:` (if any),
`# env:` lines, `# png:` / `# sha256:` / `# png_tol:`, then `# stdout:` /
`# stderr:`.

## Soft-skip

When `# requires_vulkan: true` and no device:

1. Do **not** fail the fixture.
2. Record skip in the harness log.
3. Exit 0 from the test runner for that case.

CPU fixtures never set `# requires_vulkan:`.

## Counts

12 index entries: **9** `.fm` fixtures (3 `run_png` CPU, 1 `run_png` GPU-tol,
1 `run_png` raster, 2 `run`, 2 `runtime_trap`) + **3** eggtooth script ACs
(004 size, 005 DCE symbols, 010 version).

## Eggtooth script ACs (no `.fm`)

| ID | Check |
|----|-------|
| 004 | Stripped PE of `spec/tests/M1/001_hello.fm` ≤ 20 KiB |
| 005 | That PE MUST NOT import `vulkan-1.dll` / `vkGetInstanceProcAddr` (dumpbin / nm) |
| 010 | `farmc version` stdout contains `1.01`; CHANGELOG has a v1.01 entry |

Extend `scripts\check_size_budgets.ps1` (or sibling) like M6.

## Environment variables

| Variable | Value `1` means |
|----------|-----------------|
| `FARMOS_PATH_GPU_STRICT` | Vulkan failure at `renderPath` → trap 117 (no CPU fallback) |
| `FARMOS_VULKAN_FORCE_FAIL` | Treat GPU path as failed (CI hook) |
| `FARMOS_THREADS` | CPU tile pool only (M4); ignored for GPU dispatch |

## PLACEHOLDER goldens

If a `run_png` has no M4 twin yet:

```
## PLACEHOLDER — poteto regenerates against CPU M4 golden / GPU tolerance
# sha256: 0000000000000000000000000000000000000000000000000000000000000000
```

CPU twins in this pack use **real** M4 SHA-256 values from
`spec/tests/M4/_ref/goldens_sha256.txt`.

## Index

| Fixture | Kind | Notes |
|---------|------|-------|
| 001_cpu_background | run_png | M4-001 twin |
| 002_vulkan_opt_in_fallback | run | opt-in; fallback OK |
| 003_strict_force_fail | runtime_trap | STRICT+FORCE_FAIL → 117 |
| 004_hello_size | eggtooth | ≤ 20 KiB |
| 005_hello_no_vulkan_sym | eggtooth | no Vulkan imports |
| 006_cornell_cpu | run_png | M4-008 twin |
| 007_glass_mirror_cpu | run_png | M4-009 twin |
| 008_cornell_vulkan_tol | run_png | requires_vulkan, png_tol 2 |
| 009_trap_invalid_backend | runtime_trap | 118 |
| 010_version_1_01 | eggtooth | CLI 1.01 |
| 011_raster_clear | run_png | M3 clear via `render` |
| 012_force_fail_fallback | run | FORCE_FAIL, no STRICT |
