# Farmos M3 — Three.js-like Scene API Specification

**Status:** FINAL (PM-approved 2026-09-26)  
**Milestone:** M3 Three.js-like scene API  
**Depends on:** `spec/M1-core.md` (FINAL), `spec/M2-math.md` (FINAL)  
**Normative keywords:** MUST / SHOULD / MAY per RFC 2119.

This document specifies the Farmos M3 scene-graph and renderer standard library, reclaim policy (resolving M1 OQ-M1-17), PNG output, deterministic CPU rasterization, diagnostics, acceptance criteria, and the M3 test index. It does **not** specify compiler/runtime/stdlib implementation code.

Related: `/workspace/farmos/PLAN.md`, `/workspace/farmos/spec/M1-core.md`, `/workspace/farmos/spec/M2-math.md`, `/workspace/farmos/spec/tests/M3/README.md`.

**Target platform (PLAN.md):** Windows x64 only; C backend via clang or gcc (MSVC `cl` unsupported); fully local. Size ACs use Windows x64 stripped PE.

---

## 1. Scope and non-goals

### 1.1 In scope (M3 MUST implement)

- Built-in module `farmos:scene` exporting the types in §2.2.
- Scene graph: `Object3D` hierarchy with `position` / `rotation` / `quaternion` / `scale` / children; local and world matrices.
- `PerspectiveCamera`, `Mesh`, geometries (`BoxGeometry`, `SphereGeometry`, `PlaneGeometry`), materials (`MeshBasicMaterial`, `MeshStandardMaterial`), lights (`AmbientLight`, `DirectionalLight`, `PointLight`), `Scene`, `Renderer`.
- `renderer.setSize` / `renderer.render(scene, camera)` / `renderer.savePNG(path)`.
- Deterministic **CPU software rasterizer** (depth buffer + basic shading) producing canonical PNGs (§11–§12).
- Explicit `dispose()` on geometries, materials, and `Renderer` (OQ-M1-17 resolution, §13).
- Fixture pack under `spec/tests/M3/` (≥ 25), including PNG goldens and the rotating-cube smoke test.
- Porting notes documenting M2 §3.7 copy-vs-alias and M2 §4A int-lit→float (Appendix A).

### 1.2 Explicit non-goals (M3 MUST NOT require)

| Deferred | Target |
|----------|--------|
| Full PBR BRDF, IBL, image-based lighting | M4 |
| BVH, path/ray tracing, shadows, reflection, refraction | M4 |
| Textures, multi-material groups, `BufferGeometry` user builders | post-M3 / M4 as needed |
| `WebGLRenderer` / GPU backend | stretch / later |
| OrthographicCamera, Group-as-separate-type, Layers, Fog | post-M3 |
| Animation loops / `requestAnimationFrame` | out of language (user `while` / fixed frame) |
| Physics sync | M5 |
| Inheritance / interfaces for user subclasses of Object3D | post-M1 (unchanged) |
| `null` / `Optional<T>` | post-M1 (unchanged; see §8 background) |
| Scoped arenas / GC | not in M3 (see §13) |

### 1.3 Design constraints

- PLAN.md M3 AC: Three.js “rotating cube” ports with near-identical structure; renders to PNG.
- M2: math types are **value** `struct`s; scene nodes are **class** references; `mesh.position.x = …` mutates inline storage (M2 §3.1).
- M2 §4A int-literal→float is in effect (ports may write `new Vector3(1, 2, 3)`).
- M1: no `null`; program-lifetime arena for classes; Windows x64 only.
- Unused `farmos:scene` MUST NOT grow `hello.exe` vs M2 baseline (AC-M3-01).

### 1.4 Dependencies on M2 (normative)

| M2 item | M3 use |
|---------|--------|
| `farmos:math` `Vector3` | `Object3D.position` / `scale`, light positions, `lookAt`, etc. |
| `Matrix4` | `matrix` / `matrixWorld` / `matrixWorldInverse` / projection |
| `Quaternion` | `Object3D.quaternion`; synced with `rotation` |
| `Euler` | `Object3D.rotation`; synced with `quaternion` |
| `Color` | materials, lights, `Scene.background` |
| §4A int-lit→float | constructor and field assignment numerics in ports |
| §3.7 copy-vs-alias | **MUST** be documented in M3 porting notes (this spec Appendix A) |

Users import math types from `"farmos:math"` and scene types from `"farmos:scene"` as needed. `farmos:scene` does **not** re-export math types.

---

## 2. Module exposure

### 2.1 Built-in module path

```
import { Scene, PerspectiveCamera, BoxGeometry, MeshBasicMaterial, Mesh, Renderer } from "farmos:scene";
import { Vector3, Color } from "farmos:math";
```

- Virtual module id `"farmos:scene"` (same rules as `"farmos:math"`, M2 §2.1).
- Unknown `farmos:…` id → `E0304`.
- No global scene prelude.

### 2.2 Exports

`farmos:scene` MUST export exactly:

`Object3D`, `Scene`, `PerspectiveCamera`, `Mesh`, `BoxGeometry`, `SphereGeometry`, `PlaneGeometry`, `MeshBasicMaterial`, `MeshStandardMaterial`, `AmbientLight`, `DirectionalLight`, `PointLight`, `Renderer`.

Geometries MAY share a common undocumented internal base; user-visible types are the four names above (three geometry classes + materials/lights/etc.). There is no public `BufferGeometry` class in M3.

