# Farmos M5 — Physics Specification

**Status:** FINAL (PM 2026-10-09: OQ-M5-01 .. OQ-M5-08 all option A)  
**Milestone:** M5 Physics  
**Depends on:** `spec/M1-core.md` (FINAL), `spec/M2-math.md` (FINAL), `spec/M3-scene.md` (FINAL), `spec/M3.5-concurrency.md` (FINAL), `spec/M4-ray.md` (FINAL)  
**Normative keywords:** MUST / SHOULD / MAY per RFC 2119.  
**Date:** 2026-10-09

This document specifies the Farmos M5 rigid-body physics module: `World`, `RigidBody`, sphere / box / plane colliders, broadphase and narrowphase, fixed-step semi-implicit Euler integration, sequential-impulse contact response, and sync into M3 scene objects. It does **not** specify compiler, runtime, or stdlib implementation code.

Open questions: none. PM accepted option A for OQ-M5-01 .. OQ-M5-08 on 2026-10-09 (including the stacking-on-spheres note). Those decisions are recorded in §18. The body below is that decision, not a draft.

Related: `/workspace/farmos/PLAN.md` (M5 section), `spec/M3-scene.md`, `spec/tests/M5/README.md`.

**Numeric oracle:** `spec/tests/M5/_ref/physics_ref.py` is normative for fixture floating-point outputs the way `path_ref.py` is for M4 goldens, **including expression order**. It is **not** product code.

**Target platform (PLAN.md):** Windows x64 only. Size ACs use a stripped Windows x64 PE.

---

## 1. Scope and non-goals

### 1.1 In scope (M5 MUST implement, under option A)

- Built-in module `farmos:physics` (§3).
- `World` with gravity, fixed timestep, `add` / `remove`, `step`, `dispose` (§4).
- `RigidBody` with Dynamic / Static / Kinematic types, mass, restitution, friction, linear and angular velocity, linear and angular damping, optional `Object3D` link (§5).
- `SphereCollider`, `BoxCollider`, `PlaneCollider` (§6).
- Sweep-and-prune style AABB broadphase with deterministic id tie-break; narrowphase for sphere–sphere, sphere–box, box–box (SAT / AABB for axis-aligned), sphere–plane, box–plane (§7, §8).
- Semi-implicit (symplectic) Euler, one substep per `step()` (§9).
- Sequential impulse contact + friction, fixed 10 iterations, no warm-start; geometric-mean materials (§10).
- After each `step`, sync linked scene objects (§11).
- Traps 114–116; reuse 105 for disposed `World` (§12). Exit 109 stays unused.
- Fixtures under `spec/tests/M5/` (30: 5 `run`, 19 `run_approx`, 6 `runtime_trap`).

### 1.2 Explicit non-goals

| Not in M5 | Note |
|-----------|------|
| Soft bodies, cloth, fluids | |
| Joints / constraints beyond contact | hinge, spring, motor deferred |
| Vehicles, characters | |
| Continuous collision detection (CCD) | discrete contacts only |
| Multithreaded `world.step` | single-threaded step; user `parallel` around one world is a conflict (§13) |
| Calling `render` / `renderPath` from physics | physics does not render |
| Folding M6 backlog (for-loop continue, Windows DCE / `_beginthreadex`, `run_approx` epsilon, M3 leftovers, eggtooth M4 sha notes) | not required by physics |
| Changing M1–M4 normative text | amendments live here (§19) |
| New compile-error codes | reuse M1 `E04xx` / `E05xx`; no E10xx unless a later revision needs one |

### 1.3 Design constraints

- PLAN M5: stacking / bouncing demo stable for 10 s simulated time; deterministic test outputs.
- Hello world that never imports `farmos:physics` stays ≤ 20 KiB. Physics is a separate link unit (§14).
- Same initial state + same number of `step()` calls ⇒ bit-identical body transforms and velocities for the published fixtures (IEEE 754 binary64, no `-ffast-math`).
- M2 math values stay inline. Scene nodes stay classes. `obj.position.x = …` mutates the object (M3 §3.2). Integer literals in float contexts remain legal (M2 §4A).

---

## 2. Relationship to M3 / M4

