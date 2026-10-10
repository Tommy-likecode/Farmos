# Farmos M5 physics test fixtures

Sources use the M1 TypeScript-style surface, M2 math, M3 scene (for sync fixtures),
and the M5 physics module (`import { … } from "farmos:physics"`).
See `spec/M5-physics.md`. Do not edit M1–M4 specs to “fix” physics.

This file documents **M5-only** harness notes. It does **not** replace
`spec/tests/README.md`.

## Layout

```
spec/tests/M5/
  README.md
  NNN_name.fm
  NNN_name.expected
  _ref/
    README.md
    physics_ref.py     (numeric oracle — NOT product code)
```

## Kinds

| kind | Meaning |
|------|---------|
| `run` | Exact stdout + exit; stderr empty. |
| `run_approx` | M2 float-token epsilon compare (see `tests/M2/README.md`). |
| `runtime_trap` | Trap exit + stderr. |

No `run_png` and no `compile_error` in the M5 DRAFT set.

## Import prelude

Unless testing import errors, physics fixtures import only the names they use:

```
import { World, RigidBody, … } from "farmos:physics";
import { Vector3 } from "farmos:math";          // when needed
import { Mesh, … } from "farmos:scene";         // sync fixtures only
```

## Numeric oracle

`_ref/physics_ref.py` implements the option-A algorithms in `M5-physics.md`
(semi-implicit Euler, sequential impulse, geometric-mean materials, SAP-style
AABB pairs, sphere/box/plane narrowphase). Fixture `.expected` numbers are
baked from that script. The product MUST match those algorithms; the Python
file is reference data only.

## Comments

Lines starting with `##` in `.expected` files are ignored by the harness outside
`# stdout:` / `# stderr:` blocks (same as M1).