### 2.3 Linking / DCE

- Unused `farmos:scene` MUST NOT change hello-world stripped size vs M2-capable baseline (AC-M3-01).
- Using only a subset (e.g. `MeshBasicMaterial` without `MeshStandardMaterial` lighting) SHOULD link only the needed raster paths when practical; not hard-gated beyond AC-M3-01 / AC-M3-10.

---

## 3. Reference vs value (critical)

### 3.1 Scene-graph objects are classes

The following MUST be `class` types with **reference semantics** (M1 §4.7):

`Object3D`, `Scene`, `PerspectiveCamera`, `Mesh`, `BoxGeometry`, `SphereGeometry`, `PlaneGeometry`, `MeshBasicMaterial`, `MeshStandardMaterial`, `AmbientLight`, `DirectionalLight`, `PointLight`, `Renderer`.

| Operation | Semantics |
|-----------|-----------|
| `let a = b` for these types | Copy the reference (alias). |
| `scene.add(mesh)` | Store the same reference in the children list. |
| Shared material `new Mesh(g1, mat); new Mesh(g2, mat)` | Both meshes alias `mat`. |
| Shared geometry likewise | Both meshes alias the geometry. |

### 3.2 Math fields stored inline (M2 §3.1 / §3.7)

`Object3D.position`, `.scale` are `Vector3` **struct fields stored in the object**. `rotation` is `Euler`; `quaternion` is `Quaternion`. Materials’ `color` and lights’ `color` are inline `Color` fields.

```
mesh.position.x = 1.0;              // MUST mutate mesh
mesh.position.add(new Vector3(0,1,0)); // MUST mutate mesh
let p: Vector3 = mesh.position;     // COPY (M2 §3.7)
p.x = 9.0;                          // does NOT change mesh
```

### 3.3 No operator overloading on scene types

Scene classes do not define `+`/`*` etc. Math operators remain on `farmos:math` types only.

---

## 4. `Object3D`

```
class Object3D {
  position: Vector3;
  rotation: Euler;          // radians; default order "XYZ"
  quaternion: Quaternion;
  scale: Vector3;
  matrix: Matrix4;          // local
  matrixWorld: Matrix4;
  matrixAutoUpdate: bool;
  visible: bool;
  // children: see §4.3 — not a user-mutable raw array field in M3
}
```

### 4.1 Construction and defaults

`new Object3D()`:

| Field | Default |
|-------|---------|
| `position` | `(0,0,0)` |
| `rotation` | `(0,0,0,"XYZ")` |
| `quaternion` | `(0,0,0,1)` |
| `scale` | `(1,1,1)` |
| `matrix` / `matrixWorld` | identity |
| `matrixAutoUpdate` | `true` |
| `visible` | `true` |
| parent | none |
| children | empty |

### 4.2 Euler ↔ quaternion sync (Three.js mirror)

Object3D maintains **both** `rotation` (Euler) and `quaternion`. They MUST stay synchronized:

1. Writing `rotation` (field assign of a whole `Euler`, or mutating `rotation.x` / `y` / `z` / `order` through the field path, or calling mutating Euler methods on `this.rotation`) MUST update `quaternion` via `quaternion.setFromEuler(rotation)` using the Euler’s current order.
2. Writing `quaternion` (whole assign or mutating components / quaternion methods on `this.quaternion`) MUST update `rotation` via `rotation.setFromQuaternion(quaternion, rotation.order)` (preserve order string).
3. `setRotationFromEuler(e: Euler): void` — copy into `rotation` then sync quaternion.
4. `setRotationFromQuaternion(q: Quaternion): void` — copy into `quaternion` then sync Euler.

**Implementation note:** implementations MAY track a dirty flag; observable behavior MUST match the above after any read of `rotation` or `quaternion` or before `updateMatrix`.

Invalid Euler order strings still trap **104** (M2 §14) when an Euler method validates order.

### 4.3 Hierarchy

```
add(child: Object3D): Object3D          // append; return this (receiver-return for chaining on class ref)
remove(child: Object3D): Object3D       // remove first match; return this
addAt(child: Object3D, index: int): Object3D
childCount(): int
getChild(index: int): Object3D          // OOB → trap 102
```

Rules:

1. `add` / `addAt` MUST remove `child` from its previous parent (if any) before inserting.
2. Adding an object to itself or creating a cycle (child is an ancestor of `this`) → runtime trap **106**, stderr `runtime error: Object3D hierarchy cycle`.
3. `addAt`: `index < 0` or `index > childCount()` → trap **102**. `index == childCount()` appends.
4. `remove` of a non-child is a no-op.
5. Children order is the traversal / render order among siblings (§4.6).
6. There is **no** public `children` array field in M3 (avoids exposing `Object3D[]` mutation pitfalls). Use `childCount` / `getChild`.

`Scene.add` / `remove` / `addAt` are inherited behavior (Scene extends Object3D — §5).

### 4.4 Local matrix

```
updateMatrix(): void
```

If called (and whenever `matrixAutoUpdate == true` before world updates):

`matrix.compose(position, quaternion, scale)` using `Matrix4.compose` (M2 §11).

### 4.5 World matrix

```
updateMatrixWorld(force: bool): void
```

