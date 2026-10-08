# Farmos M4 — Ray Tracing Specification

**Status:** FINAL (PM 2026-10-08: OQ-M4-01 .. OQ-M4-09 all option A)  
**Milestone:** M4 Ray tracing, reflection, materials  
**Depends on:** `spec/M1-core.md` (FINAL), `spec/M2-math.md` (FINAL), `spec/M3-scene.md` (FINAL), `spec/M3.5-concurrency.md` (FINAL)  
**Normative keywords:** MUST / SHOULD / MAY per RFC 2119.  
**Date:** 2026-10-08

This document specifies the Farmos M4 CPU path tracer: API additions on the M3 `Renderer`, area lights, transmission / emissive / albedo textures, a two-level BVH, a deterministic forward integrator, internal tiled threading, and progressive accumulation. It does **not** specify compiler, runtime, or stdlib implementation code.

Open questions: none. PM accepted option A for OQ-M4-01 .. OQ-M4-09 on 2026-10-08. Those decisions are recorded in §19. The body below is that decision, not a draft.

Related: `/workspace/farmos/PLAN.md` (M4 section; this file does not edit it), `spec/M3-scene.md`, `spec/M3.5-concurrency.md`, `spec/tests/M4/README.md`.

**Numeric oracle:** `spec/tests/M4/_ref/path_ref.py` is normative for golden PNGs the way `spec/tests/M3/_ref/raster_ref.py` is for M3, **including expression order**. The reference intersects triangles by brute force. The product MUST still build the BVH in §9. For every fixture scene the closest-hit rule in §10.2 makes the two algorithms return the same hit.

**Target platform (PLAN.md):** Windows x64 only. Size and memory ACs use a stripped Windows x64 PE and that process's peak working set.

---

## 1. Scope and non-goals

### 1.1 In scope (M4 MUST implement, under option A)

- `Renderer.renderPath(scene, camera)` — deterministic forward path tracer (§10). `Renderer.render` stays the M3 rasterizer (§2).
- `RectAreaLight` (§6). `MeshStandardMaterial` fields `transmission`, `ior`, `emissive`, `emissiveIntensity`, and an albedo `Texture` (§5, §7).
- Two-level median-split BVH (§9).
- Shadows, perfect-mirror reflection, glass refraction, direct Cook-Torrance for rough surfaces, emissive surfaces, one albedo texture, internal multi-threaded tiles, progressive sample accumulation.
- Traps 110–113 (§13). Exit 109 stays unused (M3.5 reserved it).
- Fixtures under `spec/tests/M4/` (23). Hero images are small (§17). 800×600 is a memory check, not a golden.

### 1.2 Explicit non-goals

| Not in M4 | Note |
|-----------|------|
| Changing M3 `render()` pixels | §2. M3 goldens stay byte-identical |
| sRGB, gamma, tonemap, denoise | §12 output is clamped linear |
| Environment maps, IBL, clearcoat, anisotropy, normal maps, DOF | |
| Multiscatter GGX compensation | single-scatter only |
| Importance-sampled emissive meshes | emissive is hit only when a ray strikes it |
| Nested dielectric stacks / colored shadows | outside IOR is always 1; visibility is binary |
| Russian roulette | paths run to `maxBounces` |
| Indirect GGX rays on rough surfaces | rough indirect is one cosine-hemisphere diffuse ray |
| User `parallel` as the rendering API | internal tile pool only (OQ-M4-04) |
| SAH BVH | median split (OQ-M4-09) |
| New compile error codes | no E09xx unless a later revision adds a real static error and a fixture |
| GPU / window | unchanged from PLAN |

### 1.3 Design constraints

- PLAN M4: Cornell box and a glass-sphere + mirror-sphere demo render correctly; the demo binary is ≤ 300 KiB stripped; peak RAM for one 800×600 path-traced frame is ≤ 64 MiB.
- Hello world that never names a path-tracing entry point stays ≤ 20 KiB. The tracer is a separate link unit (§15).
- Pixel values MUST NOT depend on `FARMOS_THREADS`, tile scheduling, or float-add order across threads (§11, §12).
- M2 math values stay inline. Scene nodes stay classes. `obj.position.x = …` mutates the object (M3 §3.2). Integer literals in float contexts remain legal (M2 §4A).

---

## 2. What M3 `render` still does

`Renderer.render(scene, camera)` MUST remain the M3 software rasterizer (M3 §8.2.1, §11, §12). A program that only calls `render` MUST produce PNGs **byte-identical** to the published M3 goldens.

`render` MUST ignore, and MUST NOT read for shading:

- `transmission`, `ior`, `emissive`, `emissiveIntensity`, `Texture` / `setMap`
- every `RectAreaLight` (it contributes nothing to the rasterizer)

`render` MUST NOT read or write the path-tracer accumulator (§12). `renderPath` MUST NOT write the raster depth buffer and MUST NOT run the rasterizer.

`savePNG` writes the **display buffer** left by the most recent `render` or `renderPath` (§4.4), using the M3 §11.3 canonical PNG. Quantization of path-traced pixels uses the same byte rule: `floor(c * 255.0 + 0.5)` after clamping the channel to [0, 1].

---

## 3. Module exports

`farmos:scene` MUST export every M3 name plus:

`RectAreaLight`, `Texture`.

No re-export of `farmos:math`. Unknown names still `E0304`. Ordinary type mismatches still use M1 `E04xx` / `E05xx`. M4 adds **no** diagnostic code (§14).

New types are classes (reference semantics), same as other scene nodes (M3 §3.1).

---

## 4. `Renderer` additions

Existing M3 methods stay. Under OQ-M4-01 option A the path tracer is **not** a second class.

```
setSamples(n: int): void          // default 1
setMaxBounces(n: int): void       // default 4
resetAccumulation(): void
renderPath(scene: Scene, camera: PerspectiveCamera): void
```

### 4.1 Samples and bounces

- `setSamples` stores `n` and does **not** trap. `n < 1` traps at `renderPath` (§13, exit 112).
- `setMaxBounces` stores `n` and does **not** trap. `n < 0` traps at `renderPath` (exit 113).
- Defaults before any setter: samples = 1, maxBounces = 4.
- One `renderPath` call adds exactly `samples` new samples to every pixel (§12). It does not replace the accumulator unless the count was zero.

### 4.2 `renderPath` order

On `renderPath`:

1. If this renderer is disposed → trap **105** `runtime error: use after dispose`.
2. If `width <= 0` or `height <= 0` → trap **107** `runtime error: invalid renderer size`.
3. If `samples < 1` → trap **112** `runtime error: invalid sample count`.
4. If `maxBounces < 0` → trap **113** `runtime error: invalid bounce count`.
5. `scene.updateMatrixWorld(true)`; update the camera world matrix, `matrixWorldInverse`, and `projectionMatrix` exactly as `render` does (M3 §6.4, §11.2). The ray formula in §8 does **not** read projection-matrix elements; it uses `fov`, `aspect`, and `matrixWorld`. Calling `updateProjectionMatrix` keeps those fields observable.
6. Walk visible meshes (M3 pre-order, skip a subtree whose node has `visible == false`):
   - disposed geometry or material used by that mesh → trap **105**;
   - if the material is `MeshStandardMaterial`, resolve metal / transmission as in §5.2; if the resolved transmission is `> 0` and `ior < 1` → trap **111** `runtime error: invalid ior`.
