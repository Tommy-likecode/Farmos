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
