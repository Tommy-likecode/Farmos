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
  (`farmc-<pid>-<n>-<rand>`). `DriverError` -> exit 3 (never ICE) if TEMP is truly unusable.
- Covered by `local_140_unicode_temp`.
