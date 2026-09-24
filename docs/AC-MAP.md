# AC-M1 map (from M1-core.md §10)

Each **Criterion** cell quotes M1-core.md §10 Acceptance criteria verbatim (Rev 2).
Do **not** implement M2 rule OQ-M2-11 (int literal to float) under M1.

| ID | Criterion (quoted from §10) | Proof in this tree |
|----|-----------------------------|--------------------|
| AC-M1-01 | **Binary size (Windows x64):** From the repo root on Windows: `farmc build spec\tests\M1\001_hello.fm -o hello.exe` then strip with `llvm-strip --strip-all hello.exe` (or `strip --strip-all hello.exe` if GNU binutils `strip` is on `PATH`). `(Get-Item .\hello.exe).Length` MUST be ≤ **20480**. | `farmc build` of `001_hello` / `examples/hello.fm` + `llvm-strip`; measured stripped size ≤ 20480 (claim reports bytes). |
| AC-M1-02 | ≥ **30** M1 language tests pass via local `scripts\build_and_test.ps1` (ctest). This pack ships ≥ 49 fixtures. | Default `ctest -LE conformance_bundle`: 49 `m1_*` + local regressions; `scripts/build_and_test.ps1`. |
| AC-M1-03 | Compile failures use `path:line:col: error[E0xxx]:` with correct 1-based line/col (Unicode scalar columns). | `compile_error` fixtures (e.g. `023`–`027`, `039`–`040`, `043`–`044`, `048`–`049`); runner asserts path (when present), line, col, code. |
| AC-M1-04 | `farmc build` produces a native Windows x64 PE executable using clang or gcc only (not MSVC `cl`). Linux is out of scope. | Successful PE builds via clang/gcc; `local_030_farm_cc_cl` / `local_031_farm_cc_cl_fullpath` reject `FARM_CC=cl` (exit 3, no invoke). |
| AC-M1-05 | `kind: run` stdout exact match; float formatting MUST follow §6.1 (`Number::toString` + signed-zero rule), not merely fixture snapshots. | All `kind: run` fixtures via harness stdout assert; `m1_045_float_tostring`, `local_002_float_tostring`, `m1_038_float_ieee`. |
| AC-M1-06 | Hello binary has no graphics/math stdlib; size proxy AC-M1-01. | Hello links only `farm_rt`; stripped size under AC-M1-01 budget. |
| AC-M1-07 | `farmc` exit codes §7.2; user `main` return = process exit. | `m1_028_main_exit_code`; compile_error → farmc exit 1; MSVC config → exit 3. |
| AC-M1-08 | Integer div-by-zero and OOB traps: exact stderr + exit 101/102; float `/0` prints `Infinity`/`-Infinity`/`NaN` (no trap). | `m1_029_div_by_zero_int`, `m1_018_array_bounds_trap` (stderr exact); `m1_038_float_ieee` / float fixtures (no trap). Negative: `local_040_trap_wrong_stderr`. |
| AC-M1-09 | Multi-file import/export and per-module privacy tests pass. | `m1_022_modules`, `m1_047_module_private_ok`, `m1_048_import_non_export`, `m1_049_export_not_imported`; local `020`/`021`/`022`. |
| AC-M1-10 | Soft: hello build ≤ 5 s on TommyLaptop (non-binding). | Timed `farmc build` of hello on TommyLaptop (soft / non-binding). |
| AC-M1-11 | Class instances may be returned/stored across scopes (program arena); no E0405. | `m1_041_class_escape`, `m1_021_class_methods`. |

## Suite layout

- Default ctest: `-LE conformance_bundle` → all `m1_*` (49) + positive `local_*` (no double-run of fixtures).
- Opt-in umbrella: `ctest -L conformance_bundle` or `-R '^m1_conformance$'`.
- Negative runner proofs: `local_040_trap_wrong_stderr`, `local_041_run_unexpected_stderr` (wrapper expects runner failure).
