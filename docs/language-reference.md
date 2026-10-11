# Farmos language reference

English docs in this folder are **normative**. `docs/使用手册.md` is an informative Chinese sibling (M6 §8.5 / OQ-M6-06 A). Language rules that amend M1–M5 live in `spec/M6-hardening.md`; M1–M5 milestone markdown bodies are not rewritten.

## 1. Introduction and CLI

Farmos is a statically typed, TypeScript-surface language. Sources use the `.fm` extension. `farmc` lowers them to C11 and invokes clang or gcc (`FARM_CC`). MSVC `cl` is unsupported as the C backend.

```
farmc build <file.fm> [-o out] [--emit-c [path]] [--keep-c] [-v] [--werror]
farmc run <file.fm> [-- args...]
farmc version
farmc help
```

Exit codes: `0` success, `1` compile error (and M6 unusable-TEMP abort), `2` usage, `3` driver failure, `4` internal compiler error.

On Windows, `FARM_CC` and argv paths are handled with wide APIs (`CreateProcessW`, `GetEnvironmentVariableW`, `CommandLineToArgvW`) so non-ACP characters are not silently truncated (M6 §7.1). If the temporary directory used for intermediate `.c` / objects is missing or not writable, `farmc` exits `1` and prints `error: temporary directory unavailable` (M6 §7.2).

## 2. Lexical structure and types

Identifiers are ASCII. Statements require semicolons. Comments: `//` and non-nested `/* */`.

| Type | Notes |
|------|--------|
| `int` | Signed 64-bit. Hex literals allowed (`0xff`). Overflow wraps. `/` or `%` by zero traps **101**. |
| `float` | IEEE binary64. `/0` is IEEE (`Infinity` / `NaN`), no trap. Print follows ES `Number::toString` plus `-0`. |
| `bool` | `true` / `false`. Conditions must be `bool`. |
| `string` | Immutable UTF-8. `+` concatenates. `len(s)` is byte length. |
| `T[N]` | Fixed array. Assignment **copies** all elements. |
| `T[]` | Dynamic array. Assignment / parameter / return **aliases** (M6 §6.2). Literals allocate a fresh buffer. |
| `struct` | Value type; fieldwise copy. |
| `class` | Reference type; `new` on the program arena. |

Modules: each `.fm` file is a module. `export` / `import { … } from "./x.fm"` for user modules. Built-in virtual modules: `"farmos:math"`, `"farmos:scene"`, `"farmos:physics"`.

## 3. Control flow

`if` / `else`, `while`, C/TS-style `for (init; cond; update)`, `break`, `continue`, `return`.

**`continue` inside `for` (M6 §6.1):** skip the rest of the current body, **evaluate the update clause**, then re-test the condition. This matches C / TypeScript / Java. `continue` inside `while` does **not** invent an update clause.

```
for (i = 0; i < 5; i = i + 1) {
  if (i == 2) { continue; }
  n = n + 1;
}
// i ends at 5; n == 4
```

`for` update is an assignment or a call (M1 grammar). Non-`Ident` updates and compound assigns still run on `continue`.

## 4. Arrays

| Operation | `T[]` (dynamic) | `T[N]` (fixed) |
|-----------|-----------------|----------------|
| `let b = a` | **Alias** — same buffer | **Copy** all elements |
| Pass as parameter | Alias (callee `push` / element writes are visible) | Copy |
| Return | Alias | Copy |
| `a[i] = v` | Writes element `i`; does not rebind | Writes the copy’s slot |
| `push(a, v)` | Mutates the shared buffer | N/A |
| `let a: T[] = [1, 2, 3]` | Fresh buffer; elements copied in | N/A |

Element values still follow their type’s copy/alias rules (`int` copies; `Mesh` stores a reference).

## 5. Diagnostics and traps

Format: `path:line:col: error[E0xxx]: message` (1-based Unicode scalar columns).