7. Rebuild the TLAS (§9), trace, add into the accumulator in ascending sample-index order (§12), then quantize the mean into the display buffer.

Steps 1–4 MUST NOT modify the accumulator or the display buffer. A trap at step 6 MUST leave the accumulator unchanged (matrices may already have been updated in step 5).

### 4.3 `resetAccumulation`

Sets every accumulator channel to `0` and the sample count to `0`. Does not by itself rewrite the display buffer. The next `renderPath` starts again at global sample index 0 (§8.2, §12).

### 4.4 `setSize` and `savePNG`

`setSize` keeps the M3 size rules (trap 107 if `width <= 0` or `height <= 0`; at least 2048×2048 MUST be supported). It also:

- allocates the display RGB8 buffer, the depth buffer (rasterizer only), and a float64 RGB accumulator of `width * height * 3`;
- zeroes the accumulator and the count;
- clears the display buffer to `(0, 0, 0)`.

`savePNG` is unchanged except that the display buffer may have been produced by `renderPath`. I/O failure remains trap **108**. `savePNG` inside a **user** `parallel` task remains **E0805** (M3.5 §7.5). The tracer's own threads are not user tasks (§11).

---

## 5. Material extensions

### 5.1 Fields and setters

`MeshStandardMaterial` gains, in addition to M3 `color`, `roughness`, `metalness`:

| Field | Default | Setter |
|-------|---------|--------|
| `transmission` | `0` | `setTransmission(v: float): void` |
| `ior` | `1.5` | `setIor(v: float): void` |
| `emissive` | black `(0,0,0)` | `setEmissive(c: Color): void` and `setEmissive(hex: int): void` |
| `emissiveIntensity` | `1` | `setEmissiveIntensity(v: float): void` |
| map | none | `setMap(tex: Texture): void` |

Values outside [0, 1] MAY be stored. Shading clamps `roughness`, `metalness`, and `transmission` to [0, 1]. `ior` is not clamped; it is checked in §4.2 step 6.

`MeshBasicMaterial` is unchanged: no transmission, no emissive, no map. In `renderPath` a basic hit returns `color` and stops the path (§10.4).

M3 constructors and `set` / `setRoughness` / `setMetalness` are unchanged.

### 5.2 Resolved parameters

At a standard hit, with `albedo` from §7.3:

```
metal = clamp(metalness, 0, 1)
rough = clamp(roughness, 0, 1)
trans = clamp(transmission, 0, 1)
if metal > 0: trans = 0          // metal wins; transmission is ignored
```

`rough == 0` is a **delta** BSDF (§10.6). `rough > 0` is the rough BSDF (§10.5, §10.7). There is no roughness epsilon (OQ-M4-08 A).

`F0` per channel: `F0 = 0.04 * (1 - metal) + albedo * metal`.

Emission (not multiplied by albedo): `Le_emit = emissive * emissiveIntensity` per channel. It is added when a ray hits the surface. Emissive meshes are **not** sampled as lights.

---

## 6. `RectAreaLight`

An Object3D-like node (transforms, `visible`, parenting), same contribution rule as other lights: only if reachable from the scene root and `visible == true` along the path.

```
new RectAreaLight()                                      // white, intensity 1, width 1, height 1
new RectAreaLight(hex: int, intensity: float)
new RectAreaLight(hex: int, intensity: float, width: float, height: float)
new RectAreaLight(color: Color, intensity: float, width: float, height: float)
```

Fields: `color: Color`, `intensity: float`, `width: float`, `height: float`, plus Object3D transforms.

The rectangle lies in the **local XY** plane, centered on the origin, extents `width` by `height`, **before** the node's scale. `matrixWorld` (including scale) transforms sample points. The emitting direction is local **−Z**, the same facing convention as `DirectionalLight` (M3 §10.2). Radiance (not power) is `Le = color * intensity` per channel.

If `width * height <= 0` the light contributes nothing (no trap, no division by zero).

`render` ignores the light. `renderPath` samples it in §10.5.

### 6.1 `lookAt` when up is parallel to the view axis

M3 §4.7 did not specify `lookAt` when `cross((0,1,0), z)` vanishes (a light aimed straight up or down). M4 makes the following rule normative for **every** `Object3D.lookAt`, matching `raster_ref.py` `Camera.update` and `path_ref.py` `look_at_basis`:

```
z = normalize(eye - target)          // local +Z; zero vector → (0,0,1)
x = cross((0,1,0), z)
if length(x) == 0: x = (1,0,0)
else: x = normalize(x)
y = cross(z, x)                      // local +Y
```

Local −Z then points from `eye` toward `target`. A ceiling `RectAreaLight` at `(0, h, 0)` with `lookAt(0, 0, 0)` emits along world −Y and lies in the horizontal plane (local +X = world +X, local +Y = world −Z). This is **not** Three.js's `0.0001` perturbation. M3 raster goldens do not use this pose, so they stay byte-identical. Component writes of `quaternion` MUST NOT renormalize; `Matrix4.compose` uses the stored components (M2 / `raster_ref.py`).

---

## 7. `Texture` and UVs

### 7.1 Load

```
class Texture {
  constructor(path: string);
}
```

The constructor reads `path` as a file (relative to the process working directory; see `spec/tests/M4/README.md`). On success it stores 8-bit RGB samples as `byte/255` floats with **no** sRGB decode.

MUST accept a non-interlaced PNG, bit depth 8, color type 2 (RGB), compression method 0, filter method 0, interlace 0, filter type `None` (0) on every scanline. The fixture file `spec/tests/M4/_ref/albedo.png` is that subset (canonical level-0 writer). A missing, unreadable, or unsupported file traps immediately (not at `renderPath`):

- exit **110**, stderr `runtime error: texture load failed`.

File row 0 is the **top** of the image. Texture coordinate `v = 0` is the **bottom** row (§7.2).

`render` ignores maps. `setMap` replaces any previous map. There is no `null`; there is no `clearMap` in M4.

### 7.2 UVs

| Geometry | UV |
|----------|----|
| `PlaneGeometry` | vertices in raster_ref order: `(−hx,−hy)` → `(0,0)`, `(hx,−hy)` → `(1,0)`, `(hx,hy)` → `(1,1)`, `(−hx,hy)` → `(0,1)` |
| `BoxGeometry` | each face's four corners, in `raster_ref.py` `box_geometry` order, get `(0,0), (1,0), (1,1), (0,1)` |
| `SphereGeometry` | Three.js-style: for `iy = 0 .. heightSegments`, `ix = 0 .. widthSegments`, `u = ix/widthSegments`, `v = iy/heightSegments`, `θ = v·π`, `φ = u·2π`, position `(-r cos φ sin θ, r cos θ, r sin φ sin θ)`, UV `(u, 1−v)`. Indices, for each `iy, ix`: `a = iy*(widthSegments+1)+ix`, `b = a+widthSegments+1`, triangles `(a, b, a+1)` and `(b, b+1, a+1)`. Segment minima stay the M3 clamp (width ≥ 3, height ≥ 2). |

