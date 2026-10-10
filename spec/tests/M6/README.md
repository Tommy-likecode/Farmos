# Farmos M6 hardening / docs test fixtures

Sources use the M1 TypeScript-style surface plus prior stdlib modules where needed.
See `spec/M6-hardening.md`. Do not edit M1–M5 milestone bodies to absorb these rules;
amendments live in `M6-hardening.md`.

This file documents **M6-only** harness notes. It does **not** replace
`spec/tests/README.md`.

## Layout

```
spec/tests/M6/
  README.md
  NNN_name.fm
  NNN_name.expected
```

## Kinds

| kind | Meaning |
|------|---------|
| `run` | Exact stdout + exit; stderr empty. |
| `run_approx` | M2 float-token epsilon compare. Default epsilon when `# epsilon:` omitted: **1e-10** (M6 §5.1). |
| `compile_error` | `farmc` exit 1; match `# error: line:col: E0xxx`. |

No `runtime_trap` and no `run_png` in this pack. Size / RAM / thread-symbol budgets are
eggtooth script checks (`scripts\check_size_budgets.ps1`), not `.fm` fixtures.

## Counts

15 fixtures: 11 `run`, 1 `run_approx`, 3 `compile_error`.

## Comments

Lines starting with `##` in `.expected` files are ignored by the harness outside
`# stdout:` / `# stderr:` blocks (same as M1).
