# Farmos M2 math test fixtures

Sources use the M1 TypeScript-style surface plus M2 math (`import { … } from "farmos:math"`, struct methods, operators). See `spec/M2-math.md` and `spec/M1-core.md`.

This file documents M2-only harness extensions. It does **not** replace `spec/tests/README.md`.

## Layout

```
spec/tests/M2/
  README.md                 (this file)
  NNN_name.fm
  NNN_name.expected
```

## Kinds

All M1 kinds remain valid:

| kind | Meaning |
|------|---------|
| `run` | Exact stdout + exit; stderr empty. |
| `compile_error` | `farmc` exit 1; match `# error: line:col: E0xxx`. |
| `runtime_trap` | Trap exit + stderr. |

### M2 extension: `run_approx`

```
# kind: run_approx
# exit: <integer>
# epsilon: <float>
# stdout:
...
# end
```

- Program compiles, links, runs.
- Exit code MUST match `# exit:`.
- stderr MUST be empty.
- stdout is tokenized on whitespace (ASCII space, tab, newline). Contiguous non-whitespace is one token.
- For each expected token:
  - If both expected and actual parse as floats under the rules below, assert `abs(actual - expected) <= epsilon`, OR both are NaN.
  - Otherwise tokens MUST be identical (byte-for-byte).
- Float tokens: optional leading `-`, M1 float literal forms, plus exact spellings `NaN`, `Infinity`, `-Infinity`, and M1 float print forms for finite values (including integral `1` without `.0`, scientific `1e-7` / `1e+21`).
- `# epsilon:` is required for `run_approx`. Suggested default in fixtures: `1e-12` for well-conditioned cases; loosen if needed for libm variance.

**Rationale:** Host `libm` transcendental results may differ across C runtimes; exact `run` fixtures MUST prefer binary-exact values (integers, halves, quarters, 3-4-5). Trig-dependent prints use `run_approx`.

## Import prelude in fixtures

Unless a fixture tests import errors, every math fixture begins with:

```
import { … } from "farmos:math";
```

listing only the names it uses (DCE discipline).

## Comments

Lines starting with `##` in `.expected` files are ignored by the harness (same as M1).