Hit UV is the Möller–Trumbore barycentric combination (§10.2): `w = 1−u−v`, `UV = w·UV0 + u·UV1 + v·UV2`.

### 7.3 Albedo

If the material has no map, `albedo = color`. If it has a map, per channel `albedo = color * bilinear(tex, uv)`.

Bilinear, clamp-to-edge (not wrap):

```
u, v = clamp(uv, 0, 1)
x = u * (W - 1)
y = (1 - v) * (H - 1)          // v = 0 → bottom row of the file
x0 = floor(x);  y0 = floor(y)
if x0 >= W-1: x0 = W-1; tx = 0; else tx = x - x0
if y0 >= H-1: y0 = H-1; ty = 0; else ty = y - y0
x1 = min(x0+1, W-1);  y1 = min(y0+1, H-1)
sample = lerp(lerp(c00, c10, tx), lerp(c01, c11, tx), ty)
```

`W == 1` or `H == 1` is well-defined (`x` or `y` is 0).

---

## 8. Camera rays and the sample sequence

### 8.1 Pinhole

`PerspectiveCamera`: fov in degrees, looks down local −Z, Y-up (M3 §6, §11.4). `near` / `far` do not move the pinhole origin.

For integer pixel `(x, y)` (origin top-left, `x` right, `y` down) and global sample index `s`:

```
(jx, jy) = jitter(s)                         // §8.2, both in [0, 1]
px = x + jx;  py = y + jy
tanHalf = tan((π/180) * 0.5 * fov)           // π is the float64 3.141592653589793
ndc_x = (px / width) * 2 - 1
ndc_y = 1 - (py / height) * 2                // inverse of M3 §11.4
cx = ndc_x * camera.aspect * tanHalf
cy = ndc_y * tanHalf
```

`camera.aspect` is the camera field, **not** `width/height`, unless the program set them equal (every M4 image fixture does). Direction in camera space is `(cx, cy, -1)`, transformed by the camera's `matrixWorld` linear part (the `lookAt` basis of §6.1) and normalized. Origin is the camera world position.

Sample index `s = 0` goes through the pixel center (`jx = jy = 0.5`).

### 8.2 M4 Hammersley jitter

Classical finite Hammersley `(i/N, Φ₂(i))` depends on a total count `N`. Progressive accumulation does not know a future `N`, and the sequence MUST be a pure function of `(pixelX, pixelY, sampleIndex)` alone (pixel position does not enter the jitter; it enters only the RNG in §8.3). Therefore:

```
jitter(0) = (0.5, 0.5)
jitter(s) = (Φ_base(s, 2), Φ_base(s, 3))    for s ≥ 1
```

`Φ_base` is the radical inverse: start `inv = 0`, `f = 1/base`; while `n > 0`, add `(n mod base) * f`, integer-divide `n` by `base`, divide `f` by `base`.

`Φ₂(1) = 0.5`, `Φ₃(1) = 1/3`. This is the sequence fixtures hash. Implementations MUST NOT substitute `(s/N, Φ₂(s))`.

Global index: if `count` samples are already in the accumulator, a call with `setSamples(n)` uses indices `count, count+1, …, count+n−1`. `resetAccumulation` and `setSize` put `count` back to 0.

### 8.3 Dimension hash

BSDF and area-light samples use this uint32 mix (C `uint32_t` wrap; Python `& 0xFFFFFFFF`). Inputs are truncated to uint32.

```
mix(a, b):
  x = a * 1664525 + b + 1013904223
  x ^= x >> 16
  x *= 0x7feb352d
  x ^= x >> 15
  x *= 0x846ca68b
  x ^= x >> 16
  return x          // all ops mod 2^32

rand01(px, py, s, dim) = (mix(mix(mix(px, py), s), dim) + 0.5) / 4294967296
```

At path depth `d` (0 = camera hit) the base dimension is `d * 16`:

| Use | Dimensions |
|-----|------------|
| Fresnel coin-flip **or** cosine-hemisphere `(u1, u2)` | `base+0`, `base+1` |
| k-th visible `RectAreaLight` in pre-order, k starting at 0 | `base+2+2k`, `base+3+2k` |

Fixtures use at most one rect light (`k = 0`), so dimensions stay inside the stride of 16.

---

## 9. BVH

The product MUST accelerate `renderPath` intersections with a two-level BVH. No SAH (OQ-M4-09 A). The reference tracer does not build one; it scans triangles in the order below and keeps the same hit a correct BVH must return.

### 9.1 BLAS

One BLAS per geometry object, in **that geometry's local space** (shared by every mesh that references the geometry).

Primitives are triangles. Centroid = mean of the three local vertices. Build:

1. If the node has ≤ 4 triangles, it is a leaf.
2. Otherwise choose the longest axis of the centroid AABB. Stable-sort triangles by that centroid component (equal keys keep the earlier triangle index).
3. `mid = n // 2` (integer division). Left = `[0, mid)`, right = `[mid, n)`.
4. If every centroid on that axis is equal, or `mid == 0` or `mid == n`, make a leaf even if it holds more than 4 triangles (the build MUST terminate).

Degenerate triangles (cross product length `< 1e-15` after the world transform) are not intersected.

### 9.2 TLAS

Rebuilt at the **start of every** `renderPath`, after step 5–6 of §4.2 succeed. One instance per visible mesh. The instance AABB is the world-space bounds of the geometry's local AABB transformed by that mesh's `matrixWorld` (transform the 8 corners). The same median-split rules apply, with max 4 instances per leaf.

### 9.3 Hit identity

A conforming BVH returns the **same** closest hit as scanning visible meshes in pre-order and triangles in geometry index order, keeping a hit only when its `t` is **strictly smaller** than the current best (§10.2). Ties keep the earlier `(meshIndex, triangleIndex)`. Shadow rays are any-hit; which occluder is found does not matter.

Fixture `012_multi_mesh` (4 boxes + an 8×4 sphere) is the multi-BLAS image. `005` and `009` add sphere BLASes. A BVH that drops triangles fails those goldens. Matching the golden is necessary. The build rules above are still required: a linear scan that happens to match the PNG does not satisfy this section (structural check, AC-M4-14).

---

## 10. Integrator

Deterministic forward path tracer. No Russian roulette. At most **one** indirect ray per hit. Direct lighting is next-event estimation. Float arithmetic is IEEE-754 binary64. `π` in the formulas is `3.141592653589793`.

Constants:

```
RAY_EPS  = 1e-4
T_MIN    = 1e-6
DET_EPS  = 1e-12
NDOT_EPS = 1e-8
```

### 10.1 Path

```
throughput = (1,1,1);  L = (0,0,0);  ambientDone = false
ray = camera ray (§8)
for depth = 0 .. maxBounces inclusive:
  hit = closest(ray) within t > T_MIN
  if miss:
    L += throughput * background          // scene clear color, every depth
    break
  if basic:
    L += throughput * material.color
    break
  shade standard (§10.3 .. §10.7)
  if depth == maxBounces: break
  spawn one indirect ray or break if throughput is (0,0,0)
return L
```

