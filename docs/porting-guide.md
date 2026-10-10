# Porting guide (Three.js → Farmos)

## Scene graph mapping

| Three.js | Farmos |
|----------|--------|
| `Scene`, `Object3D`, `Mesh`, `BoxGeometry`, `SphereGeometry`, `PlaneGeometry` | `farmos:scene` (M3) |
| `MeshBasicMaterial`, `MeshStandardMaterial` | same names; M3 raster is Lambert+Blinn, not full PBR |
| `AmbientLight`, `DirectionalLight`, `PointLight` | same |
| `RectAreaLight` | M4 path tracer |
| `WebGLRenderer.render` | `Renderer.render` (CPU raster) |
| Path / ray / glass / mirror demos | `Renderer.renderPath` (M4) |
| `cannon` / `ammo` rigid bodies | `farmos:physics` `World` / `RigidBody` (M5) |

Math types (`Vector3`, `Matrix4`, `Quaternion`, `Euler`, `Color`) come from `"farmos:math"`, not from `"farmos:scene"`.

`renderer.render` never runs the path tracer. `renderPath` never replaces raster `render`. Call the one you mean.

## Math copy gotcha (M2 §3.7) — MUST read

Three.js `Vector3` is a mutable **reference**. Farmos math types are **values**:

```
let p = mesh.position;
p.x = 1.0;
// mesh.position is UNCHANGED
```

Idiomatic write-back:

```
mesh.position.x = 1.0;
mesh.position.set(1.0, 2.0, 3.0);
```

Assigning a vector copies. Mutating the copy does not touch the mesh. Field paths (`mesh.position.x = …`) and methods (`set` / `copy`) write the in-object storage.

Fixture: `spec/tests/M6/014_porting_position_copy_gotcha.fm` (also M3 `010_position_copy_gotcha`).

## Integer literals in float contexts

M2 §4A: a contextual int literal may become `float` (e.g. `position.set(1, 2, 3)`, `1 + 2.0`) when it is exactly representable in binary64. A non-literal `int` expression still needs `float(i)`.

## Physics sync (M5)

```
const body: RigidBody = new RigidBody(BODY_DYNAMIC);
body.setCollider(new SphereCollider(0.5));
body.setObject(mesh);   // attach
world.add(body);
world.step();           // mesh.position / quaternion updated
```

`setObject(mesh)` + `world.step()` write the rigid-body pose back to the `Object3D`. **Scale is not written** by physics. Detach or omit `setObject` and the mesh stays where you left it.

## Arrays

Dynamic `T[]` **aliases** on assign / param / return (M6 §6.2). Fixed `T[N]` still **copies**.

```
let a: int[] = [1, 2];
let b: int[] = a;
push(b, 3);   // len(a) is 3
```

```
let a: int[2] = [1, 2];
let b: int[2] = a;
b[0] = 9;     // a[0] is still 1
```

Array **literals** always allocate a fresh dynamic buffer.

## `for` + `continue`

`continue` inside `for` runs the update clause (M6 §6.1). A C/TS/Java port that skips even numbers with `continue` will not infinite-loop.

## Size when porting

Hello without physics / threads / `renderPath` must stay ≤ 20 KiB stripped on Windows. Unused `import { Vector3 } from "farmos:math"` is DCE’d (B-06). Do not take the address of `renderPath` or write a reachable `parallel` if you need the tiny binary.
