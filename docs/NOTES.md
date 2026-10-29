# Implementation notes (M1)

These notes document local implementation choices. They are **not** part of the language specification in `spec/M1-core.md`.

## C backend: clang/gcc only

`farmc` lowers Farmos to C11 and invokes a system C compiler. The emitter uses **GNU C statement-expressions** of the form:

```c
({ int64_t __i = ...; farm_bounds_check(__i, n); arr.data[__i]; })
```

for bounds-checked indexing, integer division traps, and similar.

**Consequences:**

- Supported C backends: **clang**, **gcc** (including LLVM-MinGW on Windows).
- **MSVC `cl` is not supported** as the C backend for generated programs.
- Do not pass `-ffast-math` / `/fp:fast` (driver already avoids these) so IEEE float division and NaN/Infinity printing remain correct.

## Compound assignment

Compound assignments (`+=`, `-=`, `*=`, `/=`, `%=`) evaluate a non-identifier lvalue **exactly once** (array index expressions and field bases), then read-modify-write through a temporary pointer.

## Float printing

`print`/`println`/`str` for `float` follow M1 section 6.1 / ECMAScript 2024 `Number::toString`: specials `NaN` / `Infinity` / `-Infinity`, signed zero `0` / `-0` (Farmos rule 3), and for finite non-zero values the shortest round-trip digits from the vendored **Ryu** converter (`runtime/ryu/`, `d2s_buffered_n`), laid out in ES form (scientific when the decimal point position n is < -5 or > 21, i.e. at 1e-7 and 1e21). Details, provenance and the differential test are in "Float printing algorithm" and "Float differential test" below. With `-ffunction-sections` and `--gc-sections`, programs that never print floats do not retain the formatter (hello stays small).

## E0508 location

Missing-return diagnostics are reported at the closing `}` of the function/method body (parser stores `Stmt::end_loc`).

## Per-module scoping / mangling

Each `.fm` file is a module. Non-exported top-level names are private. C symbols are prefixed per module (`fn_m0_name`, `struct Farm_m0_Name`) so private duplicates link cleanly. Imports bind only exported names (`E0305` if not exported; `E0505` if exported but not imported; `E0303` on duplicate import).


## MSVC cl rejection (AC-M1-04)

`farmc` detects `cl` / `cl.exe` / `clang-cl` via `FARM_CC` (or basename) **before** spawning any process, prints
`farmc: C compiler must be clang or gcc; MSVC cl is unsupported`, and exits 3. The MSVC `/nologo` flag path is removed.

## Diagnostic paths

Diagnostics use cwd-relative forward-slash paths when the source lies under the process cwd (harness sets
WorkingDirectory to the tests dir and passes relative `*.fm` / `dir/main.fm`).

## Cascade after E0505

After the first `E0505` (undefined name), further diagnostics in that compilation are suppressed (spec section 8.2 MAY).

## Float printing algorithm (AC-M1-05, spec section 6.1)

- `runtime/farm_rt.c`: `farm_format_float` (NaN / ±Infinity / ±0 rules) -> `farm_format_float_finite`.
- Digits come from **Ryu** `d2s_buffered_n` (Ulf Adams, *Ryu: fast float-to-string conversion*, PLDI 2018),
  a proven shortest round-trip converter. Among shortest candidates it picks the one closest to the exact
  value (ties to even), which is the digit selection `Number::toString` specifies.
- Provenance: vendored unmodified from https://github.com/ulfjack/ryu commit
  `4c0618b0e44f7ef027ebae05d2cc7812048f7c8f`, files `d2s.c ryu.h common.h digit_table.h d2s_intrinsics.h
  d2s_small_table.h` in `runtime/ryu/`, dual-licensed Apache-2.0 OR BSL-1.0 (license texts kept alongside).
  Compiled with `RYU_OPTIMIZE_SIZE` and `NDEBUG` via `#include "ryu/d2s.c"` from `farm_rt.c`.