`background` is the M3 clear color: `background` if `hasBackground`, else `(0,0,0)`. Misses at every depth return it, so a mirror shows the background.

`depth` 0 is the camera hit. `maxBounces == 0` evaluates emission and direct light and does not spawn. `maxBounces == 4` allows four continuations.

### 10.2 Intersection

Double-sided. No backface cull. Möller–Trumbore, **do not** reject `det < 0`:

```
e1 = v1 - v0;  e2 = v2 - v0
pvec = cross(dir, e2);  det = dot(e1, pvec)
if -DET_EPS < det < DET_EPS: miss
inv = 1/det
tvec = origin - v0
u = dot(tvec, pvec) * inv
if u < 0 or u > 1: miss
qvec = cross(tvec, e1)
v = dot(dir, qvec) * inv
if v < 0 or u+v > 1: miss
t = dot(e2, qvec) * inv
if t <= T_MIN: miss
```

World positions are `matrixWorld` times the local vertex (affine; the M2 / `raster_ref.py` compose). Geometric normal `Ng = normalize(cross(v1−v0, v2−v0))` in world space, from that winding (M3 boxes and planes point outward). Hit position is the barycentric point `w·v0 + u·v1 + v·v2` with `w = 1−u−v`, not `origin + t·dir`.

If `dot(Ng, dir) > 0` the ray hit the back face: `N = −Ng`, `entering = false`. Otherwise `N = Ng`, `entering = true`. `N` faces the incoming ray. Shading uses `N`, **not** interpolated vertex normals.

### 10.3 Emission and view

`view = −dir` (unit). `NdotV = max(0, dot(N, view))`.

`L += throughput * (emissive * emissiveIntensity)`.

### 10.4 `MeshBasicMaterial`

`L += throughput * color`. No lights, no ambient, no emission field, no texture, no bounce.

### 10.5 Direct lighting (rough surfaces only)

Skipped entirely when `rough == 0` (delta lobes are not next-event sampled). Shadow origin for every light: `hit + N * RAY_EPS`.

A shadow ray is blocked when **any** triangle, opaque or transmissive, is hit with `T_MIN < t < tMax`. Binary visibility. No colored shadows.

**DirectionalLight.** Travel direction `T` is the world unit vector of local `(0,0,−1)` (M3 §10.2). Incident `Ldir = normalize(−T)`. `Le = color * intensity`. `tMax = 1e30`. No distance attenuation.

**PointLight.** World position is the translation of `matrixWorld`. `toL = lightPos − hit`, `dist = length(toL)`. If `dist == 0`, skip. `Ldir = toL/dist`. Decay default 2:

```
atten = 1 / max(dist * dist, 1e-6)          // decay == 2, product not pow
```

If `decay != 2`: `atten = 1 / max(pow(dist, decay), 1e-6)`. If `distance > 0` and `dist >= distance`, `atten = 0` (hard cutoff, M3 §10.3). `tMax = dist − RAY_EPS`.

**RectAreaLight.** One uniform sample per light per sample index:

```
u = rand01(..., base+2+2k);  v = rand01(..., base+3+2k)
local = ((u − 0.5) * width, (v − 0.5) * height, 0)
samplePos = matrixWorld * local
Lvec = samplePos − hit;  dist2 = dot(Lvec, Lvec);  dist = sqrt(dist2)
Ldir = Lvec / dist
Nw = normalize(matrixWorld direction of local (0,0,−1))
cosLight = dot(Nw, −Ldir)
```

If `dist == 0` or `cosLight <= 0`, contribution is 0 (sample faces away from the emitting side). Otherwise the estimator scale is `cosLight * area / dist2` with `area = width * height` and pdf `1/area` already inverted. `Le = color * intensity`. `tMax = dist − RAY_EPS`.

**BRDF times light**, for a light that survived the tests above, with `scale` = 1 (directional), `atten` (point), or the area scale, and `vis` = 0 or 1:

```
NdotL = dot(N, Ldir)
if vis == 0 or scale == 0 or NdotL <= 0: continue
H = normalize(view + Ldir)
NdotH = dot(N, H);  VdotH = max(0, dot(view, H))
F = schlick(F0, VdotH)
Kd = (1 − F) * (1 − metal) * (1 − trans) * albedo / π     // per channel
L += throughput * Kd * NdotL * Le * scale

if rough > 0 and NdotV > NDOT_EPS and NdotH > 0:
  alpha = rough * rough;  a2 = alpha * alpha
  D = a2 / (π * (NdotH² * (a2 − 1) + 1)²)
  G = G1(NdotV, alpha) * G1(NdotL, alpha)
  // NdotL in the microfacet denominator cancels the cosine
  spec = D * G * F / (4 * NdotV) * Le * scale
  L += throughput * spec
```

`schlick(F0, cos)`: `c = 1 − clamp(cos,0,1)`, `c5 = c²·c²·c` (multiplies, not `pow`), `F = F0 + (1−F0)*c5` per channel.

Smith G1 (separable, not height-correlated):

```
if cos <= NDOT_EPS: G1 = 0
else:
  tan2 = (1 − cos²) / cos²
  G1 = 2 / (1 + sqrt(1 + alpha² * tan2))
```

No multiscatter compensation. Diffuse and specular both use this same `F` at `VdotH`.

### 10.6 Ambient (first non-delta hit only)

If `rough > 0` and this path has not yet applied ambient:

```
for each visible AmbientLight A:
  L += throughput * albedo * (1 − metal) * (1 − trans) * A.color * A.intensity
```

No `1/π`, no Fresnel, no shadow ray. Delta hits do not apply it and do not set the flag, so a mirror → diffuse path applies ambient on the diffuse hit only. A later diffuse hit does not apply it again. Basic hits do not apply it.

### 10.7 Indirect ray

**Delta, `trans > 0` (glass).** Outside medium IOR is 1. No nested stack; `entering` from §10.2 is the whole model. Overlapping glass need not be correct.

```
if entering: eta_i, eta_t = 1, ior
else:        eta_i, eta_t = ior, 1
F = fresnelDielectric(NdotV, eta_i, eta_t)
ξ = rand01(..., base+0)
```

Exact dielectric Fresnel:

```
sin_t = (eta_i/eta_t) * sqrt(max(0, 1 − cos²))
if sin_t >= 1: F = 1                         // TIR
else:
  cos_t = sqrt(max(0, 1 − sin_t²))
  Rpar = (eta_t*cos − eta_i*cos_t) / (eta_t*cos + eta_i*cos_t)
  Rperp = (eta_i*cos − eta_t*cos_t) / (eta_i*cos + eta_t*cos_t)
  F = 0.5 * (Rpar² + Rperp²)
```

`cos` is `NdotV`. If `F >= 1` or `ξ < F`, reflect and leave throughput unchanged (the Fresnel probability cancels). Else refract with `eta = eta_i/eta_t`:

```
sin2_t = eta² * (1 − cos²)
cos_t = sqrt(1 − sin2_t)
wt = normalize(eta * dir + (eta * cos − cos_t) * N)
throughput *= albedo                         // one tint per refraction, not Beer-Lambert
```

If the refract square root would be negative, reflect instead (same as TIR). Reflection origin: `hit + N * RAY_EPS`. Refraction origin: `hit − N * RAY_EPS` (through the interface). Spawned directions are normalized.