1. If `matrixAutoUpdate`, call `updateMatrix()`.
2. If no parent: `matrixWorld = matrix` (copy).
3. Else: `matrixWorld = parent.matrixWorld * matrix` (parent updated first).
4. Recurse to children with the same `force` flag.
5. `force` MUST force recomputation even if an implementation uses dirty bits; for M3, always recomputing is conforming.

`renderer.render` MUST call `scene.updateMatrixWorld(true)` (and update the camera’s world matrix) before drawing.

### 4.6 Traversal

```
traverse(…): void   // NOT in M3 as a callback API (no closures)
```

M3 has **no** `traverse` callback (M1: no closures). Render traversal is internal:

- Depth-first, **pre-order**, children in `getChild` index order `0 .. childCount()-1`.
- Skip a subtree if `visible == false` (object and descendants not drawn). Lights that are not visible MUST NOT contribute (§10).

### 4.7 `lookAt`

```
lookAt(x: float, y: float, z: float): void
lookAt(target: Vector3): void
```

Orient this object so its local **-Z** axis points toward the target (Three.js Object3D.lookAt), with up-vector `(0,1,0)`. Updates `quaternion` and syncs `rotation`. Does **not** change `position`.

---

## 5. `Scene`

```
class Scene extends Object3D semantics {
  // background: see below — no null
  hasBackground: bool;
  background: Color;
}
```

**Inheritance:** M1 has no user `extends`. For stdlib only, `Scene` is a distinct `class` that **is-a** `Object3D` for hierarchy purposes: it MAY be used wherever an `Object3D` parent is required, exposes the same transform/children API, and MAY be passed to APIs expecting `Object3D`. User code cannot declare subclasses.

### 5.1 Background (no null)

M1 forbids `null`. Scene background uses:

| Fields | Meaning |
|--------|---------|
| `hasBackground == false` | Clear color is **black** `(0,0,0)` (default). `background` field is `(0,0,0)` but ignored for clear. |
| `hasBackground == true` | Clear uses `background` (raw `Color`, M2 §13). |

```
setBackground(color: Color): void   // sets background = color; hasBackground = true
clearBackground(): void             // hasBackground = false
```

`new Scene()`: `hasBackground = false`, `background = (0,0,0)`.

---

## 6. `PerspectiveCamera`

```
class PerspectiveCamera /* Object3D-like transforms */ {
  fov: float;       // vertical field of view, **degrees**
  aspect: float;
  near: float;
  far: float;
  matrixWorldInverse: Matrix4;
  projectionMatrix: Matrix4;
  // plus Object3D transform fields: position, rotation, quaternion, scale, matrix, matrixWorld, …
}
```

`PerspectiveCamera` is a scene-graph node (same transform/children API as `Object3D`, may be added to a scene though typically not required for rendering).

### 6.1 Constructor

```
new PerspectiveCamera(fov_degrees: float, aspect: float, near: float, far: float)
```

- `fov` is **degrees** (Three.js), converted to radians only when building the projection matrix.
- Defaults if a future overload `new PerspectiveCamera()` is added: not required in M3; the four-arg form is required.
- `near > 0` and `far > near` SHOULD hold; if `near <= 0` or `far <= near`, behavior is implementation-defined for projection contents but MUST NOT crash; fixtures use valid values.

### 6.2 Projection update

```
updateProjectionMatrix(): void
```

MUST set `projectionMatrix` equivalent to Three.js / M2 `Matrix4.makePerspective` derived from fov/aspect/near/far:

```
top = near * tan(π/180 * 0.5 * fov)
height = 2 * top
width = aspect * height
left = -0.5 * width
right = left + width
bottom = -top   // symmetric
// then Matrix4.makePerspective(left, right, top, bottom, near, far) per M2 §11
```

### 6.3 `lookAt`

Same overloads as Object3D §4.7.

### 6.4 Before render

`renderer.render` MUST:

1. `camera.updateMatrixWorld(true)` (via Object3D rules).
2. `camera.matrixWorldInverse = inverse(camera.matrixWorld)`.
3. `camera.updateProjectionMatrix()` (or require user to call it; **normative default:** `render` calls it every frame so ports need not).

---

## 7. Geometries

All geometry classes are reference types. They store CPU vertex attributes used by the rasterizer.

### 7.1 Common API

Each geometry:

```
dispose(): void     // §13
```

Internal (not necessarily user-visible fields): positions `float`×3 per vertex, normals `float`×3, index buffer of `int` triangles.

Winding: **CCW when viewed from outside** (opposite the outward normal), matching §12 front-face test.

### 7.2 `BoxGeometry`

```
new BoxGeometry(width: float, height: float, depth: float)
new BoxGeometry()  // equivalent to (1,1,1)
```

Axis-aligned box centered at the origin. 24 vertices (unique normals per face), 12 triangles. Face layout MUST match the reference generator `spec/tests/M3/_ref/raster_ref.py` `box_geometry` (normative for golden fixtures).

### 7.3 `PlaneGeometry`

```
new PlaneGeometry(width: float, height: float)
new PlaneGeometry()  // (1,1)
```

XY plane, facing **+Z**, centered at origin. 4 vertices, 2 triangles. Winding CCW from +Z. Match `_ref` `plane_geometry`.

### 7.4 `SphereGeometry`

```
new SphereGeometry(radius: float, widthSegments: int, heightSegments: int)
new SphereGeometry(radius: float)           // widthSegments=32, heightSegments=16
new SphereGeometry()                        // radius=1, 32, 16
```

