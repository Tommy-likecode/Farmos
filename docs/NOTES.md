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

`print`/`println`/`str` for `float` follow M1 §6.1 / ECMAScript 2024 `Number::toString`: specials `NaN` / `Infinity` / `-Infinity` / `-0`, and shortest round-trip decimals with scientific notation when the decimal exponent \(k < -6\) or \(k \geq 21\`.

Implemented in `runtime/farm_rt.c` via a compact try-precision-1..17 + `strtod` round-trip loop, then ES-style formatting. With `-ffunction-sections` and `--gc-sections`, hello-world programs that never print floats should not retain the float formatter.