**Delta, `trans == 0` (perfect mirror, including metal).** One reflection. `throughput *= schlick(F0, NdotV)`. Origin `hit + N * RAY_EPS`. A white metal (`albedo = 1`) reflects the scene at full Fresnel; a colored metal tints. This is what keeps a mirror sharp at 1 sample (OQ-M4-08 A).

**Rough (`rough > 0`).** One cosine-weighted hemisphere sample, not a GGX sample:

```
u1 = rand01(..., base+0);  u2 = rand01(..., base+1)
r = sqrt(u1);  φ = 2π * u2
local = (r cos φ, r sin φ, sqrt(max(0, 1−u1)))
```

Orthonormal basis around `N` (PBRT coordinate system):

```
if abs(N.x) > abs(N.y):
  inv = 1/sqrt(N.x² + N.z²);  T = (−N.z*inv, 0, N.x*inv)
else:
  inv = 1/sqrt(N.y² + N.z²);  T = (0, N.z*inv, −N.y*inv)
B = cross(N, T)
dir = normalize(T*local.x + B*local.y + N*local.z)
```

`Fview = schlick(F0, NdotV)`. The cosine and the pdf `cos/π` cancel, so

```
throughput *= (1 − Fview) * (1 − metal) * (1 − trans) * albedo     // per channel
```

Origin `hit + N * RAY_EPS`. Metal (`weight` has `1−metal`) and full transmission (`1−trans`) therefore send no diffuse indirect; a rough metal is lit only by the direct specular lobe.

### 10.8 What this is not

No indirect specular on rough surfaces, no emissive next-event, no volume absorption, no normal mapping. Two implementations match by following this section and `path_ref.py`, not by matching a production PBR renderer.

---

## 11. Threading

Under OQ-M4-04 option A, users do **not** parallelize pixels. `renderPath` runs an internal pool.

- Tile size **16×16**. The last column and row of tiles are clipped to the image. Tile index is row-major.
- Worker count `W` is `FARMOS_THREADS`, parsed as in M3.5 §11.2: a decimal integer in `1 .. 256`. **Unset, empty, non-numeric, or out of range means `W = 1` for `renderPath`.** That default is **not** the M3.5 "logical CPU count" default, which still applies only to user `parallel` blocks. A valid `W` is shared: user blocks and `renderPath` both use it.
- The program reads the variable when `renderPath` runs. A program that never calls `renderPath` and never executes `parallel` MUST NOT read it and MUST NOT start threads (hello-world size, §15).
- Worker `i` owns tiles with `tileIndex % W == i`. One worker owns a whole pixel. That worker adds sample indices in **ascending order** into that pixel's float64 sum. No two workers write the same pixel. After the pool joins, the calling thread quantizes the mean into the display buffer.
- Pixel values MUST be identical for every `W`, including 1 and 4 (fixtures 003, 004, 008, 009).
- These threads are invisible to the M3.5 conflict checker. They are not `task` bodies. `savePNG` in a user task is still E0805. `renderPath`'s **external** effects match `render` (M3.5 §8.3): it reads the scene tree, writes the renderer's frame (display buffer **and** accumulator), and writes the same matrices `render` writes. Add accumulator storage to the renderer's `#frame` (or an equivalent renderer-private location) so two user tasks calling `renderPath` on one renderer still conflict. Internal tile writes are not user accesses.

---

## 12. Progressive accumulation

Under OQ-M4-05 option A:

- The accumulator is float64 RGB per pixel, plus an integer `count` (the same count for every pixel).
- `renderPath` adds `samples` new estimates. `count` increases by `samples`.
- The display image is the mean: each channel `sum / count`, then clamp to [0, 1], then `floor(c * 255 + 0.5)`.
- `render` does not read or write the accumulator. A later `savePNG` shows the raster image, not the path mean, until the next `renderPath` rewrites the display buffer from the mean.
- `resetAccumulation` and `setSize` zero the sums and `count`.
- Fixture `011` calls `renderPath` twice with `setSamples(1)` (indices 0 and 1) and hashes the mean. Fixture `022` does the same, then `resetAccumulation`, then one more `renderPath`; the PNG MUST equal the image of sample index 0 alone. Those two PNGs differ (the cosine indirect ray depends on the sample index).

---

## 13. Runtime traps

| Exit | stderr | When |
|------|--------|------|
| 105 | `runtime error: use after dispose` | `renderPath` on a disposed renderer, or a visible mesh whose geometry or material is disposed (M3) |
| 107 | `runtime error: invalid renderer size` | `renderPath` before a successful `setSize`, or `width`/`height` ≤ 0 (M3) |
| 108 | `runtime error: PNG write failed` | `savePNG` I/O (M3) |
| 110 | `runtime error: texture load failed` | `new Texture` cannot read a supported PNG |
| 111 | `runtime error: invalid ior` | `renderPath`, a visible standard material with resolved transmission `> 0` and `ior < 1` |
| 112 | `runtime error: invalid sample count` | `renderPath` with `samples < 1` |
| 113 | `runtime error: invalid bounce count` | `renderPath` with `maxBounces < 0` |

Exit **109** is still reserved (M3.5) and MUST NOT be used. The check order inside `renderPath` is §4.2. `ior == 1` is legal. A non-transmissive material with a bad `ior` does not trap. A metal (`metal > 0` after clamp) does not trap on `ior`, because transmission is forced to 0.

---

## 14. Diagnostics

No new code. M4 does not define E0901 or any E09xx. Wrong types on the new methods use existing M1 codes. There is no `compile_error` fixture. A future code needs a real static rule and a fixture in the same change; do not reserve an unused number here.

`savePNG` / `renderPath` inside user tasks: file output remains E0805 for `savePNG` only. `renderPath` is a memory effect (§11), not `FILE_IO`.

---

## 15. Linking, size, memory

### 15.1 Link units

The BVH, integrator, and tile pool MUST live in a link unit reached only from `renderPath` (and the helpers only it calls). `new Texture` may pull the PNG loader without pulling the tracer. A hello world that names neither MUST stay within the M1/M3 size budget.

### 15.2 Measurement — stripped size (AC-M4-04, AC-M4-05)

Not a ctest image compare.

1. Build `spec/tests/M1/001_hello.fm` with the M4-capable `farmc` (clang or gcc, `-Os`, LTO, `--gc-sections`) and strip. The Windows x64 PE MUST be ≤ **20 KiB** (20480 bytes) and MUST NOT reference the path-tracing object file.
2. Build `spec/tests/M4/009_glass_and_mirror.fm` the same way and strip. The PE MUST be ≤ **300 KiB** (307200 bytes). `008_cornell.fm` is the other hero; either program shape satisfies the "calls `renderPath`" budget. PLAN's "300 KB" is this 300 KiB limit.

### 15.3 Measurement — memory (AC-M4-06)

Not a ctest image compare. Not an 800×600 golden.

Build a program with the **same scene shape as `008_cornell.fm`** (same meshes, light, and camera, not the same pixel count) that calls `setSize(800, 600)`, `setSamples(1)`, `setMaxBounces(4)`, and one `renderPath`. On Windows x64, record `PeakWorkingSetSize` from `GetProcessMemoryInfo` (or a tool that reports that counter) for the process. The peak MUST be ≤ **64 MiB** (67108864 bytes). `savePNG` may be omitted for the measurement.