- Physics does **not** re-export `farmos:scene` or `farmos:math`. User code imports both modules as needed (OQ-M5-01 A).
- Colliders attach to `RigidBody`, not to `Mesh` (OQ-M5-02 A).
- Sync writes body pose into a linked `Object3D` / `Mesh` after each `step` (§11). Scale is never written by physics.
- M3 `render` and M4 `renderPath` are unchanged. Physics never calls them.
- M3.5: two user tasks that both call `step` on the same `World` conflict under M3.5 §8.3 (effectful stdlib on a shared receiver). Document; no new diagnostic code.

---

## 3. Module exports

`farmos:physics` MUST export:

```
World,
RigidBody,
SphereCollider,
BoxCollider,
PlaneCollider,
BODY_DYNAMIC,      // int 0
BODY_STATIC,       // int 1
BODY_KINEMATIC     // int 2
```

Unknown names still `E0304`. Ordinary type mismatches still use M1 `E04xx` / `E05xx`. M5 adds **no** new compile diagnostic code.

`BODY_*` are `int` constants. `World`, `RigidBody`, and collider types are **classes** (reference semantics), same as scene nodes (M3 §3.1).

---

## 4. `World`

```
class World {
  // no public transform fields
}
```

### 4.1 Construction and defaults

`new World()`:

| State | Default |
|-------|---------|
| gravity | `(0, -9.81, 0)` |
| fixed timestep `dt` | `1/60` |
| body list | empty |
| disposed | false |
| next body id | `0` |

### 4.2 API

```
setGravity(v: Vector3): void
setFixedTimeStep(dt: float): void
add(body: RigidBody): void
remove(body: RigidBody): void
bodyCount(): int
step(): void
dispose(): void
```

- `setGravity` copies `v`'s components into the world (M2 copy semantics: the argument is not retained by reference).
- `setFixedTimeStep` **stores** `dt` and does not trap. `dt <= 0` traps at `step` (§12, exit 115).
- `add`: appends `body` if not already in this world; assigns `body.id` to the current next id, then increments next id. Re-adding an already-present body is a no-op (id unchanged). Adding a body that belongs to another live world is a no-op on the other world and moves membership to this world (implementation MUST remove from the previous world first). Bodies keep their id when moved.
- `remove`: removes the first match; does not reuse ids. Unknown body is a no-op. Removed bodies keep their last `id` value.
- `bodyCount`: number of bodies currently in the world.
- `dispose`: frees native solver buffers; marks disposed. Second `dispose` is a no-op. After dispose, `step` / `add` / `remove` / `setGravity` / `setFixedTimeStep` → trap **105**. `bodyCount` on a disposed world MAY return 0 or trap 105; fixtures do not call it after dispose. Reading fields on bodies that were in the world remains legal.

### 4.3 `step` order

On `world.step()`:

1. If disposed → trap **105** `runtime error: use after dispose`.
2. If `dt <= 0` → trap **115** `runtime error: invalid fixed timestep`.
3. For each body in registration order:
   - if `bodyType == BODY_DYNAMIC` and `mass <= 0` → trap **114** `runtime error: invalid mass`;
   - if collider is a `PlaneCollider` and `bodyType == BODY_DYNAMIC` → trap **116** `runtime error: invalid collider`;
   - if `SphereCollider` with `radius <= 0`, or `BoxCollider` with any half-extent `<= 0` → trap **116**.
4. Recompute inverse mass and inverse inertia for every Dynamic body (§5.4).
5. Integrate Dynamic bodies (§9). Static and Kinematic bodies are not integrated (Kinematic pose is whatever the user set).
6. Broadphase + narrowphase → contact list (§7, §8).
7. Sort contacts deterministically (§10.1).
8. Compute restitution bias once per contact from pre-solve relative velocity (§10.2).
9. Sequential impulse, **10** iterations, warm-start off (§10.3).
10. Sync every body that has a linked `Object3D` (§11).

A trap at steps 1–3 MUST leave body state unchanged from before the call (no partial integrate).

---

## 5. `RigidBody`

```
class RigidBody {
  position: Vector3;           // inline fields (M3-style)
  quaternion: Quaternion;      // inline
  linearVelocity: Vector3;     // inline
  angularVelocity: Vector3;    // inline
  id: int;                     // assigned on World.add; default -1 before add
  // bodyType is fixed at construction (getter MAY exist; field MUST be readable)
}
```

### 5.1 Construction

```
new RigidBody(bodyType: int)
```