| Family | Typical use |
|--------|-------------|
| E00xx | UTF-8 / reserved words |
| E02xx | Syntax. Bare `!` / `=` in expression position stays **E0202**. |
| E03xx | Modules |
| E04xx | Types |
| E05xx | Names / `main` / missing `return` / `break` outside loop |
| E06xx | Operators / math. Non-overloadable `operator` forms → **E0605** (M6 §6.3). Overload of a built-in primitive → **E0601**. |
| E07xx | Scene |
| E08xx | `parallel` / `task` conflict |

Runtime traps (process exit): 101 integer `/0`, 102 bounds, 103 non-finite `int(float)`, 104 invalid Euler order, 105 use-after-dispose, 106 hierarchy cycle, 107 bad renderer size, 108 PNG write failure. M6 adds **no** new trap codes.

## 6. `farmos:math`

`Vector2/3/4`, `Matrix3/4`, `Quaternion`, `Color`, `Euler`, `Ray`, `Box3`, `Sphere`, `RayHit`. Math types are **structs (values)**. See the [porting guide](porting-guide.md) for the M2 §3.7 copy gotcha (`let p = mesh.position` copies).

Overloadable operators: `+ - * / % == !=` and unary `-`. **Not** overloadable: `!`, `=`, comparisons, `&&` `||`, `[]`, `()`, compound assigns → **E0605**.

Integer literals may coerce to float in float contexts (M2 §4A) when exactly representable.

## 7. `farmos:scene` and path tracing

Scene graph: `Scene`, `Object3D`, `Mesh`, geometries, materials, lights, `PerspectiveCamera`, `Renderer`.

- `renderer.render(scene, camera)` — M3 CPU rasterizer.
- `renderer.renderPath(scene, camera)` — M4 CPU path tracer (BVH, glass / mirror / area lights). `setSamples` / `setMaxBounces` / `setSize` apply. `FARMOS_THREADS` unset means 1 worker for `renderPath`.

`farm_ray.c` is linked only when `renderPath` is a reachable call.

## 8. Concurrency (`parallel` / `task`)

M3.5 compile-time conflict detection (`E0801`/`E0802`, `W0801`). Join is implicit. Prints from tasks flush in source order.

A program that **never executes** `parallel` and **never calls** `renderPath` must not link the worker-pool objects or import thread-start symbols (`_beginthreadex`, `_beginthread`, `CreateThread`, `pthread_create`, …) — M6 §4 / OQ-M6-05 A.

## 9. `farmos:physics`

`World`, `RigidBody`, `SphereCollider` / `BoxCollider` / `PlaneCollider`, `BODY_DYNAMIC` / `BODY_STATIC` / `BODY_KINEMATIC`. `world.step()` integrates a fixed timestep. `body.setObject(mesh)` writes pose back to the scene mesh after each step (scale is not written).

## 10. Size and DCE

Budgets (stripped Windows x64 PE unless noted) are in `spec/M6-hardening.md` §3:

| ID | Artifact | Budget |
|----|----------|--------|
| B-01 | hello, no graphics / physics / path / `parallel` | ≤ 20 KiB |
| B-02 | rotating cube | ≤ 96 KiB |
| B-03 | glass + mirror | ≤ 300 KiB |
| B-04 | `S_demo − S_scene` | ≤ 64 KiB |
| B-05 | Cornell 800×600 `renderPath` peak working set | ≤ 64 MiB |
| B-06 | unused `farmos:math` import | ≤ 20 KiB |
| B-07 | no `parallel` / no `renderPath` | no thread-start imports |

CI: `scripts\check_size_budgets.ps1` (Windows) / `scripts/check_size_budgets.py` (Linux). Primary entry: `scripts\build_and_test.ps1` (ctest `local_size_budgets`).

## Backlog disposition (M6 §11)

Every parked item is closed in M6 (no silent drops): `for`+`continue` update; DCE / `_beginthreadex` CI; `run_approx` default epsilon 1e-10; gimbal fixture 009; unused-math B-06; E0605 vs E0202; stale `M2_IMPLEMENTATION_STATUS.md` deleted; FARM_CC / TEMP / ACP; sha256-required / golden-optional; `T[]` alias; porting-guide §3.7.