---

## 16. Acceptance criteria

| ID | Criterion |
|----|-----------|
| AC-M4-01 | M3 `render` goldens byte-identical. `render` ignores transmission, ior, emissive, maps, and `RectAreaLight`. |
| AC-M4-02 | Cornell fixture `008_cornell` matches its golden (open box, rect light, 32×32, 4 samples, 2 bounces). |
| AC-M4-03 | Glass + mirror fixture `009_glass_and_mirror` matches its golden (32×32, 4 samples, 3 bounces). |
| AC-M4-04 | Stripped Windows x64 PE of `009` (or the same program shape) ≤ 300 KiB. Procedure §15.2. |
| AC-M4-05 | Hello world that does not reference path-tracing entry points ≤ 20 KiB; tracer is a separate link unit. |
| AC-M4-06 | Cornell-shaped scene, `setSize(800,600)`, `setSamples(1)`, `setMaxBounces(4)`, one `renderPath`: peak working set ≤ 64 MiB. Procedure §15.3. |
| AC-M4-07 | `FARMOS_THREADS` 1 and 4 (fixtures 003, 004, 008, 009) produce the same PNG. Unset ⇒ 1 worker for `renderPath`. |
| AC-M4-08 | Progressive mean: `011` is the mean of sample indices 0 and 1; `022` after `resetAccumulation` equals sample index 0 only. |
| AC-M4-09 | Delta mirror: `004` (metal 1, roughness 0, 1 sample) shows the red box **only in reflection** (the box is behind the camera). |
| AC-M4-10 | Refraction: `005` glass sphere, transmission 1, roughness 0, ior 1.5, segments 8×4, shows the red box through the sphere against the green wall. |
| AC-M4-11 | Emissive: `006` quad, no lights, matches `emissive * emissiveIntensity` on the lit texels. |
| AC-M4-12 | Hard shadow: `003`, directional, `maxBounces` 0, binary visibility, occluder darkens the floor. |
| AC-M4-13 | Albedo texture: `010`, `color * bilinear`, no sRGB, CWD rule in the M4 README. |
| AC-M4-14 | Product intersections go through the §9 BVH. Images `012`, `005`, `009` match the brute-force oracle; a BVH that drops triangles fails them. |
| AC-M4-15 | Traps: 112 `014`, 113 `015`, 111 `016`, 110 `017`, 105 `018`, 107 `019`. Exit 109 unused. |
| AC-M4-16 | Double-sided: `020`, camera on −Z sees a plane whose geometric normal is +Z. The rasterizer would cull it; `renderPath` must not. |
| AC-M4-17 | `visible == false` drops the mesh and its shadow (`021`). |
| AC-M4-18 | Rough metal vs delta metal differ (`007`). Point-light inverse-square matches §10.5 (`023`). |
| AC-M4-19 | `run_png` SHA-256 uses the M3 canonical PNG. No gamma, no tonemap. |
| AC-M4-20 | Internal tile threads are not user tasks. `savePNG` in a user task remains E0805. `renderPath` from two user tasks on one renderer conflicts (M3.5 §8.3 plus the accumulator). |

AC-M4-04 and AC-M4-06 are eggtooth checks. They are not image fixtures and have no golden PNG.

---

## 17. Test harness

Normative detail: `spec/tests/M4/README.md`.

- CWD is `spec/tests/M4`.
- `run_png` as in M3, plus `# threads:` as in M3.5 on 003, 004, 008, 009.
- Image fixtures are ≤ 32×32 and ≤ 4 samples, `maxBounces` ≤ 4. Full 800×600 is not hashed.
- 23 fixtures: **16** `run_png`, **1** `run`, **6** `runtime_trap`. No `compile_error`.

### 17.1 Index

| Fixture | Kind | What |
|---------|------|------|
| 001_background_only | run_png | 16×16, empty scene, clear color `0x4488ff` |
| 002_unlit_basic | run_png | 24×24, basic cube, directional ignored |
| 003_hard_shadow | run_png | 32×32, 1 sample, 0 bounces, threads 1,4 |
| 004_perfect_mirror | run_png | 32×32, 1 sample, 2 bounces, threads 1,4 |
| 005_glass_sphere | run_png | 32×32, 1 sample, 3 bounces, sphere 8×4 |
| 006_emissive_quad | run_png | 24×24, emission `(0.2,0.4,0.05)*2` |
| 007_metal_vs_rough | run_png | 32×24, delta mirror vs roughness 0.45 metal |
| 008_cornell | run_png | 32×32, 4 samples, 2 bounces, threads 1,4 |
| 009_glass_and_mirror | run_png | 32×32, 4 samples, 3 bounces, threads 1,4 |
| 010_albedo_texture | run_png | 32×32, `_ref/albedo.png` |
| 011_progressive_mean | run_png | 16×16, two `renderPath` calls, mean |
| 012_multi_mesh | run_png | 24×24, 4 boxes + sphere |
| 013_rect_light_fields | run | prints width 3, height 4, intensity 2 |
| 014_trap_samples | runtime_trap | exit 112 |
| 015_trap_bounces | runtime_trap | exit 113 |
| 016_trap_ior | runtime_trap | exit 111 |
| 017_trap_missing_texture | runtime_trap | exit 110 |
| 018_trap_dispose | runtime_trap | exit 105 |
| 019_trap_no_size | runtime_trap | exit 107 |
| 020_double_sided | run_png | 24×24, back face of a plane |
| 021_visible_false | run_png | 24×24, hidden box casts no shadow |
| 022_reset_accumulation | run_png | 16×16, reset then one sample |
| 023_point_light | run_png | 24×24, decay 2, 0 bounces |

Hashes: Appendix C and `spec/tests/M4/_ref/goldens_sha256.txt`.

---

## 18. Open questions

**None.** PM decided OQ-M4-01 .. OQ-M4-09, all option A, on 2026-10-08. See §19.

---

## 19. Resolved decisions

PM 2026-10-08 accepted option A for every OQ below. The integrator rows are the housekeeping those questions did not cover.