- `farm_format_float_finite` then applies the ECMAScript `Number::toString` layout: integer form when
  k <= n <= 21, `ddd.ddd` when 0 < n <= 21, `0.000ddd` when -6 < n <= 0, otherwise `d[.ddd]e+N` / `e-N`
  (so 1e21 -> `1e+21`, 1e-7 -> `1e-7`). Signed zero follows the Farmos rule: `-0.0` prints `-0`.
- Replaced the previous `printf("%.*e")` + `strtod` loop, whose closest-candidate choice depended on the C
  library's printf rounding and was not a proven algorithm.

## Float differential test

- `scripts/float_diff.mjs` builds `tests/float/float_harness.c` (which `#include`s `farm_rt.c` and calls
  `farm_format_float` directly) and compares against the reference for every input.
- Reference: **Node `String(x)`** (V8's `Number::toString`), used because Node is already installed on
  TommyLaptop; it is the ECMAScript implementation itself, so no reformatting is needed. Only deviation: `-0`
  is expected as `-0` (spec section 6.1 rule 3).
- Inputs: 1,000,000 random 64-bit patterns (splitmix64, seed 20260925), 100,000 random short decimals, and
  edge cases (±0, min/max subnormal, min normal, max finite, 1e-324..1e308 with nextafter neighbours,
  ±3 ulps around 1e21 / 1e-7, integers near 2^53, NaN payloads, ±Infinity).
- ctest runs a quick variant (`local_090_float_diff_quick`, 20,000 random patterns + edges); run the full
  set with `node scripts/float_diff.mjs`.

## UTF-8 validation (spec section 2.1)

- `src/main.cpp` `validate_utf8` runs over the raw file bytes (after stripping one leading UTF-8 BOM, which
  section 2.1 says MUST be accepted and ignored) before lexing, per Unicode Table 3-7.
- The E0001 location is the first byte of the ill-formed sequence (the stray/invalid byte itself, or the
  lead byte of an overlong / surrogate / out-of-range / truncated sequence). Line/column use the same rules
  as every other diagnostic: CR LF / CR / LF breaks, column = 1 + Unicode scalars before it on the line.
- Exactly one E0001 is emitted; `common.hpp` suppresses any later diagnostics (no cascade).

## FARM_CC parsing (AC-M1-04)

- `src/main.cpp` `split_cc` splits the trimmed FARM_CC into {compiler token, args}:
  1. leading `"`: the quoted text is the token;
  2. the whole value names an existing file (`X` or `X.exe`): the whole value is the token;
  3. else the shortest space-delimited prefix that names an existing file (unquoted paths with spaces);
  4. else the text up to the first whitespace (bare `cl`, `cl /nologo`, `clang`).
- `is_msvc_cc`: token basename, extension stripped, case-insensitive, equals `cl` (or `clang-cl`) -> exit 3
  before any process is spawned. Checking the whole value / existing-file prefixes first means an unquoted
  `C:\x\cl tools\bin\clang.exe` is not mistaken for `cl` (local_038_*).
- The executed command always quotes the resolved token (`cc_command_prefix`), so unquoted FARM_CC paths with
  spaces run the intended executable.

## Exit codes (spec section 7.2)

- 2: unknown option / command, missing file argument. 3: missing or unreadable input file
  (`farmc: cannot read input file '...'`), `-o` naming an existing directory
  (`farmc: output path '...' is a directory`; the directory is untouched and no `<dir>.exe` is written),
  no C compiler, MSVC selected, C compile/link failure.
- 4: any escaped C++ exception (`farmc: internal compiler error: ...`). Test hook: `FARMC_TEST_ICE=1` makes
  `farmc build` throw, but ONLY in the `farmc_testhooks` target (compiled with `FARMC_ENABLE_TEST_HOOKS`);
  the shipped `farmc` contains no hook and ignores the variable (`local_085_release_ignores_ice`).
  `local_084_exit4_ice` runs against `farmc_testhooks`.

## String literal codegen

- `src/emit_c.cpp` `c_escape_bytes` / `c_string_value`: printable ASCII passes through except `\\`, `"` and
  `?` (trigraph guard); every other byte (controls, NUL, DEL, every non-ASCII UTF-8 byte) is a fixed
  3-digit octal escape `\ooo`. An octal escape stops after 3 digits, so a following digit or hex letter is
  never absorbed (the old `\x%02x` form swallowed it). The literal is wrapped as
  `((FarmString){ "...", (int64_t)<byte count> })`: the length is explicit, so embedded NUL survives;
  the runtime writes `len` bytes (never `strlen`).
- Other C literals emitted: `#include "farm_rt.h"` and `farm_str_from_cstr("")` (constant). Identifiers are
  ASCII-only (spec section 2.4) and diagnostics/trap messages/paths are never embedded in generated C.

## Non-ASCII outside strings and comments

- `validate_utf8` is the only E0001 source. A well-formed non-ASCII scalar in code (e.g. `中`, `é`, NBSP
  U+00A0, a mid-file U+FEFF) is not a token start: the lexer consumes the whole scalar and reports ONE
  `E0202 unexpected token `{tok}`` (spec section 8 template) at its scalar column, then suppresses further
  diagnostics. Comments and string contents may contain any well-formed UTF-8; only a leading BOM is
  stripped, a BOM inside a string is kept as bytes EF BB BF.

## Temporary files

- Each farmc process creates a private scratch directory `%TEMP%\farmc-<pid>-<counter>-<16 hex random>`
  with `create_directory` (fails if the name exists, so it is exclusive across processes). The generated
  `<stem>_farmc_gen.c`, the `cc_run_<n>.cmd` wrappers and the link output live there; the link output is
  then moved (rename, or copy across volumes) to the requested `-o` path. The directory is removed on
  success, failure and exception (RAII). `farmc run` uses its own private scratch directory for the
  temporary executable. No fixed temp names remain (`local_130_parallel_same_stem`).
- `--keep-c` (without `--emit-c`) writes the C next to the output (`<out>.c`) instead of keeping a temp file.

## Runner (scripts/run_one_m1.ps1)

- All expected-vs-actual comparisons are ordinal (case-sensitive).
- Blocks: only a `# end` line terminates a `# stdout:` / `# stderr:` block (README: "Everything between
  `# stdout:` and `# end` is compared exactly"). Inside a block every other line, including `#` and `##`
  lines, is literal content. Outside blocks, `##` lines are comments and ignored. This reading of the two
  README rules is our interpretation; no spec fixture has a `##` line inside a block.

## Float diff mutation check

- `node scripts/float_mutation.mjs` copies `runtime/` to `build/float_mut/<id>/`, applies an exact textual
  replacement in the copy's `farm_rt.c` (asserted to match exactly once; the source tree is never
  modified), and runs the FULL `float_diff.mjs` (1,000,000 random patterns, seed 20260925, 1,104,039 inputs)
  against it via `--runtime`:
  - M1 `if (k <= n && n <= 21) {` -> `n <= 20` (integer-form threshold): 3254 mismatches (full diff);
    the quick variant (`--count 20000`, 26,039 inputs) gives 107 for the same mutant.
  - M2 `} else if (0 < n && n <= 21) {` -> `n <= 20`: 0, an equivalent mutant (shortest binary64 digits
    have k <= 17 < 21, so n == 21 always takes the integer branch).
  - M3 both replacements: 3254.


## Unicode TEMP / ScratchDir (Windows)

- Root cause at c8398bf: `std::filesystem::temp_directory_path()` (and `path::string()`) on MSVC convert
  the wide TEMP path through the ANSI/ACP code page. TEMP containing U+00DF (ß), U+1F600 (😀), or
  U+200D (ZWJ) throws `No mapping for the Unicode character exists in the target multi-byte code page`,
  which became exit-4 ICE.
- Fix: resolve TEMP/TMP with `GetEnvironmentVariableW` / `GetTempPathW`; build `fs::path` from `wstring`;
  emit command lines with UTF-8 (`path_to_utf8` via `WideCharToMultiByte(CP_UTF8)`); invoke the C
  compiler with `CreateProcessW` (no `cmd.exe` / `.cmd`, which are ACP). Scratch leaf names stay ASCII
  (`farmc-<pid>-<n>-<rand>`). M6 §7.2: unusable TEMP → exit **1** and
  `error: temporary directory unavailable` (`TempDirError`, never ICE). Other driver I/O stays exit 3.
- Covered by `local_140_unicode_temp`.

## M11 glossy continuation (VNDF-GGX)

Implementation-owned formulas, shared by `runtime/farm_ray.c`, `runtime/farm_vk_path.comp`, and
`runtime/farm_vk_rt_path.comp`. Spec: `spec/M11-glossy-continuation.md` §5.

- **When:** `getPathGlossyContinuation() == PATH_GLOSSY_VNDF` and the hit has `metal > 0` after clamp
  and `rough > 0`. Dielectrics, glass, and `rough == 0` (delta mirror) stay on the M4 cosine / delta
  branches even if the flag is ON.
- **Lobe:** Heitz 2018 VNDF sample of isotropic GGX with `alpha = rough * rough` (same alpha as
  existing NEE `ggx_d`). View is transformed into the M4 ONB (`|Nx|>|Ny|`). RNG slots `depth*16+0/1`
  match the cosine bounce; one resample uses `+4/+5` if the reflected direction is below the
  geometric hemisphere. Still invalid → documented fallback `reflect_dir` with Schlick weight
  (treated as delta, `mis_pdf = 0`).
- **Estimator:** VNDF identity `f * N·L / pdf = F * G1(L)` with Schlick `F0 = 0.04*(1−metal)+albedo*metal`
  and Smith GGX `G1`. No `(1−metal)` on this path. No firefly clamp (OQ-M11-05 A).
- **PDF (MIS):** `pdf(L) = D(H) * G1(V) / (4 * N·V)`.
- **NEE:** RectArea / Point / Directional stay. For a glossy-metal hit, skip_spec is forced off
  (CPU speed-lane must not drop GGX NEE). Balance-heuristic MIS uses the VNDF pdf instead of
  Lambert, including when `PATH_SAMPLING_M4` (so NEE + BSDF-to-rect are not double-counted).
  Dielectrics under glossy ON + M4 sampling are untouched (no extra MIS).
- **GPU:** glossy mode is `GpuParams.extra.z`. SPIR-V regenerated via `scripts/compile_vk_shader.sh`.
- **Env:** `FARMOS_PATH_GLOSSY=1` only if the setter was never called; method wins.

## M12 native Vulkan upscale (XeSS / FSR)

Implementation-owned glue in `runtime/farm_upscale.c`, called after denoise from
`farm_Renderer_renderPath`. Spec: `spec/M12-native-upscale.md`.

- **API:** `setPathUpscale` / `PATH_UPSCALE_*`; constructor size is the path-trace
  (render) resolution; `setPathUpscaleResolution` sets render/display for getters
  and for the upscale pass. `savePNG` uses `fb_width`/`fb_height` after a successful
  upscale (display buffer), otherwise `width`/`height`.
- **Jitter:** Halton base 2/3 in `[-0.5, 0.5]` packed into GPU `cam_x.w` / `cam_y.w`
  when upscale is ON **and** the backend is Vulkan. Identity on CPU and when OFF
  (M4 SHA). SPIR-V regenerated via `scripts/compile_vk_shader.sh`.
- **Aux:** path-tracer first-hit `t` from sample 0 bounce 0 (GPU accum SSBO tail
  after RGB; no extra `closest_hit`). CPU first bounce into `depthbuffer`.
  `fill_aux` is O(pixels) linearize + cheap camera MV; static camera writes zero
  MV (no CPU ray×tris). Object / skinned MV deferred. Depth is linearized
  `(t−near)/(far−near)` uploaded as `R32F`. Color is post-quantize LDR `RGBA8`
  (`XESS_INIT_FLAG_LDR_INPUT_COLOR` + autoexposure).
- **XeSS:** `dlopen` / `LoadLibrary` of `libxess` (`FARMOS_XESS_LIB` override).
  Headless `VkImage`s, no swapchain. Images/staging/cmd persist across frames.
  Disk `VkPipelineCache` at `%TEMP%/farmos_xess_pso_<dw>x<dh>.bin` passed to
  `xessVKInit`. Warm processes that already loaded that blob skip
  `vkGetPipelineCacheData` + rewrite. `isPathUpscaleAvailable` is file-present
  only. When upscale is ON, RT/compute backends query official
  `xessVKGetRequired*` extras onto the path-tracer device (hardcoded extras
  in more-headroom likely broke borrow). After path `vkQueueSubmit`,
  `farm_upscale_prepare_xess` runs CreateContext + init + image alloc during
  `vkQueueWaitIdle`. **Prefer a dedicated XeSS VkDevice** so init / first-submit
  PSO compile cannot `DeviceWaitIdle` the in-flight path-tracer queue (clear-m12-backlog
  Pass1 residual-init hypothesis). Fall back to borrow if own create fails.
  Native OFF does not load libxess at device create.
  Quality preset from scale (2× → Balanced) with nearby retries. Vendor DLLs
  are not linked into the Farmos PE.
- **FSR2:** `isPathUpscaleAvailable(PATH_UPSCALE_FSR)` is true when the official
  ffx shared lib is loadable; execute is not stubbed with Enabler — missing dispatch
  soft-fails (STRICT → trap 122).
- **DCE:** `program_uses_upscale` gates `-DFARM_ENABLE_UPSCALE` + `farm_upscale.c`.
- **Harness (Windows):** `Test-HostVulkanDevice` writes a temp ctypes probe and
  runs it via `System.Diagnostics.Process` (not `Start-Process -c`, which
  mangled quotes and false-soft-skipped Arc). Soft-skip only after a completed
  probe says no device; loader present + probe tooling fail → run / hard-fail.
- **Arc / backlog:** helpers in `spec/tests/M12/bench/` (`PATH_GLOSSY_VNDF`).
  Pinned AC-04 ROI `x=940:980,y=520:560` @1080p (40×40). Binding \|ΔL\| =
  \|L_xess − L_native\| of **means**. Glossy CLAIM **PASS** under FINAL
  OQ-M12-12 A + OQ-M12-13 A at docs `f6c2285` / product `d23acfb`
  (TommyLaptop, dirty chrome/msedge): AC-05 Pass1 **1.264** (4.441 / 3.512)
  / Pass2 **1.348** (4.712 / 3.497) (SHOULD ≥ **1.05×**; prefer ≥1.08–1.10
  **YES**; XeSS < native both); ROI L **105.810 / 105.801** ∈ [80,200],
  \|ΔL\| of means **0.009**; mean_abs_dL **0.440** report-only; max_abs
  **212** report-only; PE B-11 **72704**. `Test-HostVulkanDevice=True`;
  M7 `008_cornell_vulkan_tol` ran **PASS** (no false soft-skip). Historical
  ≥1.2× / [80,140] superseded. Linux M12 8/8, M4 24/24, M11 4/4, M7 9/9,
  ELF B-11 **68088**. Enabler not used. **No merge** from this CloudAgent run.

## M13 performance / more speed

Spec: `spec/M13-performance.md` FINAL (OQ-M13-01..08 A). Arc SHOULD ≥ **1.20×**
both Pass1 and Pass2 after warm-up. M12 ≥ **1.05×** floor unchanged. Baseline
product `d23acfb3` Pass1 **1.264** / Pass2 **1.348**. Master tip `affc63ea`.

- **API:** no new `.fm` public API. Internal `FARMOS_PATH_FRAME_MS=1` prints
  `frame_ms=<ms>` per `renderPath` for in-process benches.
- **Harness:** `spec/tests/M13/bench/run_arc_ac.py` reuses M12 `ac04_*` /
  `ac05_*` sources. Primary ratio is in-process frame samples after dropping
  3 warmup frames. Process-per-frame AC-05 still reports the M12 floor.
  Linux host-amort check: `run_linux_host_amort.py` (CPU, no SDK).
- **Device:** dedicated XeSS VkDevice kept. Enabler / Swapper / OptiScaler OUT.
- Arc CLAIM **PASS** 2026-10-28 (TommyLaptop, dirty chrome/msedge informational)
  at measured tip `e4978668` / product `945c13ae`. In-process SHOULD Pass1
  **1.455×** (2570.275 / 1766.219 ms) / Pass2 **1.607×** (2575.415 / 1602.777 ms).
  Process-per-frame M12 floor Pass1 **1.288×** (4.729 / 3.671 s) / Pass2
  **1.288×** (4.608 / 3.578 s). XeSS < native both protocols. ROI L
  **105.810 / 105.801**, \|ΔL\| of means **0.009**, mean_abs_dL **0.440**
  report-only. PE ac05_xess **324096**. Do not compare in-process ms to
  `d23acfb3` process-per-frame **1.264 / 1.348**. **No merge.**

## M14 performance / more speed

Spec: `spec/M14-performance.md` FINAL (OQ-M14-01..08 A). Arc SHOULD ≥ **1.50×**
both Pass1 and Pass2 after warm-up under in-process `FARMOS_PATH_FRAME_MS`.
M13 ≥ **1.20×** inproc and M12 ≥ **1.05×** process-per-frame remain floors.
Baseline product `945c13ae` Pass1 **1.455** / Pass2 **1.607**. Master tip
`47949c82`. Do not compare in-process ms to `d23acfb3`.

- **API:** no new `.fm` public API. Keep `FARMOS_PATH_FRAME_MS=1`.
- **H1 (kept):** persisted static BLAS/TLAS built with
  `VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR` (was FAST_BUILD).
  First-frame AS build stays warmup.
- **H2 (kept):** XeSS display `vkCmdCopyImageToBuffer` + RGB unpack deferred to
  `savePNG` via `Renderer.path_upscale_flush` (NULL when upscale is DCE'd).
  Timed inproc `frame_ms` no longer includes the 1080p host download. Preview
  still unpacks immediately. Process-per-frame AC-05 still includes `savePNG`.
- **H3 (kept):** dummy XeSS execute during `farm_upscale_prepare_xess` on the
  dedicated XeSS device while the path queue is in flight (Pass1 PSO overlap).
- **Not applied:** fill_aux one-pass (M13 ~0.06 ms); XeSS quality-mode; GPU
  export/import skip of color/depth host round-trip.
- **Harness:** `spec/tests/M14/bench/run_arc_ac.py` reuses M12 `ac04_*` /
  `ac05_*`. Linux: `run_linux_host_amort.py`.
- Arc CLAIM **PASS** 2026-10-29 (TommyLaptop, dirty chrome/msedge informational)
  at measured tip `d409152e` / product `4d7523ec`. In-process SHOULD Pass1
  **3.184×** (2552.985 / 801.892 ms) / Pass2 **3.913×** (2743.773 / 701.114 ms).
  Process-per-frame M12 floor Pass1 **1.235×** (4.794 / 3.883 s) / Pass2
  **1.139×** (4.251 / 3.732 s). XeSS < native both protocols. ROI L
  **105.810 / 105.801**, \|ΔL\| of means **0.009**, mean_abs_dL **0.440**
  report-only. PE ac05_xess **325120**. Do not compare in-process ms to
  `d23acfb3` process-per-frame **1.264 / 1.348**. Product **unchanged**.
  Enabler / Swapper / OptiScaler OUT. **No merge.**