- `widthSegments` MUST be ≥ 3; `heightSegments` ≥ 2. If a literal/argument is below the minimum, clamp up to the minimum at construction (Three.js-style) — do not trap.
- UV sphere centered at origin; poles on ±Y; triangles CCW outward.
- Exact vertex count: `(widthSegments+1) * (heightSegments+1)` style strip topology is fine; golden PNG fixtures in M3 do **not** depend on sphere tessellation (sphere covered by non-PNG run tests / structural tests).

---

## 8. Materials

### 8.1 `MeshBasicMaterial`

```
class MeshBasicMaterial {
  color: Color;
  dispose(): void;
}
```

| Constructor | Meaning |
|-------------|---------|
| `new MeshBasicMaterial()` | `color = (1,1,1)` |
| `new MeshBasicMaterial(color: Color)` | copy color |
| `new MeshBasicMaterial(hex: int)` | `color.setHex(hex)` raw (M2 §13.2) |

**Shading:** unlit. Output the material `color` (clamped to [0,1] per channel when quantizing to PNG). **Ignores all lights.**

### 8.2 `MeshStandardMaterial`

```
class MeshStandardMaterial {
  color: Color;
  roughness: float;    // default 1.0
  metalness: float;    // default 0.0
  dispose(): void;
}
```

| Constructor | Meaning |
|-------------|---------|
| `new MeshStandardMaterial()` | white, roughness 1, metalness 0 |
| `new MeshStandardMaterial(color: Color)` | color as given; roughness 1; metalness 0 |
| `new MeshStandardMaterial(hex: int)` | from hex; roughness 1; metalness 0 |

Optional setters (MUST exist):

```
set(color: Color): void
setRoughness(r: float): void
setMetalness(m: float): void
```

Roughness / metalness values outside [0,1] are allowed to be stored; the shading model clamps to [0,1] at shade time.

#### 8.2.1 M3 shading model (simplified — not full PBR)

**Normative for M3.** M4 MAY replace the implementation with a full PBR BRDF **without changing** the material field API (`color`, `roughness`, `metalness`). Ports MUST NOT rely on M3 pixel-exact StandardMaterial results beyond the published goldens.

Let `albedo = material.color`, `N` unit world normal, `V = normalize(cameraPosition - worldPos)`, `metal = clamp(metalness,0,1)`, `rough = clamp(roughness,0,1)`.

```
result = (0,0,0)

for each contributing AmbientLight L:
  result += albedo * L.color * L.intensity

shininess    = 1.0 + (1.0 - rough) * 255.0
specStrength = 0.2 + 0.8 * (1.0 - rough)
specColor    = lerp((1,1,1), albedo, metal)   // per-channel

for each contributing DirectionalLight L:
  Ldir = normalize(-worldDirection(L))   // incident light direction toward the surface
  NdotL = max(0, dot(N, Ldir))
  result += albedo * L.color * L.intensity * NdotL * (1.0 - 0.9 * metal)
  H = normalize(Ldir + V)
  NdotH = max(0, dot(N, H))
  specular = (NdotH ** shininess) * specStrength     // if NdotH==0 → 0
  result += specColor * L.color * L.intensity * specular

for each contributing PointLight L:
  toLight = L.worldPosition - worldPos
  dist = length(toLight)
  if dist == 0: skip
  Ldir = toLight / dist
  // Three.js-style inverse-square with distance/decay defaults (§10.3):
  attenuation = attenuate(L, dist)
  NdotL = max(0, dot(N, Ldir))
  result += albedo * L.color * L.intensity * NdotL * (1.0 - 0.9 * metal) * attenuation
  H = normalize(Ldir + V)
  NdotH = max(0, dot(N, H))
  specular = (NdotH ** shininess) * specStrength
  result += specColor * L.color * L.intensity * specular * attenuation

return clamp(result, 0, 1) per channel
```

`**` is IEEE `pow`. Host `libm` variance on `pow` for StandardMaterial goldens is absorbed by publishing goldens from `_ref` (same formula); poteto MUST use the same expression order and `pow`.

---

## 9. `Mesh`

```
class Mesh /* Object3D transforms + children */ {
  geometry: /* BoxGeometry | SphereGeometry | PlaneGeometry */;
  material: /* MeshBasicMaterial | MeshStandardMaterial */;
}
```

```
new Mesh(geometry: G, material: M)
```

- Stores **references** to `geometry` and `material` (sharing allowed).
- Is an Object3D-like node (transforms, children, add/remove).

Type checking: `geometry` MUST be one of the three geometry classes; `material` one of the two materials; else `E0408` / `E0411` as appropriate.

---

## 10. Lights

Lights are Object3D-like nodes (have transforms; may be parented). Only lights that are reachable from the `Scene` root via the child hierarchy and have `visible == true` contribute.

### 10.1 `AmbientLight`

```
new AmbientLight(color: Color, intensity: float)
new AmbientLight(hex: int, intensity: float)
new AmbientLight(hex: int)               // intensity = 1.0
new AmbientLight()                       // color white (1,1,1), intensity = 1.0
```

Fields: `color: Color`, `intensity: float`. Direction/position ignored.

### 10.2 `DirectionalLight`

```
new DirectionalLight(color: Color, intensity: float)
new DirectionalLight(hex: int, intensity: float)
new DirectionalLight(hex: int)           // intensity 1
new DirectionalLight()                   // white, 1
```

