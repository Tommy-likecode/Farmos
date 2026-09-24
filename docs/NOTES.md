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

`print`/`println`/`str` for `float` follow M1 section 6.1 / ECMAScript 2024 `Number::toString`: specials `NaN` / `Infinity` / `-Infinity` / `-0`, and shortest round-trip decimals with scientific notation when the decimal exponent \(k < -6\) or \(k \geq 21\`.

Implemented in `runtime/farm_rt.c` via a compact try-precision-1..17 + `strtod` round-trip loop, then ES-style formatting. With `-ffunction-sections` and `--gc-sections`, hello-world programs that never print floats should not retain the float formatter.

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

- FARM_CC is trimmed; its first command token (double quotes honored) is checked: basename with extension
  stripped, case-insensitive, equal to `cl` (or `clang-cl`) -> exit 3 before any process is spawned. For an
  unquoted value containing spaces, each space-delimited prefix naming an existing file is also checked.

## Exit codes (spec section 7.2)

- 2: unknown option / command, missing file argument. 3: missing or unreadable input file
  (`farmc: cannot read input file '...'`), no C compiler, MSVC selected, C compile/link failure.
- 4: any escaped C++ exception (`farmc: internal compiler error: ...`). Test hook: `FARMC_TEST_ICE=1` makes
  `farmc build` throw so `local_084_exit4_ice` can exercise the path; it has no other effect.

## Runner (scripts/run_one_m1.ps1)

- All expected-vs-actual comparisons are ordinal (case-sensitive).
- Blocks: only a `# end` line terminates a `# stdout:` / `# stderr:` block (README: "Everything between
  `# stdout:` and `# end` is compared exactly"). Inside a block every other line, including `#` and `##`
  lines, is literal content. Outside blocks, `##` lines are comments and ignored. This reading of the two
  README rules is our interpretation; no spec fixture has a `##` line inside a block.
