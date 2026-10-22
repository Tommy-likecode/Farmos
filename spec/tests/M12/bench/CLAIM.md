# M12 Arc CLAIM — more-headroom product (Linux measured; Arc pending)

**Verdict:** Linux CI **PASS**. Arc AC-M12-05 two independent warm-up medians **pending poteto** (eggtooth AUDIT FAIL r1: primary 1.237 PASS / second 1.131 FAIL).  
**Do not merge. Do not invite eggtooth. Enabler NOT used.**

| Field | Value |
|-------|--------|
| PR | #20 (draft) https://github.com/Tommy-likecode/Farmos/pull/20 |
| Branch | `cursor/m12-native-upscale-f17d` |
| Product SHA | `4f3ef560844da23f1e57843dda3adb9d4f0829f2` (more-headroom) |
| Last Arc SHA | `edeb84531de709226a2dd9aa23d8d7c169253558` |
| Host (Arc) | TommyLaptop, Windows x64, Intel Arc, `PATH_BACKEND_VULKAN_RT` |
| Date | 2026-10-22 (Linux this commit; Arc smoke still edeb845 ~20:10 CST) |
| Spec | `spec/M12-native-upscale.md` **FINAL** (OQ-M12-01..09 A; OQ-M12-10 A + OQ-M12-11 A) |
| SDK | Official XeSS 2.1.1 (`FARMOS_XESS_LIB` / `libxess.dll`) |
| Fixtures | In-repo `spec/tests/M12/bench/` (`PATH_GLOSSY_VNDF` ON) |

## Gates (FINAL) — last Arc at `edeb845` (not re-smoked on this tip)

| Gate | Rule | Last Arc (`edeb845`) | This tip |
|------|------|----------------------|----------|
| AC-M12-05 binding | XeSS wall < native | 3.477 < 4.302 (pass 1); 3.791 < 4.288 (pass 2) | **pending poteto two-pass** |
| AC-M12-05 SHOULD | ≥ **1.2×** (OQ-M12-10 A; historical ≥1.5 **superseded**) | pass 1 **1.237** PASS; pass 2 **1.131** FAIL | **pending** (target ~1.25–1.3×) |
| AC-M12-04 primary | ROI L both lanes ∈ [80,140]; \|ΔL\| ≤ 5.0 (OQ-M12-11 A) | L **105.810 / 105.805**; \|ΔL\| **0.005** | last Arc unchanged |
| AC-M12-04 mean_abs | report | **0.465** | report |
| AC-M12-04 png_tol / max_abs | **report-only** (OQ-M12-11 A) | max_abs **211** | report-only |
| AC-M12-07 PE | B-11 ≤ 700 KiB | **72704** / 716800 at `edeb845` | Linux ELF **71888**; Windows PE pending |

## AC-M12-04 numbers (last Arc `edeb845`; not re-run)

| Lane | wall_s |
|------|--------|
| Native 1920×1080 OFF | **31.880** |
| XeSS 960→1080 | **20.529** |

Silver ROI L native/xess **105.810 / 105.805**. mean_abs **0.465**. max_abs **211** (report-only).

## AC-M12-05 numbers (last Arc `edeb845`; not re-run)

| | primary median | second independent warm |
|--|----------------|-------------------------|
| Native | **4.302** | **4.288** |
| XeSS | **3.477** | **3.791** |
| ratio native/xess | **1.237** | **1.131** (AUDIT FAIL r1) |

Do not treat the primary 1.237 as repeatable. This product change does not invent new Arc walls.

## Linux verification (product `4f3ef56`; no Vulkan/SDK on this host)

| Suite | Result |
|-------|--------|
| `spec/tests/M12` | 8/8 green (004/006 soft-skip `requires_vulkan`) |
| `spec/tests/M4` | 24/24 PASS |
| `spec/tests/M11` | 4/4 PASS |
| `scripts/check_size_budgets.py` | PASS |

ELF B-11 **71888** / 716800. Hello 14464. DCE: unused upscale omits `farm_upscale.c`.

## Product change (more-headroom) — why it should raise repeatable ratio

- Skip `vkGetPipelineCacheData` + disk rewrite when the XeSS PSO blob already loaded (warm AC-05). Rewriting ~323 KiB after every cold `farmc` was the leading XeSS-only explanation for second-pass slowdown (3.477→3.791) and cache bloat across 8+9 processes.
- `isPathUpscaleAvailable` / RT device-create no longer `LoadLibrary` the ~75 MiB SDK. Probe is overlapped with `vkQueueWaitIdle` after path submit.
- Hardcoded XeSS 2.x extras (`VK_EXT_mutable_descriptor_type`, `shaderStorageImageWriteWithoutFormat`, float16/int8, 16-bit storage) so the borrowed path-tracer device still matches Intel XeSS-SR required set without a pre-GPU map. SDK query used if libxess is already mapped.
- Skip RT PSO dump when the compute-pipeline cache came from disk (shared hitch; native was already stable).

Host RGB↔RGBA pack / static-MV copy / fence-vs-WaitIdle left in place (process-per-frame first-frame; expected ≪100 ms; not the second-pass +314 ms).

## AC-M12-01..12

| ID | Status | Evidence |
|----|--------|----------|
| AC-M12-01 | **PASS** | Linux `001_upscale_off_cpu_twin` |
| AC-M12-02 | **PASS** | Linux `002_upscale_mode_roundtrip` |
| AC-M12-03 | **PASS** | Linux `003_trap_invalid_upscale` exit 122 |
| AC-M12-04 | last Arc **PASS** | ROI at `edeb845`; not re-run |
| AC-M12-05 | **pending two-pass** | last Arc 1.237 / 1.131; SHOULD floor not yet repeatable |
| AC-M12-06 | **PASS (skip policy)** | Linux `006_denoise_then_upscale` |
| AC-M12-07 | **PASS** (Linux) | ELF B-11 **71888** / 716800; DCE OK. Windows PE pending |
| AC-M12-08 | **PASS** | Official XeSS 2.1.1 only; Enabler/Swapper/OptiScaler NOT USED |
| AC-M12-09 | **PASS** | `# requires_xess` / `# requires_vulkan` skip on Linux |
| AC-M12-10 | **PASS** | M11 trap 121 + m12_008 STRICT |
| AC-M12-11 | **PASS** | M4 CPU SHA; jitter identity when OFF |
| AC-M12-12 | **PASS** | Linux `005_trap_invalid_resolution` exit 122 |

## Product rules

- Official Intel XeSS only. **Enabler / Swapper / OptiScaler NOT USED.**
- Default `PATH_UPSCALE_OFF` untouched.
- AC text / `run_arc_ac.py` SHOULD_RATIO **not** weakened.
- **No merge.** Arc re-smoke (two independent warm-up medians both ≥1.2×, prefer ~1.25–1.3×) is poteto's job after this push.
- Do not invent new Arc smoke numbers.

## Artifacts

- Last Arc: `/workspace/m12_arc/resmoke_edeb845.log` + `resmoke_edeb845_ac05_second.log`
- Last Arc PNGs: `/workspace/m12_arc/{native,xess}_1080.png`
- Last Arc PE: `/workspace/m12_arc/pe_budgets_edeb845.log`