Fields: `color`, `intensity`, plus Object3D transforms.

**World direction:** default orientation matches Three.js: local **-Z** is the direction the light “points”. The world-space travel direction of light rays is the world-space unit vector of local `(0,0,-1)` after `matrixWorld` (same as transforming direction `(0,0,-1)`). In §8.2.1, `worldDirection(L)` is that vector; incident `Ldir = normalize(-worldDirection(L))`.

Default: identity transform → light points toward **-Z** world; `Ldir = (0,0,1)` (light coming from -Z toward +Z). Fixtures that need a sun-like angle rotate/position the light or document direction via rotation.

Informative helper for ports (not required): place light and `lookAt` a point so **-Z** aims at the target.

### 10.3 `PointLight`

```
new PointLight(color: Color, intensity: float, distance: float, decay: float)
new PointLight(color: Color, intensity: float)
new PointLight(hex: int, intensity: float)
new PointLight(hex: int)
new PointLight()
```

Defaults: color white, `intensity = 1`, `distance = 0`, `decay = 2`.

Attenuation (Three.js r152-ish physical-ish for M3):

```
if distance == 0:
  attenuation = 1.0 / max(decay_factor, 1e-6)   // where decay_factor = dist^decay with decay default 2 → 1/dist^2
else:
  attenuation = dist < distance
      ? (1 - dist/distance) ^ decay   // simplified; OR physical 1/dist^2 within range
      : 0
```

**Normative M3 choice (Resolved):**  

```
attenuation = 1.0 / max(dist * dist, 1e-6)     // when decay == 2 (default)
if distance > 0 and dist >= distance: attenuation = 0
if distance > 0 and dist < distance:
  // still use inverse-square; distance is a hard cutoff only
```

For `decay != 2`: `attenuation = 1.0 / max(pow(dist, decay), 1e-6)` with the same hard cutoff when `distance > 0`.

World position = translation of `matrixWorld`.

---

## 11. `Renderer` and PNG output

### 11.1 Construction and size

```
class Renderer {
  constructor();                        // width=0, height=0 until setSize
  constructor(width: int, height: int); // calls setSize
  setSize(width: int, height: int): void
  render(scene: Scene, camera: PerspectiveCamera): void
  savePNG(path: string): void
  dispose(): void
}
```

- `setSize`: allocate RGB8 framebuffer `width*height*3` and float depth buffer `width*height`. Width/height ≤ 0 → trap **107**, `runtime error: invalid renderer size`.
- Maximum size in M3: implementations MUST support at least **2048×2048**; larger MAY fail with trap **107**.

### 11.2 `render`

```
renderer.render(scene, camera)
```

MUST:

1. Reject if renderer or any needed resource is disposed (§13) → trap **105**.
2. Require `width > 0 && height > 0` else trap **107**.
3. `scene.updateMatrixWorld(true)`; update camera world + inverse + projection (§6.4).
4. Clear color buffer to scene clear color (§5.1); clear depth to `+Infinity`.
5. Traverse and rasterize all visible meshes (§12).
6. Leave the internal RGB buffer as the image for `savePNG`.

`render` does **not** write a file.

### 11.3 `savePNG`

```
renderer.savePNG(path: string): void
```

Writes the **last** `render` framebuffer to `path` using the **canonical PNG** encoding below. If never rendered, or after `setSize` without a new `render`, contents are the clear-only buffer from the last render attempt — fixtures always `render` then `savePNG`.

I/O failure → trap **108**, `runtime error: PNG write failed`.

#### Canonical PNG (normative — required for golden stability)

1. PNG signature: `89 50 4E 47 0D 0A 1A 0A`.
2. Chunks **only**: `IHDR`, `IDAT`, `IEND` (no `sRGB`, `tEXt`, `gAMA`, …).
3. `IHDR`: width, height, bit depth **8**, color type **2** (RGB), compression 0, filter 0, interlace **0**.
4. Scanlines: each precedes filtered bytes with filter type **`None` (0)**.
5. `IDAT` zlib: compression level **0** (stored/no compression), deterministic zlib headers as produced by a level-0 deflate of the filtered raw image.
6. Pixel order: row 0 = **top** of image; column 0 = **left**; RGB8; Y increases **downward** in the file.
7. Channel quantization from float shade in [0,1]: `byte = floor(c * 255.0 + 0.5)` after clamp to [0,1] (same bias as M2 `Color.getHex`).

Harness compares either exact file bytes to `*.golden.png` or SHA-256 of the file (AC-M3-04).

### 11.4 Image / clip conventions

| Space | Rule |
|-------|------|
| World | Right-handed, **Y-up** (M2 §4) |
| Camera | Looks down local **-Z**; up local **+Y** |
| NDC | After projection + divide: `x,y,z ∈ [-1,1]` (OpenGL-style) |
| Viewport | `sx = (ndc_x * 0.5 + 0.5) * width`; `sy = (1 - (ndc_y * 0.5 + 0.5)) * height` |
| Depth | NDC `z`; closer to camera has **smaller** NDC z in the OpenGL mapping used by `makePerspective`; depth test: pass if `z_ndc < depth[x,y]` (strict), then store |

---

## 12. CPU software rasterizer (normative algorithm)

M3 MUST implement a **deterministic software rasterizer** (not a path tracer). Pseudocode:

```
for each Mesh m in scene (pre-order, visible):
  for each triangle (i0,i1,i2) in m.geometry:
    p0,p1,p2 = transform positions by m.matrixWorld
    n0,n1,n2 = transform normals as directions by m.matrixWorld (normalize)
    clip0..2 = projectionMatrix * matrixWorldInverse_camera * p   // as Matrix4 point transform
    if any clip.w <= 0: reject triangle
    ndc = clip.xyz / clip.w
    screen = viewport(ndc)   // §11.4
    area = edge(s0,s1,s2)
    if area <= 0: continue   // back-face or degenerate (CCW NDC → area>0 in Y-down)
    for each pixel center (x+0.5,y+0.5) in triangle bbox ∩ screen:
      barycentric w0,w1,w2 via edge functions; require all >= 0
      z = b0*z0+b1*z1+b2*z2
      if z >= depth[y*width+x]: continue
      depth[...] = z
      interpolate world position / normal with b0,b1,b2; normalize normal
      if MeshBasicMaterial: color = material.color
      else: color = shade_standard(...)   // §8.2.1
      write RGB bytes
```

Edge function: `edge(a,b,c) = (c.x-a.x)*(b.y-a.y) - (c.y-a.y)*(b.x-a.x)`.

No multisampling, no blending, no gamma/sRGB conversion (raw Color → bytes).

Reference: `spec/tests/M3/_ref/raster_ref.py` (golden generator only; not product code).

---

## 13. Memory reclaim — resolution of OQ-M1-17

### 13.1 Decision (normative)

1. **Keep** the M1 **program-lifetime arena** for all class instances (scene graph nodes, materials, geometries, renderer objects as language objects).
2. Add explicit **`dispose()`** on `BoxGeometry`, `SphereGeometry`, `PlaneGeometry`, `MeshBasicMaterial`, `MeshStandardMaterial`, and `Renderer` that frees the **native CPU buffers** associated with that object (vertex/index arrays, framebuffer/depth, etc.).
3. Failing to `dispose()` in long-running loops **leaks native buffers until process exit** (arena still holds the tiny object header). Document this for ports.
4. Scoped arenas / automatic reclaim are **not** in M3 (recorded as closed alternative below).

### 13.2 `dispose` semantics

```
geometry.dispose(): void
material.dispose(): void
renderer.dispose(): void
```

- First `dispose()` frees native buffers and marks the object disposed.
- Second `dispose()` is a **no-op**.
- After dispose, `render` / `savePNG` / raster use of that geometry or material / renderer → trap **105**, stderr `runtime error: use after dispose`.
- Reading fields like `mesh.position` on a Mesh whose geometry was disposed remains legal; only drawing paths trap.
- Disposing a material/geometry still referenced by a live Mesh is allowed; next `render` that draws that mesh traps **105**.

### 13.3 What OQ-M1-17 alternatives were rejected

| Option | Disposition |
|--------|-------------|
| A — Program arena + explicit `dispose()` on GPU/CPU buffers (Three.js-like) | **Accepted** |
| B — Scoped arenas reclaiming all `new` at block exit | Rejected for M3: breaks escaping Mesh helpers / scene ownership |

---

## 14. Diagnostics (E07xx)

Compile-time codes for scene (no collision with M1 ≤ E0513 or M2 E06xx):

| Code | Message template |
|------|------------------|
| E0701 | `{name}` is not a scene-graph geometry type |
| E0702 | `{name}` is not a scene material type |
| E0703 | `savePNG` path argument must be `string` |
| E0704 | disposed-object API misuse detected at compile time (reserved; M3 MAY leave unused) |

Ordinary type errors continue to use M1 `E0408` / `E0411` / `E0505` etc. Prefer M1 codes when they already apply (wrong arg type to `add`, etc.). E0701–E0702 are for dedicated geometry/material slot mismatches when the checker distinguishes them.

---

## 15. Runtime traps (additions)

| Exit | Message | When |
|------|---------|------|
| 105 | `use after dispose` | Use of disposed geometry/material/renderer in render/save path |
| 106 | `Object3D hierarchy cycle` | `add`/`addAt` would create a cycle |
| 107 | `invalid renderer size` | `setSize`/`render` with invalid dimensions |
| 108 | `PNG write failed` | `savePNG` I/O failure |

Existing: 101 int div0, 102 OOB (also child index / addAt), 103 non-finite→int, 104 invalid Euler order.

---

## 16. Acceptance criteria

| ID | Criterion | PLAN / fixtures |
|----|-----------|-----------------|
| AC-M3-01 | Windows x64: build `spec/tests/M1/001_hello.fm` with M3-capable `farmc`, strip; size **equals** M2 baseline hello on same toolchain | PLAN size / DCE |
| AC-M3-02 | Three.js-like rotating-cube program (Appendix A structure) compiles and writes a PNG; fixture `003_rotating_cube` passes | PLAN M3 AC |
| AC-M3-03 | ≥ **25** fixtures under `spec/tests/M3/` pass via `scripts\build_and_test.ps1` (this pack: **32**) | quality gate |
| AC-M3-04 | `kind: run_png` goldens: output PNG SHA-256 matches `# sha256:` (canonical encoding §11.3) | determinism |
| AC-M3-05 | `mesh.position.x = …` / `mesh.position.add(…)` mutate the mesh (fixture); assign-to-local copies (fixture) | M2 §3.7 |
| AC-M3-06 | Euler↔quaternion sync: write rotation, read quaternion (and reverse) fixtures pass | §4.2 |
| AC-M3-07 | `scene.add` / `remove` / hierarchy cycle trap 106 | §4.3 |
| AC-M3-08 | MeshBasic ignores lights; MeshStandard uses Ambient/Directional/Point per §8–§10 | fixtures |
| AC-M3-09 | `dispose` then `render` → trap 105 | §13 |
| AC-M3-10 | Soft size: stripped rotating-cube demo PE ≤ **96 KiB** on TommyLaptop (clang or gcc as driver) | size budget |
| AC-M3-11 | Unused `import { … } from "farmos:scene"` with no uses: hello size unchanged (covered by AC-M3-01 if DCE of unused import is total; empty import list still grammar-forbidden) | DCE |
| AC-M3-12 | Diagnostics format unchanged (`path:line:col: error[E0xxx]:`) | M1 §8 |
| AC-M3-13 | Solid clear / unlit cube / rotating cube PNG goldens match `_ref` hashes | smoke |

