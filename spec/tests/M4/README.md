# Farmos M4 ray-tracing test fixtures

Sources use the M1 TypeScript-style surface, M2 math, the M3 scene API, and the
M4 path-tracing additions (`renderPath`, `RectAreaLight`, `Texture`, extended
`MeshStandardMaterial`). See `spec/M4-ray.md`. Do not edit M1–M3.5 specs to
absorb these rules; amendments live in `M4-ray.md`.

This file documents **M4-only** harness extensions. It does not replace
`spec/tests/README.md` or the M3 / M3.5 READMEs. Kinds `run`, `compile_error`,
and `runtime_trap` keep their M1 meaning. `run_png` keeps the M3 meaning.

## Layout

```
spec/tests/M4/
  README.md
  NNN_name.fm
  NNN_name.expected
  NNN_name.golden.png          (run_png only)
  _ref/
    README.md
    path_ref.py                (golden generator — NOT product code)
    goldens_sha256.txt
    albedo.png                 (RGB8 texture for 010)
```

## Working directory

The harness MUST run each M4 program with its working directory set to
`spec/tests/M4` (the directory that contains the `.fm` files).

- `savePNG("out.png")` writes `spec/tests/M4/out.png`. The harness hashes that
  file (M3 `run_png` rule) and SHOULD byte-compare it to `NNN_name.golden.png`.
- `new Texture("_ref/albedo.png")` loads `spec/tests/M4/_ref/albedo.png`.
- Fixture 017 uses a path that does not exist (`no_such_texture.png`) and MUST
  trap before any render.

If a harness uses a temporary CWD instead, it MUST still make `_ref/albedo.png`
available at `_ref/albedo.png` relative to that CWD and MUST read `out.png`
from that same CWD. The shipped layout assumes the fixture directory itself.

## `# threads:`

Same header as M3.5 (`spec/tests/M3.5/README.md`). On a `run_png` fixture the
harness runs the binary once per listed worker count with `FARMOS_THREADS`
set to that count, and the PNG SHA-256 MUST match `# sha256:` every time.

M4 `renderPath` uses that variable for its internal tile pool (M4-ray.md).
Unset means **1** worker for `renderPath`, which is intentional and differs
from the M3.5 default of "logical CPU count" for user `parallel` blocks.
Header order: `# kind:`, `# exit:`, then `# threads:` if present, then
`# png:` / `# sha256:`.

## Counts

23 fixtures: 16 `run_png`, 1 `run`, 6 `runtime_trap`. No `compile_error`
(M4 adds no new diagnostic code). Image fixtures are at most 32×32 and at
most 4 samples so ctest stays small. 800×600 is an eggtooth memory check,
not a golden.

Regenerate goldens (reference only):

```
python3 spec/tests/M4/_ref/path_ref.py
```