- `bodyType` MUST be `BODY_DYNAMIC` (0), `BODY_STATIC` (1), or `BODY_KINEMATIC` (2). Any other value is treated as `BODY_DYNAMIC` (no trap).
- Defaults:

| Field | Default |
|-------|---------|
| `position` | `(0,0,0)` |
| `quaternion` | `(0,0,0,1)` |
| `linearVelocity` | `(0,0,0)` |
| `angularVelocity` | `(0,0,0)` |
| mass | `1` (Dynamic); ignored for Static/Kinematic (infinite) |
| restitution | `0` |
| friction | `0.3` |
| linearDamping | `0` |
| angularDamping | `0` |
| collider | none |
| linked object | none |
| `id` | `-1` until `add` |

### 5.2 Setters

```
setMass(m: float): void
setRestitution(e: float): void
setFriction(f: float): void
setLinearDamping(d: float): void
setAngularDamping(d: float): void
setCollider(c: SphereCollider | BoxCollider | PlaneCollider): void
setObject(obj: Object3D): void
clearObject(): void
getBodyType(): int
```

- Setters store values and do **not** trap. Mass / restitution / friction / damping / collider validity are checked at `step` as in §4.3.
- Restitution MAY be stored outside [0, 1]; friction MAY be stored negative. The solver clamps restitution to [0, 1] and friction to `max(0, f)` when building contacts (§10).
- `setObject(obj)`: stores a reference to `obj` and **pulls** once: copy `obj.position` components into `body.position` and `obj.quaternion` components into `body.quaternion` (field assigns; do not replace the body's Vector3/Quaternion structs with a copy-out that orphans later mutations). Then sync Euler on `obj` is not required on pull.
- `clearObject()`: clears the link. Sync becomes a no-op for that body.
- There is no `null`. Absence of a link is internal state.

### 5.3 Body types

| Type | Mass | Integrated | Collision response |
|------|------|------------|--------------------|
| Dynamic | finite `mass > 0` | yes | pushed by impulses |
| Static | infinite | no | never moves; impulses do not apply |
| Kinematic | infinite | no | user sets `position` / `quaternion` each frame; collides and can push Dynamic; not pushed |

Plane colliders are allowed on Static and Kinematic only (§4.3).

### 5.4 Mass properties (Dynamic)

- `invMass = 1 / mass`.
- Sphere radius `r`, mass `m`: inertia `I = (2/5) m r²` on each principal axis; `invI = 1/I` (or 0 if `I == 0`).
- Box half-extents `(hx, hy, hz)`, mass `m`:
  - `Ixx = (1/3) m (hy² + hz²)`
  - `Iyy = (1/3) m (hx² + hz²)`
  - `Izz = (1/3) m (hx² + hy²)`
- World-space inverse inertia: `I_inv_w = R * diag(invI) * Rᵀ` where `R` is the body's quaternion as a rotation matrix. Apply as: `t = Rᵀ v`, scale by `invI`, then `R t`.
- Static / Kinematic: `invMass = 0`, `invI = 0`.
- No collider: `invI = 0` (translation only).

---

## 6. Colliders

Colliders are classes. Local offset from the body origin defaults to `(0,0,0)`. M5 exposes offset only as constructor default zero; a future revision MAY add `setOffset`. Implementations MUST treat offset as zero for all M5 fixtures.

```
new SphereCollider(radius: float)
new BoxCollider(hx: float, hy: float, hz: float)
new BoxCollider(halfExtents: Vector3)    // copies hx,hy,hz from the vector
new PlaneCollider()
```

- **Sphere:** radius as given.
- **Box:** half-extents along local X/Y/Z before body rotation.
- **Plane:** infinite plane in body local space: point on plane = body origin (+ offset), normal = local `+Y` rotated by body quaternion. Static/Kinematic body pose orients the plane.

---

## 7. Broadphase

Under OQ-M5-05 A:

1. Skip pairs where either body has no collider.
2. Skip Static–Static pairs.
3. If either collider is a plane, keep the pair (planes have no finite AABB).
4. Otherwise compute world AABB:
   - Sphere: center ± radius on each axis (`center = body.position` with offset rotation).
   - Box: AABB of the eight OBB corners.
5. Keep the pair if AABBs overlap on all three axes.
6. Sort the kept pair list by `(min(idA,idB), max(idA,idB), idA, idB)` for determinism.

Informative: a sort-and-sweep on AABB center X with the same overlap test and the same pair sort is conforming if it produces the same pair set.

---

## 8. Narrowphase

Contact record: `(bodyA, bodyB, point, normal, penetration, e, mu)`.

- `normal` is a unit vector **from A toward B**.
- `penetration > 0` means overlap depth along the separating direction (A should move opposite to `normal` to resolve when B is immovable).
- `e = sqrt(clamp(eA,0,1) * clamp(eB,0,1))`
- `mu = sqrt(max(0,fA) * max(0,fB))`

### 8.1 Sphere–sphere

Centers `pa`, `pb`, radii `ra`, `rb`. `d = pb - pa`, `dist = |d|`. If `dist == 0`, use `normal = (0,1,0)`. Else `normal = d / dist`. `penetration = ra + rb - dist`. Drop if `penetration <= 0`. Contact point = `pa + normal * (ra - penetration/2)`. One point.

### 8.2 Sphere–plane / box–plane

Plane world normal `pn` = normalize(rotate body quat · `(0,1,0)`); if zero, `(0,1,0)`. Plane point `p0` = body position.

For **sphere** as A, plane as B: signed distance of center to plane `dist = (center - p0)·pn`. `penetration = radius - dist`. Drop if `penetration <= 0`. `normal = -pn`. Point = center projected onto plane. One point.

For **box** as A: test eight corners; each corner with `penetration = -((corner - p0)·pn) > 0` yields a contact at that corner with `normal = -pn`. Keep up to **4** deepest.

### 8.3 Sphere–box

Closest point on the OBB to the sphere center (clamp center into box local AABB, rotate back). If distance from center to closest ≥ radius, no hit. If center is inside the OBB (distance ≈ 0), push out along the minimum local face; `penetration = faceDepth + radius`. Otherwise `penetration = radius - dist`, outward from box toward sphere is `n_out`; contact `normal` from sphere(A) toward box(B) is `-n_out`. One point.

### 8.4 Box–box

Use separating-axis overlap on world AABBs of both boxes (sufficient for M5 fixtures, which keep boxes axis-aligned). If any axis separates, no hit. Else minimum-penetration AABB axis defines `normal` from A toward B (sign from center delta). Build up to **4** contact points at the corners of the overlap rectangle on A's face toward B. Penetration is the overlap depth on that axis.

Implementations MAY use a full OBB SAT; fixture scenes are axis-aligned so both MUST match the oracle.

### 8.5 Manifold size

| Pair | Max points |
|------|------------|
| sphere–* | 1 |
| box–plane | 4 |
| box–box | 4 |

---

## 9. Integrator

Under OQ-M5-03 A: **semi-implicit (symplectic) Euler**, fixed `dt`, one substep per `step()`.

For each Dynamic body, in order:

```
linearVelocity += gravity * dt
linearVelocity *= max(0, 1 - linearDamping * dt)
angularVelocity *= max(0, 1 - angularDamping * dt)
position += linearVelocity * dt
quaternion = normalize( quaternion + 0.5 * dt * Omega * quaternion )
```

where `Omega = (ωx, ωy, ωz, 0)` and `*` is quaternion multiply (`Omega * quaternion`). Zero-length quaternion after normalize becomes identity `(0,0,0,1)`.

No accumulator API in M5. No Verlet (unless PM picks OQ-M5-03 B).

---

## 10. Solver

Under OQ-M5-04 A: **sequential impulse**, contact + friction, **10** iterations, warm-start **off**.

### 10.1 Contact sort

Before solving, sort contacts by:

```
(min(idA,idB), max(idA,idB), idA, idB, point.x, point.y, point.z)
```

### 10.2 Restitution bias (once per step)

For each contact, with pre-solve `vn = (vA - vB) · normal` at the contact point (`v*` includes angular contribution `ω × r`):

- If `vn > 0.05` (closing along `normal`): `restBias = -e * vn`.
- Else `restBias = 0`.

Do **not** recompute `restBias` inside the iteration loop.

### 10.3 Iteration

Baumgarte: `slop = 0.005`, `baumgarte = 0.2`. If `penetration > slop`, `biasPen = (baumgarte / dt) * (penetration - slop)`; else `0`.

Effective mass `keff` along `normal` includes `invMass` and angular terms ` (r × n) · I_inv (r × n) ` for each Dynamic body.

Impulse scalar:

```
jn = (vn - restBias + biasPen) / keff
jn = max(jn, 0)
```

Apply `-jn * normal` to A and `+jn * normal` to B (Static/Kinematic skip apply).

Friction: tangential relative velocity `vt = vrel - normal * (vrel·normal)`. If `|vt| > 0` and `mu > 0`, unit tangent `t = vt / |vt|`, solve `jt` to cancel tangential speed, clamp `|jt| <= mu * jn`, apply similarly.

---

## 11. Sync to scene objects

After the solver, for each body with a linked `Object3D` `obj`:

1. Assign `obj.position.x/y/z` from `body.position.x/y/z` (component field writes).
2. Assign `obj.quaternion.x/y/z/w` from `body.quaternion`.
3. Sync Euler from quaternion per M3 §4.2 rule 2 (`rotation.setFromQuaternion(quaternion, rotation.order)`).

**Do not** write `obj.scale`. **Do not** replace `obj.position` by assigning a whole `Vector3` temporary that was copied out earlier and mutated offline — that is the M3 copy-gotcha (M3 §3.2). Field assigns on `obj.position.*` mutate the object.

`step` always syncs; there is no separate required `world.sync()` API in M5 (callers MAY expose a no-op alias; fixtures only call `step`).

Physics does not call `updateMatrixWorld`; renderers still do on `render` / `renderPath`.

---

## 12. Runtime traps

| Exit | stderr | When |
|------|--------|------|
| 105 | `runtime error: use after dispose` | `step` / mutating World API after `dispose` |
| 114 | `runtime error: invalid mass` | Dynamic body with `mass <= 0` at `step` |
| 115 | `runtime error: invalid fixed timestep` | `dt <= 0` at `step` |
| 116 | `runtime error: invalid collider` | Plane on Dynamic; sphere `radius <= 0`; box half-extent `<= 0` at `step` |

Exit **109** remains unused (M3.5 reserved). M4 traps 110–113 unchanged.

---

## 13. Concurrency note

`World.step` is single-threaded. User `parallel` tasks that both invoke `step` (or `add` / `remove` / setters that affect the same world) on one shared `World` conflict under M3.5. Physics MUST NOT spawn internal threads for stepping.

---

## 14. Linking and size

### 14.1 Link units

`farmos:physics` is a separate link unit from scene and path tracing. A program that does not reference any physics export MUST NOT link physics code (DCE).

### 14.2 Measurement — stripped size

- **AC-M5-04:** Hello world that does not import `farmos:physics` ≤ **20 KiB** stripped Windows x64 PE (same procedure as M1/M4).
- **AC-M5-05 (OQ-M5-08 A):** Let `S_demo` be the stripped size of the Appendix A stacking/bouncing demo, and `S_scene` the stripped size of a scene-only hello that imports `farmos:scene` + `farmos:math` but not `farmos:physics` and does not reference physics names. Require `S_demo - S_scene ≤ 64 KiB`.

Procedure: `farmc build …`, link with the project’s `-Os` / LTO / `--gc-sections` flags, `strip`, measure file size on Windows x64.

---

## 15. Acceptance criteria

| ID | Criterion |
|----|-----------|
| AC-M5-01 | Fixtures `001`–`030` pass under the harness kinds in §16. |
| AC-M5-02 | Stacking demo (Appendix A / fixture `006`): after `600` steps at `dt=1/60` (10 s), all sphere centers remain finite, ordered by increasing `y`, and above the floor (oracle match within fixture epsilon). |
| AC-M5-03 | Bouncing demo (fixtures `005`, `030`): restitution 0.5 sphere on plane damps; after 90 steps height is in the fixture band; after 600 steps matches oracle settle height. |
| AC-M5-04 | Hello without physics imports ≤ 20 KiB stripped (§14.2). |
| AC-M5-05 | `S_demo - S_scene ≤ 64 KiB` (§14.2). |
| AC-M5-06 | Determinism: same program ⇒ bit-identical printed floats for `run` / `run_approx` fixtures on the reference algorithm (oracle). |
| AC-M5-07 | Sync: fixtures `011`, `021` — mesh transform matches body after `step`. |
| AC-M5-08 | Traps: 114 (`012`,`013`), 115 (`014`,`026`), 116 (`020`), 105 (`015`); dispose twice OK (`029`). |
| AC-M5-09 | Static unmoved (`009`); kinematic pushes dynamic (`010`) and is not pushed (`028`). |
| AC-M5-10 | Friction reduces slide speed (`007`); linear damping reduces speed (`018`); angular integrate (`016`). |
| AC-M5-11 | Geometric-mean restitution (`025` settle matches `e = sqrt(0.25*1) = 0.5` behavior of `005`). |
| AC-M5-12 | Unused physics not linked (AC-M5-04). |

AC-M5-04 and AC-M5-05 are eggtooth checks (no golden stdout beyond existing hello).

---

## 16. Test harness

See `spec/tests/M5/README.md`. Kinds: `run`, `run_approx` (M2 epsilon), `runtime_trap`.

- 30 fixtures: **5** `run`, **19** `run_approx`, **6** `runtime_trap`. No `compile_error`, no `run_png`.

### 16.1 Index

| ID | Name | Kind | Notes |
|----|------|------|-------|
| 001 | `001_world_step_empty` | run | empty world steps |
| 002 | `002_import_smoke` | run | construct + add |
| 003 | `003_gravity_default_freefall` | run_approx | one step default g |
| 004 | `004_set_gravity_zero` | run_approx | zero g |
| 005 | `005_sphere_bounce_plane` | run_approx | 600 steps settle |
| 006 | `006_sphere_stack_600` | run_approx | 4 spheres, 10 s |
| 007 | `007_friction_slide` | run_approx | vx reduced |
| 008 | `008_sphere_box_contact` | run_approx | ball stops at box |
| 009 | `009_static_unmoved` | run_approx | static pose fixed |
| 010 | `010_kinematic_pushes` | run_approx | kinematic moves dynamic |
| 011 | `011_sync_mesh_position` | run_approx | mesh.y == body.y |
| 012 | `012_trap_mass_zero` | runtime_trap | 114 |
| 013 | `013_trap_mass_negative` | runtime_trap | 114 |
| 014 | `014_trap_invalid_dt` | runtime_trap | 115 |
| 015 | `015_trap_disposed_world` | runtime_trap | 105 |
| 016 | `016_angular_spin` | run_approx | quat integrates |
| 017 | `017_sphere_sphere_contact` | run_approx | inelastic meet |
| 018 | `018_linear_damping` | run_approx | damp factor |
| 019 | `019_body_id_order` | run | ids 0,1,2 |
| 020 | `020_trap_plane_on_dynamic` | runtime_trap | 116 |
| 021 | `021_sync_quaternion` | run_approx | mesh quat sync |
| 022 | `022_no_collider_falls` | run_approx | translate only |
| 023 | `023_remove_body` | run | remove + step |
| 024 | `024_set_fixed_timestep` | run_approx | dt=0.1 |
| 025 | `025_restitution_geometric_mean` | run_approx | e=√(0.25) |
| 026 | `026_trap_negative_dt` | runtime_trap | 115 |
| 027 | `027_box_plane_rest` | run_approx | box on plane |
| 028 | `028_kinematic_not_pushed` | run_approx | kinematic fixed |
| 029 | `029_dispose_twice_ok` | run | second dispose no-op |
| 030 | `030_bounce_height_band` | run_approx | band after 90 steps |

---

## 17. Open questions

**None.** PM decided OQ-M5-01 .. OQ-M5-08, all option A, on 2026-10-09. Stacking-on-spheres note accepted. See §18.

---

## 18. Resolved decisions

PM 2026-10-09 accepted option A for every OQ below. The RD rows are housekeeping those questions did not cover.

| ID | Decision |
|----|----------|
| OQ-M5-01 | Built-in module `farmos:physics`; does not re-export scene/math. |
| OQ-M5-02 | `RigidBody` optionally holds `Object3D`; colliders on body; `step` syncs pose → object. |
| OQ-M5-03 | Semi-implicit Euler, fixed `dt` default `1/60`, `step` = one substep, no accumulator. |
| OQ-M5-04 | Sequential impulse, 10 iters, warm-start off; `e`/`μ` geometric means. |
| OQ-M5-05 | AABB / sort-and-sweep broadphase, id tie-break; narrowphase as §8. |
| OQ-M5-06 | Body types: Dynamic + Static + Kinematic. |
| OQ-M5-07 | Full 3D `ω`, analytical inertia, quaternion normalize after integrate. |
| OQ-M5-08 | Hello without physics ≤ 20 KiB; `S_demo - S_scene ≤ 64 KiB`. |
| RD-M5-01 | Default gravity `(0,-9.81,0)`; `setGravity` copies components. |
| RD-M5-02 | Restitution stored any float, clamped [0,1] at solve; friction clamped `≥ 0` at solve. |
| RD-M5-03 | Traps 114 mass, 115 dt, 116 collider; 105 dispose; 109 unused. |
| RD-M5-04 | No soft bodies, joints, CCD, fluids, or threaded step. |
| RD-M5-05 | `physics_ref.py` is the numeric oracle; not product code. |
| RD-M5-06 | Sync uses field assigns; scale untouched; Euler synced per M3. |
| RD-M5-07 | Body id = registration order integer; stable for contact sort. |
| RD-M5-08 | Baumgarte `0.2`, slop `0.005`, restitution threshold `0.05`, solver iterations `10`. |
| RD-M5-09 | Damping: `v *= max(0, 1 - damping * dt)` each substep before position integrate. |

---

## 19. Amendments relative to M3 / M3.5 / M4

M1–M4 markdown files are not edited.

| # | Amendment |
|---|-----------|
| 1 | New module `farmos:physics` with exports in §3. |
| 2 | Optional `RigidBody` → `Object3D` link; sync after `step` (§11). |
| 3 | Traps 114–116; 105 applies to disposed `World`. |
| 4 | M3.5: `World.step` / mutating World APIs are effectful on the world receiver for conflict analysis. |
| 5 | Harness: `spec/tests/M5/README.md`; oracle `_ref/physics_ref.py`. |

---

## Appendix A — Stacking / bouncing demo (AC shape)

Same structure as fixture `006` (stack) plus a bouncing ball as in `005`. Stripped size of a program shaped like the following is `S_demo` for AC-M5-05.

```
import { World, RigidBody, BODY_DYNAMIC, BODY_STATIC,
         SphereCollider, PlaneCollider } from "farmos:physics";
import { Scene, Mesh, SphereGeometry, MeshBasicMaterial } from "farmos:scene";

function main(): int {
  const scene: Scene = new Scene();
  const world: World = new World();
  const floor: RigidBody = new RigidBody(BODY_STATIC);
  floor.setFriction(0.5);
  floor.setCollider(new PlaneCollider());
  world.add(floor);
  let i: int = 0;
  while (i < 4) {
    const mesh: Mesh = new Mesh(new SphereGeometry(0.5, 8, 4), new MeshBasicMaterial(0xffffff));
    mesh.position.set(0.0, 0.5 + i * 1.02, 0.0);
    scene.add(mesh);
    const body: RigidBody = new RigidBody(BODY_DYNAMIC);
    body.setMass(1.0);
    body.setFriction(0.5);
    body.setCollider(new SphereCollider(0.5));
    body.setObject(mesh);
    world.add(body);
    i = i + 1;
  }
  const ballMesh: Mesh = new Mesh(new SphereGeometry(0.5, 8, 4), new MeshBasicMaterial(0xff0000));
  ballMesh.position.set(2.0, 2.0, 0.0);
  scene.add(ballMesh);
  const ball: RigidBody = new RigidBody(BODY_DYNAMIC);
  ball.setMass(1.0);
  ball.setRestitution(0.5);
  ball.setCollider(new SphereCollider(0.5));
  ball.setObject(ballMesh);
  world.add(ball);
  i = 0;
  while (i < 600) {
    world.step();
    i = i + 1;
  }
  return 0;
}
```

---

## Appendix B — Oracle

```
python3 spec/tests/M5/_ref/physics_ref.py
```

Smoke-prints free-fall, bounce, stack, friction, spin, static, and kinematic cases used to bake `.expected` files.

---

## Document history

- 2026-10-09: DRAFT. Option A throughout. Fixtures 001–030. OQ-M5-01 .. OQ-M5-08 open, recommended A.
- 2026-10-09: FINAL. PM accepted OQ-M5-01 .. OQ-M5-08, all option A (stacking-on-spheres note accepted). Open questions cleared.
