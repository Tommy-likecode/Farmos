# Farmos M3 scene test fixtures

Sources use M1 TypeScript-style surface + M2 math + M3 scene
(`import { … } from "farmos:scene"`, `import { … } from "farmos:math"`).
See `spec/M3-scene.md`, `spec/M2-math.md`, `spec/M1-core.md`.

This file documents **M3-only** harness extensions. It does **not** replace
`spec/tests/README.md` and does **not** amend the M1 README.

## Layout

```
spec/tests/M3/
  README.md                 (this file)
  NNN_name.fm
  NNN_name.expected
  NNN_name.golden.png       (for run_png fixtures)
  _ref/
    README.md
    raster_ref.py           (golden generator — NOT product code)
    goldens_sha256.txt
```

## Kinds

All M1/M2 kinds remain valid:

| kind | Meaning |
|------|---------|
| `run` | Exact stdout + exit; stderr empty. |
| `run_approx` | M2 float-token epsilon compare (see `tests/M2/README.md`). |
| `compile_error` | `farmc` exit 1; match `# error: line:col: E0xxx`. |
| `runtime_trap` | Trap exit + stderr. |

### M3 extension: `run_png`

```
# kind: run_png
# exit: <integer>
# png: <relative-path>
# sha256: <64 lowercase hex chars>
# stdout:
...
# end
```

- Program compiles, links, runs.
- Exit code MUST match `# exit:`.
- stderr MUST be empty.
- stdout MUST match the `# stdout:` block exactly (same rules as `run`).
- After the process exits, the harness computes SHA-256 over the file named by
  `# png:` (path relative to the process working directory used for the run —
  fixtures write a basename such as `out.png` into that CWD) and compares it
  **case-insensitive** to `# sha256:`.
- When a sibling `NNN_name.golden.png` exists, the harness SHOULD also
  byte-compare the output file to that golden (exact match). SHA-256 is the
  required gate; golden byte-compare is a convenience.

**Rationale:** Canonical PNG encoding is specified in `M3-scene.md` §11.3 so
hashes are stable across machines. Goldens are produced by
`tests/M3/_ref/raster_ref.py` (reference data only).

## Import prelude

Unless testing import errors, scene fixtures import only the names they use:

```
import { Scene, Renderer, … } from "farmos:scene";
import { Vector3, Color, … } from "farmos:math";
```

## Comments

Lines starting with `##` in `.expected` files are ignored by the harness outside
`# stdout:` / `# stderr:` blocks (same as M1).