---

## 17. Test harness extension (`run_png`)

Documented in `spec/tests/M3/README.md` only (do not edit M1 README).

```
# kind: run_png
# exit: <integer>
# png: <relative-path-written-by-program>
# sha256: <64 hex chars>
# stdout:
...
# end
```

Harness: build & run; assert exit; assert stderr empty; assert stdout; compute SHA-256 of the PNG file at `# png:` (relative to process CWD used by the harness — fixtures use a basename like `out.png` in the temp run dir); compare to `# sha256:`. Optionally also byte-compare to sibling `NNN_name.golden.png` when present.

`run_approx` from M2 remains available if needed; M3 PNG goldens use exact hashes instead.

---

## 18. Test case index

Fixtures: `spec/tests/M3/`. Goldens: `*.golden.png`. Reference: `tests/M3/_ref/`.

| ID | File | Kind | Focus |
|----|------|------|-------|
| 001 | `001_clear_color.fm` | run_png | background clear → PNG |
| 002 | `002_unlit_cube.fm` | run_png | MeshBasic red cube |
| 003 | `003_rotating_cube.fm` | run_png | PLAN rotating-cube smoke |
| 004 | `004_lit_standard_cube.fm` | run_png | MeshStandard + lights |
| 005 | `005_unlit_plane.fm` | run_png | PlaneGeometry |
| 006 | `006_scene_add_count.fm` | run | add / childCount |
| 007 | `007_scene_remove.fm` | run | remove |
| 008 | `008_hierarchy_cycle_trap.fm` | runtime_trap | 106 |
| 009 | `009_position_mutate.fm` | run | mesh.position.x mutate |
| 010 | `010_position_copy_gotcha.fm` | run | §3.7 copy |
| 011 | `011_euler_quat_sync.fm` | run_approx | rotation→quaternion |
| 012 | `012_quat_euler_sync.fm` | run_approx | quaternion→rotation |
| 013 | `013_camera_lookat.fm` | run | lookAt changes orientation |
| 014 | `014_basic_ignores_lights.fm` | run_png | Basic==unlit despite lights |
| 015 | `015_shared_material.fm` | run | two meshes one material |
| 016 | `016_shared_geometry.fm` | run | two meshes one geometry |
| 017 | `017_dispose_geometry_trap.fm` | runtime_trap | 105 |
| 018 | `018_dispose_renderer_trap.fm` | runtime_trap | 105 |
| 019 | `019_invalid_size_trap.fm` | runtime_trap | 107 |
| 020 | `020_addAt_oob_trap.fm` | runtime_trap | 102 |
| 021 | `021_import_scene_module.fm` | run | farmos:scene import |
| 022 | `022_literal_float_ctor.fm` | run | §4A int lit in scene ctors |
| 023 | `023_ambient_only_print.fm` | run | construct lights / colors |
| 024 | `024_point_light_construct.fm` | run | PointLight defaults |
| 025 | `025_sphere_construct.fm` | run | SphereGeometry construct |
| 026 | `026_no_background_black.fm` | run_png | hasBackground false → black |
| 027 | `027_visible_false_skip.fm` | run_png | invisible mesh not drawn |
| 028 | `028_matrix_compose_smoke.fm` | run | updateMatrix elements |
| 029 | `029_bad_geometry_type.fm` | compile_error | E0408/E0701 |
| 030 | `030_unknown_scene_export.fm` | compile_error | E0305 |
| 031 | `031_child_get.fm` | run | getChild |
| 032 | `032_renderer_setSize_render_api.fm` | run | API smoke without PNG assert |

**Fixture count:** **32** (≥ 25 required).

PNG fixtures 001–005, 014, 026, 027 use goldens / hashes from `_ref`.

---

## 19. Open questions

**None.** All M3 design questions are resolved (see §20).

---

## 20. Resolved decisions