| ID | Decision |
|----|----------|
| OQ-M4-01 | `renderPath` on the existing `Renderer`. `render` stays the M3 rasterizer, byte-identical. `savePNG` writes whichever buffer is current. No `PathRenderer`. |
| OQ-M4-02 | `RectAreaLight` (local XY, emits along local −Z). Emissive materials are visible and bounce, not importance-sampled. |
| OQ-M4-03 | No gamma, no tonemap, no sRGB. Clamp to [0,1], quantize with M3 `floor(c*255+0.5)`. |
| OQ-M4-04 | Internal 16×16 tile pool. `FARMOS_THREADS`, unset means 1. Pixels identical for every worker count. User `parallel` is not the render API. |
| OQ-M4-05 | `setSamples` adds samples into a float64 mean. `resetAccumulation` and `setSize` clear it. `render` does not touch it. |
| OQ-M4-06 | `transmission`, `ior`, `emissive`, `emissiveIntensity` on `MeshStandardMaterial`. Metal wins over transmission. Outside IOR is 1. No nested-dielectric stack. |
| OQ-M4-07 | Albedo `Texture` is required. Hero fixtures 008 and 009 do not use one. Fixture 010 does. |
| OQ-M4-08 | Roughness that clamps to 0 is one perfect reflect or refract ray. Roughness > 0 uses GGX in the direct lobe only. |
| OQ-M4-09 | Two-level BVH, median split, max 4 triangles per leaf. No SAH. |
| RD-M4-01 | Forward path tracer: NEE for Ambient (first non-delta hit only, formula in §10.6), Point, Directional, RectArea; then at most one indirect ray. |
| RD-M4-02 | `maxBounces` default 4. `depth` runs `0 .. maxBounces` inclusive. No Russian roulette. |
| RD-M4-03 | Missed rays return the M3 clear color at every depth. |
| RD-M4-04 | Basic materials are unlit and end the path. |
| RD-M4-05 | Rough direct lobe: single-scatter Cook-Torrance, GGX NDF, separable Smith GGX, Schlick at `VdotH`. `F0 = lerp(0.04, albedo, metal)`. `Kd` as §10.5. No multiscatter, clearcoat, anisotropy, normal maps, DOF, denoise, or environment maps. |
| RD-M4-06 | Rough indirect is one cosine hemisphere, throughput `(1−Fview)*(1−metal)*(1−trans)*albedo`. Not a GGX sample. |
| RD-M4-07 | Hits are double-sided. Shading normal is the flipped geometric normal. Shadow and non-refract bounces start at `hit + N*1e-4`. Refraction starts at `hit − N*1e-4`. |
| RD-M4-08 | Point and directional lights keep M3 direction, attenuation, and cutoff. Shadows are binary, including against transmissive triangles. |
| RD-M4-09 | Rect light: `Le = color * intensity`, one uniform point, pdf `1/area`, zero if `cosLight <= 0`. |
| RD-M4-10 | Pinhole from `PerspectiveCamera`. Sample 0 is the pixel center. Later samples are §8.2. |
| RD-M4-11 | Tiles 16×16. The sample stream is a pure function of `(x, y, sampleIndex)`. |
| RD-M4-12 | Albedo `= color * bilinear`. No sRGB. UV tables in §7.2. |
| RD-M4-13 | Trap numbers: 110 texture, 111 ior, 112 samples, 113 bounces. 105 and 107 reused. 109 unused. |
| RD-M4-14 | `path_ref.py` is the numeric oracle, including expression order and CPython `sin`/`cos` for sphere vertices. Brute-force intersection there is equivalent under §9.3, not a product license to skip the BVH. |
| RD-M4-15 | Glass: one delta ray, Fresnel probability with `ξ`, weights cancel; refraction multiplies `albedo` once. Outside IOR is 1. |
| RD-M4-16 | `lookAt` parallel-up case is §6.1. Quaternion component stores are not renormalized. |

---

## 20. Amendments relative to M3 / M3.5

M3-scene.md and M3.5-concurrency.md are not edited.

| # | Amendment |
|---|-----------|
| 1 | `farmos:scene` also exports `RectAreaLight` and `Texture`. |
| 2 | `MeshStandardMaterial` gains the §5 fields. M3 `render` ignores them. |
| 3 | `Renderer` gains `renderPath`, `setSamples`, `setMaxBounces`, `resetAccumulation`, and the accumulator. |
| 4 | `Object3D.lookAt` parallel-up case is specified (§6.1). |
| 5 | Traps 110–113. Exit 109 stays reserved. |
| 6 | `renderPath` reads `FARMOS_THREADS` with invalid/unset ⇒ 1. User `parallel` keeps the M3.5 CPU default when the variable is unset or invalid. |
| 7 | M3.5 §8.3: `renderPath` has `render`'s effects plus the accumulator. Internal threads are not tasks. `savePNG` remains `FILE_IO` / E0805. |
| 8 | M3.5 §11.2 "programs that never execute `parallel` MUST NOT read `FARMOS_THREADS`" does not apply to a program that calls `renderPath`. |
| 9 | Harness: `spec/tests/M4/README.md`, CWD = that directory, `# threads:` on four `run_png` fixtures. |
| 10 | Sphere vertices are now on the golden path (M3 image fixtures avoided them). The formula is §7.2. |

---

## Appendix A — Cornell demo

Same program as `spec/tests/M4/008_cornell.fm`. 32×32, 4 samples, 2 bounces. The 800×600 memory check uses this scene shape with a different `setSize` (§15.3); that run is not hashed.

```
import { Scene, PerspectiveCamera, PlaneGeometry, BoxGeometry,
         MeshStandardMaterial, Mesh, RectAreaLight, Renderer } from "farmos:scene";
import { Color } from "farmos:math";

function main(): int {
  const scene: Scene = new Scene();
  const camera: PerspectiveCamera = new PerspectiveCamera(40, 1, 0.1, 100);
  camera.position.set(0, 1, 3.3);
  camera.lookAt(0, 0.9, 0);
  const white: MeshStandardMaterial = new MeshStandardMaterial(new Color(0.78, 0.78, 0.78));
  white.setRoughness(1);
  const floor: Mesh = new Mesh(new PlaneGeometry(2, 2), white);
  floor.quaternion.x = -0.7071067811865476;
  floor.quaternion.y = 0;
  floor.quaternion.z = 0;
  floor.quaternion.w = 0.7071067811865476;
  scene.add(floor);
  const ceilMat: MeshStandardMaterial = new MeshStandardMaterial(new Color(0.78, 0.78, 0.78));
  ceilMat.setRoughness(1);
  const ceiling: Mesh = new Mesh(new PlaneGeometry(2, 2), ceilMat);
  ceiling.position.y = 2;
  ceiling.quaternion.x = 0.7071067811865476;
  ceiling.quaternion.y = 0;
  ceiling.quaternion.z = 0;
  ceiling.quaternion.w = 0.7071067811865476;
  scene.add(ceiling);
  const backMat: MeshStandardMaterial = new MeshStandardMaterial(new Color(0.78, 0.78, 0.78));
  backMat.setRoughness(1);
  const back: Mesh = new Mesh(new PlaneGeometry(2, 2), backMat);
  back.position.set(0, 1, -1);
  scene.add(back);
  const redMat: MeshStandardMaterial = new MeshStandardMaterial(new Color(0.75, 0.12, 0.12));
  redMat.setRoughness(1);
  const left: Mesh = new Mesh(new PlaneGeometry(2, 2), redMat);
  left.position.set(-1, 1, 0);
  left.quaternion.x = 0;
  left.quaternion.y = 0.7071067811865476;
  left.quaternion.z = 0;
  left.quaternion.w = 0.7071067811865476;
  scene.add(left);
  const greenMat: MeshStandardMaterial = new MeshStandardMaterial(new Color(0.12, 0.55, 0.15));
  greenMat.setRoughness(1);
  const right: Mesh = new Mesh(new PlaneGeometry(2, 2), greenMat);
  right.position.set(1, 1, 0);
  right.quaternion.x = 0;
  right.quaternion.y = -0.7071067811865476;
  right.quaternion.z = 0;
  right.quaternion.w = 0.7071067811865476;
  scene.add(right);
  const shortMat: MeshStandardMaterial = new MeshStandardMaterial(new Color(0.78, 0.78, 0.78));
  shortMat.setRoughness(1);
  const shortBox: Mesh = new Mesh(new BoxGeometry(0.6, 0.7, 0.6), shortMat);
  shortBox.position.set(0.35, 0.35, 0.25);
  scene.add(shortBox);
  const tallMat: MeshStandardMaterial = new MeshStandardMaterial(new Color(0.78, 0.78, 0.78));
  tallMat.setRoughness(1);
  const tallBox: Mesh = new Mesh(new BoxGeometry(0.6, 1.2, 0.6), tallMat);
  tallBox.position.set(-0.35, 0.6, -0.25);
  scene.add(tallBox);
  const light: RectAreaLight = new RectAreaLight(0xffffff, 40, 0.5, 0.5);
  light.position.set(0, 1.98, 0);
  light.lookAt(0, 0, 0);
  scene.add(light);
  const renderer: Renderer = new Renderer(32, 32);
  renderer.setSamples(4);
  renderer.setMaxBounces(2);
  renderer.renderPath(scene, camera);
  renderer.savePNG("out.png");
  return 0;
}
```

---

## Appendix B — Glass sphere and mirror sphere

Same program as `spec/tests/M4/009_glass_and_mirror.fm`. 32×32, 4 samples, 3 bounces. This is the program shape for the 300 KiB stripped-size check (§15.2).

```
import { Scene, PerspectiveCamera, PlaneGeometry, BoxGeometry, SphereGeometry,
         MeshStandardMaterial, MeshBasicMaterial, Mesh, DirectionalLight,
         AmbientLight, Renderer } from "farmos:scene";
import { Color } from "farmos:math";

function main(): int {
  const scene: Scene = new Scene();
  scene.setBackground(new Color(0x181820));
  const camera: PerspectiveCamera = new PerspectiveCamera(42, 1, 0.1, 100);
  camera.position.set(0, 0.7, 2.8);
  camera.lookAt(0, 0.45, 0);
  const floorMat: MeshStandardMaterial = new MeshStandardMaterial(new Color(0.75, 0.75, 0.75));
  floorMat.setRoughness(1);
  const floor: Mesh = new Mesh(new PlaneGeometry(4, 3), floorMat);
  floor.quaternion.x = -0.7071067811865476;
  floor.quaternion.y = 0;
  floor.quaternion.z = 0;
  floor.quaternion.w = 0.7071067811865476;
  scene.add(floor);
  const wallMat: MeshStandardMaterial = new MeshStandardMaterial(new Color(0.25, 0.45, 0.85));
  wallMat.setRoughness(1);
  const wall: Mesh = new Mesh(new PlaneGeometry(4, 2.2), wallMat);
  wall.position.set(0, 1, -1.4);
  scene.add(wall);
  const glassMat: MeshStandardMaterial = new MeshStandardMaterial(new Color(1, 1, 1));
  glassMat.setRoughness(0);
  glassMat.setTransmission(1);
  glassMat.setIor(1.5);
  const glass: Mesh = new Mesh(new SphereGeometry(0.42, 8, 4), glassMat);
  glass.position.set(-0.48, 0.42, 0.05);
  scene.add(glass);
  const mirrorMat: MeshStandardMaterial = new MeshStandardMaterial(new Color(1, 1, 1));
  mirrorMat.setRoughness(0);
  mirrorMat.setMetalness(1);
  const mirror: Mesh = new Mesh(new SphereGeometry(0.42, 8, 4), mirrorMat);
  mirror.position.set(0.52, 0.42, 0.15);
  scene.add(mirror);
  const gem: Mesh = new Mesh(new BoxGeometry(0.28, 0.28, 0.28), new MeshBasicMaterial(0xe0c020));
  gem.position.set(-0.48, 0.42, -0.75);
  scene.add(gem);
  const sun: DirectionalLight = new DirectionalLight(0xffffff, 2.5);
  sun.lookAt(0.15, -1, -0.35);
  scene.add(sun);
  scene.add(new AmbientLight(0xffffff, 0.08));
  const renderer: Renderer = new Renderer(32, 32);
  renderer.setSamples(4);
  renderer.setMaxBounces(3);
  renderer.renderPath(scene, camera);
  renderer.savePNG("out.png");
  return 0;
}
```

---

## Appendix C — Golden hash manifest

From `spec/tests/M4/_ref/goldens_sha256.txt` (`python3 spec/tests/M4/_ref/path_ref.py`). SHA-256 of the canonical PNG.

```
001_background_only.golden.png  e7655a2c9a205f90786991219a3b558e27b2314377a4f5d4a66891b5b25915dd
002_unlit_basic.golden.png  f17bac401b669b2889830cfccc3cee1a86172924e729d8278f51477e609ecdaf
003_hard_shadow.golden.png  e179ea2d08faba40915a625b3fbbb8495ed85d0fb3345f3f92cc8263b8fc0f6c
004_perfect_mirror.golden.png  2af33ba39dae62c5b3df30970794d7d92213f51a2997ef7f82860910638ecd04
005_glass_sphere.golden.png  f4615d08b4ae849e2c2c64320410e8b996d4066cfb12da5cdbf40ea0868d8b24
006_emissive_quad.golden.png  d96a4c770d87b3806d87f4886421736c0f2ae587ad33be5eb7db2690127a2687
007_metal_vs_rough.golden.png  29fee1100619acaa6c26666fdbcdc27d7cdc8ef26f73b12e64ee1f3115c79033
008_cornell.golden.png  3f29eb72e3b47f6108fa455e315a4d6a97228f47fdf1e9dac030ea42a4b9608f
009_glass_and_mirror.golden.png  edf8f93ee8ae044efb16768e9749da5674e6a8c3f91fe59a2da54ad53f39e842
010_albedo_texture.golden.png  02f14f54d35b4682be553e9d86e277e60f18053b76bdc1d0ee2974a6711a2885
011_progressive_mean.golden.png  b7a1bc913c7875519216395bde0da6c45df0f3c0463fed8c8d6533e752c8a6c3
012_multi_mesh.golden.png  471a9d719ca78d905dd33a06d1db6ce0fca43a0fd35e15c73f33af399966f3aa
020_double_sided.golden.png  1d1cbda60b4112540c6f740a84c48816c30466ae8f80d4dc1f9ecaf0f54aa1cb
021_visible_false.golden.png  fb2aba19506aae7470266a9f9c5ab5a08a41700a922cd8cd6136931007f1aa15
022_reset_accumulation.golden.png  0bbf67dab2b18cb0f0ac2668cf292af370535747cc468f8def37f504538a4b9e
023_point_light.golden.png  af06979d167e4b401da21d5b1cde0e585ec0879a1d17009dcb9ef21ca1a4e939
```

---

## Document history

- 2026-10-08: DRAFT. Path tracer on `Renderer.renderPath` (option A throughout). Fixtures 001–023.
- 2026-10-08: FINAL. PM accepted OQ-M4-01 .. OQ-M4-09, all option A. Open questions cleared.