| ID | Decision |
|----|----------|
| OQ-M3-01 | Soft stripped rotating-cube demo PE ≤ **96 KiB** on Windows x64 (AC-M3-10). PM 2026-09-26 chose A. |
| OQ-M3-02 | Module path `farmos:scene`; does **not** re-export math; user imports both. |
| OQ-M3-03 | Scene graph types are **classes** (reference); math fields inline values; §3.7 documented in Appendix A. |
| OQ-M3-04 | Hierarchy: `add` / `remove` / `addAt` / `childCount` / `getChild`; cycle → 106; DFS pre-order. |
| OQ-M3-05 | Euler↔quaternion bidirectional sync mirroring Three.js. |
| OQ-M3-06 | Deterministic CPU software rasterizer; MeshStandardMaterial = Lambert+Blinn approx (§8.2.1); full PBR deferred to M4 without API break. |
| OQ-M3-07 | PNG API: `setSize` + `render` + `savePNG`; canonical PNG writer §11.3; image origin top-left Y-down. |
| OQ-M3-08 | PerspectiveCamera fov in **degrees**; `lookAt` float triple or `Vector3`. |
| OQ-M3-09 | Lights: Ambient / Directional / Point with Three.js-ish defaults; Basic ignores lights. |
| OQ-M3-10 | Background: `hasBackground` + `Color` (no null). |
| OQ-M3-11 | **OQ-M1-17 resolved:** program-lifetime arena retained; `dispose()` on geometries, materials, Renderer frees native buffers; use-after-dispose → 105. |
| OQ-M3-12 | Diagnostics E07xx; traps 105–108. |
| OQ-M3-13 | DirectionalLight default points local -Z; incident Ldir = -worldDirection. |
| OQ-M3-14 | PointLight attenuation = inverse-square with optional hard `distance` cutoff. |

---

## 21. Amendments relative to M1/M2

| # | Amendment | Notes |
|---|-----------|-------|
| 1 | Built-in module `farmos:scene` | Parallel to `farmos:math` |
| 2 | Stdlib `dispose()` + trap 105 | Resolves OQ-M1-17 |
| 3 | Runtime traps 106–108 | Hierarchy / size / PNG I/O |
| 4 | Diagnostics E0701–E0704 | Scene-specific |
| 5 | Harness kind `run_png` | tests/M3/README.md |
| 6 | Stdlib Scene “extends” Object3D | Stdlib-only; user `extends` still forbidden |

M1-core.md / M2-math.md text are not edited by this milestone; this document carries the amendments.

---

## Appendix A — Three.js rotating-cube port (non-normative)

Near-identical structure to the classic Three.js example. Notes:

1. **§4A:** integer literals `75`, `1`, `4` in float contexts are legal under M2+.
2. **§3.7 copy gotcha:** use `cube.rotation.x = …` (field path), not `let r = cube.rotation; r.x = …` without writing back.
3. No browser animation loop — set a fixed pose (or a fixed `for` step) then `render` + `savePNG`.

```
import { Scene, PerspectiveCamera, BoxGeometry, MeshBasicMaterial,
         Mesh, Renderer } from "farmos:scene";
import { Color } from "farmos:math";

function main(): int {
  const scene: Scene = new Scene();
  scene.setBackground(new Color(0x111111));

  const camera: PerspectiveCamera = new PerspectiveCamera(75, 1, 0.1, 1000);
  camera.position.z = 4;

  const geometry: BoxGeometry = new BoxGeometry(1, 1, 1);
  const material: MeshBasicMaterial = new MeshBasicMaterial(0x00ff88);
  const cube: Mesh = new Mesh(geometry, material);
  scene.add(cube);

  // Fixed "rotation frame" (ports that loop would assign each frame)
  cube.rotation.x = 0.5;
  cube.rotation.y = 0.8;

  const renderer: Renderer = new Renderer(256, 256);
  renderer.render(scene, camera);
  renderer.savePNG("out.png");

  // Optional: free native buffers (arena headers remain until exit)
  geometry.dispose();
  material.dispose();
  renderer.dispose();
  return 0;
}
```

**Wrong (copy gotcha):**

```
let r = cube.rotation;   // Euler value COPY
r.x = 0.5;               // does not update cube
```

**Right:**

```
cube.rotation.x = 0.5;
cube.rotation.y = 0.8;
```

---

## Appendix B — Golden hash manifest (informative)

From `spec/tests/M3/_ref/goldens_sha256.txt` (regenerate with `python3 raster_ref.py`):

```
001_clear_color.golden.png            102c63db3b4bb94afc5c1a5f7b7675c9cd28eacb14e828f13c610c1712577ad7
002_unlit_cube.golden.png             667223dfd9984713ee4ddadf2a41fa448efe8be267c6c5a88b1783a8ae1702d1
003_rotating_cube.golden.png          f04f4678f68a9b386c31987ff34661480f6ab24406edf190246893a2fe894078
004_lit_standard_cube.golden.png      3c31a3e03c51f88d8a90d2ea3252714e017ca08af09172dcc4bba4c66e3ebc12
005_unlit_plane.golden.png            3c5b5b721ef304448dd7b4d33ef59a3687a15480c45c52470a748b70c60af7e3
014_basic_ignores_lights.golden.png   667223dfd9984713ee4ddadf2a41fa448efe8be267c6c5a88b1783a8ae1702d1
026_no_background_black.golden.png    7bbd50eb08c7f094b2a74305029107e161232c52585a3216a3b7b1adaa7eca5a
027_visible_false_skip.golden.png     0403ddcaed23feeb7e0d76bc4a0be12b46ba74f05b28c7d80fcc41c1e0d1baa4
```

---

## Document history

- 2026-09-26: INITIAL FINAL-candidate — M3 scene API, rasterizer, dispose/OQ-M1-17, fixtures 001–032, `_ref` goldens.
- 2026-09-26: FINAL — PM accepted OQ-M3-01 option A (≤ 96 KiB); open questions cleared.
