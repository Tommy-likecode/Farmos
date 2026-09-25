# Farmos M2 — Math Standard Library Specification

**Status:** FINAL (PM-approved 2026-09-24)  
**Milestone:** M2 Math stdlib  
**Depends on:** `spec/M1-core.md` (FINAL)  
**Normative keywords:** MUST / SHOULD / MAY per RFC 2119.

This document specifies the Farmos M2 math standard library, value/reference semantics for math types, user-defined operator overloading (deferred from M1 OQ-M1-15), printing, dead-code rules, acceptance criteria, and the M2 test index. It does **not** specify compiler/runtime/stdlib implementation code.

Related: `/workspace/farmos/PLAN.md`, `/workspace/farmos/spec/M1-core.md`, `/workspace/farmos/spec/tests/M2/README.md`.

**Target platform (PLAN.md, amended 2026-09-24):** Windows x64 only; C backend via clang or gcc (MSVC `cl` unsupported); fully local. Linux size ACs from M1 text are **out of scope** for M2 measurement; M2 size ACs use Windows x64 stripped PE.

---

## 1. Scope and non-goals

### 1.1 In scope (M2 MUST implement)

- Built-in module `farmos:math` exporting: `Vector2`, `Vector3`, `Vector4`, `Matrix3`, `Matrix4`, `Quaternion`, `Color`, `Euler`, `Ray`, `Box3`, `Sphere`, and helper result type `RayHit`.
- Language extensions required by the math API:
  - Methods on `struct` (value-type methods; M1 structs had fields only).
  - User-defined operator overloading.
  - Built-in operators for math types.
  - `println` / `print` / `str` overloads for math types.
- Built-in module import paths of the form `"farmos:…"`.
- Dead-code elimination so unused math is not linked.
- Fixture pack under `spec/tests/M2/` (≥ 40), including `run_approx` (documented in `tests/M2/README.md`).

### 1.2 Explicit non-goals (M2 MUST NOT require)

| Deferred | Target |
|----------|--------|
| Scene graph, Mesh, cameras, lights, Renderer | M3 |
| Ray tracing BVH / PBR materials | M4 |
| Physics | M5 |
| Generics, closures, inheritance, `null` / `Optional<T>` | post-M1 (unchanged) |
| Color management / sRGB↔linear display pipeline | post-M2 (raw hex in M2; Resolved OQ-M2-05) |
| SIMD / f32 math types | post-M2 |
| Full Three.js math surface (Curve, Frustum, Triangle, …) | post-M2 as needed |

### 1.3 Design constraints

- PLAN.md: value types for vec/mat; stdlib linked only if used; Three.js-like API for M3 ports.
- M1: `int`=i64, `float`=f64, semicolons required, no `null`, IEEE float `/`, ECMAScript float printing, program-lifetime arena for **classes** only.
- Critical M3 requirement: `mesh.position.x = 1` MUST mutate the mesh’s stored position (see §3).

---

## 2. Module exposure

### 2.1 Built-in module path

Math types MUST be imported from the built-in module path `"farmos:math"`:

```
import { Vector3, Matrix4, Color } from "farmos:math";
```

- The string `"farmos:math"` is a **virtual module id**, not a filesystem path.
- M1 rule that `from` strings MUST end in `.fm` is **amended for built-in ids only**: paths matching `farmos:[a-z][a-z0-9]*` MAY omit `.fm` and MUST NOT resolve on disk.
- Invalid built-in id (unknown after `farmos:`) → `E0304` (module not found), column at the opening `"` of the string.
- Relative `.fm` imports remain as in M1.

**Justification (Resolved OQ-M2-01):** Named built-in modules make unused exports trivially DCE’d per import/use, keep hello-world free of math symbols, and mirror Three.js ESM better than a global prelude. A global prelude MUST NOT be provided in M2.

### 2.2 Exports

`farmos:math` MUST export exactly the types listed in §1.1 (and their constructors as type names usable with `new` / struct literals). No other names are required in M2.

### 2.3 Linking / DCE

- Emitting a reference to a math type, method, operator, or `println` overload MAY pull only the **used** subset of the math runtime.
- A program that does not import `farmos:math` and does not otherwise reference math MUST produce a binary **byte-identical** (or size-unchanged per AC-M2-01) to the same program built under an M1-only toolchain baseline.
- Using only `Vector3` MUST NOT require linking `Matrix4` / `Quaternion` / … implementation object code (AC-M2-02).

---

## 3. Value semantics and mutation (critical)

### 3.1 Decision (normative)

Math types are **`struct` value types** with **methods**.

| Operation | Semantics |
|-----------|-----------|
| Assignment `a = b` | Copy all fields (deep for nested value fields; class-typed fields would copy the reference — math types have none). |
| Pass to / return from function | Copy (by value), except the implicit `this` of a method call (§3.2). |
| `let b = a; b.x = 1;` | Mutates `b` only; `a` unchanged. |
| Field of a `class`: `mesh.position.x = 1` | `position` is a struct field stored **inline** in the object; `.x = 1` mutates that inline storage. MUST NOT invent a temporary copy. |
| Field of a `struct`: `outer.inner.x = 1` | Same: mutates through the lvalue path. |
| `const v: Vector3 = …; v.x = 1;` | Forbidden (`E0504` / `E0511`) — shallow const on struct forbids field writes (M1 §5.4). |

This is required so M3 ports of `mesh.position.set(…)` / `mesh.position.x = …` match Three.js mutation of the object’s own fields while remaining PLAN-compliant value types.

### 3.2 Struct methods (language extension)

M1 forbade methods on structs. M2 **MUST** allow them on **all** structs, including user-defined types (Resolved OQ-M2-07), not only `farmos:math` types:

```
struct_decl   ::= "struct" identifier "{" struct_member* "}"
struct_member ::= struct_field | method_member
```

- Method syntax matches class methods (M1 §3.2): `name(params)(: type)? block`, body MAY use `this`.
- **`this` for struct methods** is an implicit **by-reference** binding to the receiver’s storage for the duration of the call. Writes to `this.field` mutate that storage.
- The receiver expression of `recv.method(…)` MUST be evaluated as an **lvalue** when it designates storage (local, parameter, field path, array element). If the receiver is a temporary rvalue, the temporary’s storage is mutated and then discarded after the full postfix expression completes.

### 3.3 Mutating vs pure methods

- **Mutating** methods: may write `this` fields; documented as mutating; conventionally return `this`’s type for chaining (§3.4).
- **Pure** methods: MUST NOT write `this`; return a new value (or `bool` / `float` / …).

### 3.4 Chaining (`v.add(w).multiplyScalar(2)`)

Mutating methods that return the struct’s own type and execute `return this;` have **receiver-return** semantics:

1. Inside a **postfix chain** `recv.m1().m2()…` where each `mi` is a mutating method returning the same struct type via `return this;`, every call MUST alias the **same storage** as `recv` (when `recv` is an lvalue).
2. When the call result escapes the postfix chain (assignment, argument, field init, etc.), the value MUST be **copied**.

Example:

```
let v: Vector3 = new Vector3(1.0, 0.0, 0.0);
v.add(new Vector3(0.0, 1.0, 0.0)).multiplyScalar(2.0);
// v is (2, 2, 0)
let w: Vector3 = v.add(new Vector3(1.0, 0.0, 0.0));
// v is (3, 2, 0); w is a copy of that value
```

### 3.5 Three.js `target` out-parameters

Three.js often uses `getCenter(target)`. With value types and no `null`, M2 MUST:

- Prefer **return by value**: `getCenter(): Vector3`, `getSize(): Vector3`, `at(t: float): Vector3`, etc.
- MUST NOT require a mutable out-parameter for these getters in M2.

General user-facing `ref` parameters MUST NOT be added in M2 (Resolved OQ-M2-03).

### 3.6 Ray misses (no `null`)

M1 has no `null` / optionals. Intersections MUST use result struct:

```
struct RayHit {
  hit: bool;
  point: Vector3;
  distance: float;
}
```

- `hit == false` → `point` MUST be `(0,0,0)` and `distance` MUST be `0.0` (deterministic dummy).
- `hit == true` → `point` is the intersection; `distance` is parametric `t` along the ray (`origin + direction * t`) with `t >= 0` for forward hits.


---


### 3.7 Copies vs aliases (Resolved OQ-M2-12)

Three.js `Vector3` is a mutable reference object. In Farmos, math types are values (§3.1):

```
let p: Vector3 = mesh.position;
p.x = 1.0;
// mesh.position is UNCHANGED — p is a copy
```

This divergence is **normative**. M2 MUST NOT add aliasing references / `ref` locals for math values.

Idiomatic Farmos:

```
mesh.position.x = 1.0;           // mutate through the field path
mesh.position.set(1.0, 2.0, 3.0);
// or write back:
let p: Vector3 = mesh.position;
p.x = 1.0;
mesh.position = p;
```

The **M3 and M6 porting guides MUST document** this rule and the idioms above (Resolved OQ-M2-12).

## 4. Conventions (all math types)

| Topic | Rule |
|-------|------|
| Coordinates | Right-handed; **Y-up** (Three.js). |
| Angles | **Radians**. |
| Matrix storage | **Column-major** `elements` array (length 9 for `Matrix3`, 16 for `Matrix4`), index `e[row + col*N]` with `N∈{3,4}` — same layout as Three.js. |
| `set(…)` arguments | **Row-major** argument order (Three.js): `set(n11,n12,n13,…)` writes columns into `elements`. |
| Quaternion | Components `(x, y, z, w)`; `w` is the real/scalar part; Hamilton product; Three.js order. |
| Euler | Default order **`"XYZ"`** (Three.js default). Intrinsic Tait–Bryan per order string. |
| Color | Components `r,g,b` as **linear** `float` in **[0,1]** (may store out-of-range; methods that convert hex clamp — §11). |
| Vector `==` / `equals` | Component-wise IEEE `==` (M1 §4.12.1): `+0` equals `-0`; NaN component → not equal. Same rule for matrices/quaternions/colors over their numeric fields. |
| Zero normalize | `normalize()` on zero-length vector leaves it **unchanged** `(0,…)` (Three.js). |
| Singular invert | `invert()` on singular matrix sets the matrix to **all zeros** and still returns `this` (Three.js). |
| Float transcends | Implementations MAY use host `libm`. Fixtures that depend on transcends MUST use `run_approx` (§16) or avoid printing raw libm outputs. |

---


## 4A. Amendment to M1 §4.8 (effective with M2)

**Status:** Normative (Resolved OQ-M2-11, PM 2026-09-24).

This section amends M1 §4.3 / §4.8 / §4.10 for **M2 and later** only. It softens `E0402` / `E0408` for the cases below.

**M1 acceptance claim unchanged:** An **M1-conformant** compiler MUST still reject programs that rely on these coercions (they remain type errors under M1-core.md). An **M2-conformant** compiler MUST accept them per this section. M1 fixtures that expect `E0402`/`E0408` for int literals in float contexts remain valid for M1-only builds; they are not M2 language tests.

### 4A.1 Motivation

Three.js ports routinely write integer-looking numerics where Farmos expects `float`:

```
new Vector3(1, 2, 3);
mesh.position.z = 5;
new Color(1, 0, 0);
v.multiplyScalar(0);
```

Under strict M1 rules each `1` / `5` / `0` is an `int` literal → type error. That blocks the M3 AC of near-identical structure.

### 4A.2 Rule

An **integer literal** (decimal or hex per M1 §2.6), optionally prefixed by a single unary `-` (and no other operators), that appears in a **float context** MUST be typed as `float` when its mathematical value \(v\) is **exactly representable** in IEEE 754 binary64 as an integer, i.e. \(|v| \leq 2^{53}\).

Float contexts:

1. Argument to a parameter of type `float`.
2. RHS of assignment / `let`/`const` initializer whose target type is `float` (including field paths like `v.x`).
3. Expression in `return` of a function/method with return type `float`.
4. Operand of a binary arithmetic or comparison operator where the **other** operand has type `float` (**literal operand only**). Example: with `x: float`, `x * 2` and `2 * x` type the literal `2` as `float`; `x + i` with `i: int` remains `E0402`.

If the literal’s value is outside \([-2^{53}, 2^{53}]\) → compile error **`E0608`** at the literal:

| Code | Message template |
|------|------------------|
| E0608 | integer literal `{lit}` cannot be used as `float` (not exactly representable) |

**Non-literal** `int` expressions (names, calls, arithmetic yielding `int`) in a float context remain errors: argument/assignment → **`E0408`**; mixed arithmetic with a non-literal `int` → **`E0402`**. Explicit `float(i)` still required.

Hex color constructors taking `int` (`new Color(0xff0000)`, `setHex`) are **unchanged** — those parameters are `int`, not float contexts.

Unary `+` on an integer literal is **not** included (only optional unary `-`).

### 4A.3 Non-goals

- No implicit conversion of `float` → `int`.
- No implicit conversion of runtime `int` values.
- No change to integer arithmetic between two `int`s.

### 4A.4 Fixtures

Fixtures `051`–`054` exercise this amendment and are ordinary required M2 fixtures.

---

## 5. Operator overloading

### 5.0 Keyword and grammar amendments

- `operator` is a new **reserved keyword** in M2 (MUST NOT be used as an identifier; reuse `E0003` if so).
- Compilation-unit `item` production (M1 §3.1) is amended:

```
item ::= import_decl | export_item | decl | operator_decl
decl ::= function_decl | struct_decl | class_decl | const_decl
```

- `struct_decl` is amended per §3.2 (methods allowed).

### 5.1 Syntax

```
operator_decl ::= "export"? "operator" overload_op "(" param_list ")" ":" type block
overload_op   ::= "+" | "-" | "*" | "/" | "%" | "==" | "!="
```

- Unary minus: `operator -(a: T): T` — exactly **one** parameter.
- Binary: exactly **two** parameters.
- MAY appear at module scope (including inside `farmos:math` and user `.fm` modules).
- MUST NOT appear inside types as methods; method form is **not** used (`operator` is a free declaration).
- `export operator …` exports the overload for importers (user modules). Built-in math operators are available when the corresponding types are in scope via import (see §5.4).

### 5.2 Overloadable operators

| Operator | Arity | Notes |
|----------|-------|-------|
| `+` `-` `*` `/` `%` | binary | |
| `-` | unary | Distinguished by arity |
| `==` `!=` | binary | MUST return `bool` |

**Not** overloadable in M2: `<` `<=` `>` `>=`, `&&` `||` `!`, `=` and compound assigns (compounds are derived — §5.5), `[]`, `()`.

### 5.3 Resolution

1. Built-in primitive operators on `int`/`float`/`bool`/`string` (M1) are fixed; a user `operator` with only primitive parameter types that would replace them → `E0601`.
2. For an operator expression, collect candidate overloads visible in the current module (imports + local) whose parameter types **exactly** match operand types (no `int`/`float` coercion).
3. If exactly one candidate → use it.
4. If none → `E0403` (operator not defined for type) at the operator token.
5. If more than one → `E0602` ambiguous overload at the operator token.
6. Return type MUST be exactly as declared; for `==`/`!=` MUST be `bool` else `E0603`.

### 5.4 Interaction with math built-ins

When math types are imported, the following built-in overloads MUST be available (same resolution rules). Scalar means `float`.

| Expression | Result |
|------------|--------|
| `v + w`, `v - w` | same vector type |
| `-v` | same vector type |
| `v * s`, `s * v` | vector |
| `v / s` | vector (component `/`; `/0` IEEE) |
| `m * m` | matrix (Matrix3×Matrix3, Matrix4×Matrix4) |
| `m * v` | `Matrix3*Vector3`→`Vector3`; `Matrix4*Vector4`→`Vector4`; `Matrix4*Vector3`→`Vector3` treating `w=1` (affine point) |
| `q * q` | Quaternion |
| `v == w`, `v != w` | `bool` (equals / not) |

`%` is NOT defined on math types in M2.

### 5.5 Compound assignment

`a ⊕= b` for overloadable `⊕` ∈ `{+,-,*,/,%)` MUST desugar to `a = a ⊕ b` with **single** evaluation of the lvalue `a` (M1 §2.9), using overload resolution on `⊕`.

No separate `operator +=` declarations.

### 5.6 Diagnostics (new codes; no collision with M1 ≤ E0513)

| Code | Message template |
|------|------------------|
| E0601 | cannot overload built-in operator `{op}` for primitive types |
| E0602 | ambiguous operator `{op}` for types `{lhs}` and `{rhs}` |
| E0603 | operator `{op}` overload must return `{expected}`, found `{found}` |
| E0604 | invalid operator overload arity for `{op}` |
| E0605 | operator `{op}` is not overloadable |
| E0606 | duplicate operator overload for `{op}` with parameter types `{sig}` |

---

## 6. Printing

### 6.1 `print` / `println` / `str`

M2 adds overloads for each math type `T` in §7–15:

```
function print(value: T): void
function println(value: T): void
function str(value: T): string
```

Format (exact). Components use M1 float formatting (§6.1 of M1-core). No spaces after commas? **One space after each comma** as below:

| Type | Format |
|------|--------|
| Vector2 | `Vector2({x}, {y})` |
| Vector3 | `Vector3({x}, {y}, {z})` |
| Vector4 | `Vector4({x}, {y}, {z}, {w})` |
| Matrix3 | `Matrix3({e0}, {e1}, …, {e8})` in `elements` order (column-major storage order) |
| Matrix4 | `Matrix4({e0}, …, {e15})` in `elements` order |
| Quaternion | `Quaternion({x}, {y}, {z}, {w})` |
| Color | `Color({r}, {g}, {b})` |
| Euler | `Euler({x}, {y}, {z}, "{order}")` |
| Ray | `Ray(origin: {Vector3…}, direction: {Vector3…})` |
| Box3 | `Box3(min: {Vector3…}, max: {Vector3…})` |
| Sphere | `Sphere(center: {Vector3…}, radius: {r})` |
| RayHit | `RayHit(hit: {true\|false}, point: {Vector3…}, distance: {d})` |

Example: `println(new Vector3(1.0, 2.0, 3.0));` → `Vector3(1, 2, 3)\n`.

---

## 7. `Vector2`

```
struct Vector2 {
  x: float;
  y: float;
}
```

### 7.1 Constructors

| Form | Meaning |
|------|---------|
| `new Vector2()` | `(0, 0)` |
| `new Vector2(x: float, y: float)` | `(x, y)` |
| `Vector2 { x: …, y: … }` | named fields |

### 7.2 Methods

Mutating methods return `Vector2` with receiver-return semantics (§3.4) unless noted.

| Signature | Mut? | Semantics |
|-----------|------|-----------|
| `set(x: float, y: float): Vector2` | Y | set components |
| `copy(v: Vector2): Vector2` | Y | copy from `v` |
| `clone(): Vector2` | N | return copy of `this` |
| `add(v: Vector2): Vector2` | Y | `this += v` componentwise |
| `addVectors(a: Vector2, b: Vector2): Vector2` | Y | `this = a + b` |
| `sub(v: Vector2): Vector2` | Y | |
| `subVectors(a: Vector2, b: Vector2): Vector2` | Y | |
| `multiplyScalar(s: float): Vector2` | Y | |
| `divideScalar(s: float): Vector2` | Y | IEEE `/` |
| `negate(): Vector2` | Y | `this = -this` |
| `dot(v: Vector2): float` | N | |
| `lengthSq(): float` | N | |
| `length(): float` | N | `sqrt(lengthSq)` |
| `normalize(): Vector2` | Y | zero → unchanged |
| `distanceTo(v: Vector2): float` | N | |
| `distanceToSquared(v: Vector2): float` | N | |
| `lerp(v: Vector2, alpha: float): Vector2` | Y | `this += (v-this)*alpha` |
| `equals(v: Vector2): bool` | N | IEEE component `==` |
| `applyMatrix3(m: Matrix3): Vector2` | Y | treat as `(x,y,1)` |

---

## 8. `Vector3`

```
struct Vector3 {
  x: float;
  y: float;
  z: float;
}
```

### 8.1 Constructors

`new Vector3()` → `(0,0,0)`; `new Vector3(x,y,z)`; brace literal.

### 8.2 Methods

| Signature | Mut? | Semantics |
|-----------|------|-----------|
| `set(x,y,z: float): Vector3` | Y | |
| `copy(v: Vector3): Vector3` | Y | |
| `clone(): Vector3` | N | |
| `add(v: Vector3): Vector3` | Y | |
| `addVectors(a,b: Vector3): Vector3` | Y | |
| `sub(v: Vector3): Vector3` | Y | |
| `subVectors(a,b: Vector3): Vector3` | Y | |
| `multiplyScalar(s: float): Vector3` | Y | |
| `divideScalar(s: float): Vector3` | Y | |
| `negate(): Vector3` | Y | |
| `dot(v: Vector3): float` | N | |
| `cross(v: Vector3): Vector3` | Y | `this = this × v` |
| `crossVectors(a,b: Vector3): Vector3` | Y | `this = a × v` |
| `lengthSq(): float` | N | |
| `length(): float` | N | |
| `normalize(): Vector3` | Y | zero unchanged |
| `distanceTo(v: Vector3): float` | N | |
| `distanceToSquared(v: Vector3): float` | N | |
| `lerp(v: Vector3, alpha: float): Vector3` | Y | |
| `equals(v: Vector3): bool` | N | |
| `applyMatrix3(m: Matrix3): Vector3` | Y | normal matrix style (no translation) |
| `applyMatrix4(m: Matrix4): Vector3` | Y | as affine point `w=1`, divide by `w` if `w≠0` / `±Inf` handling per IEEE |
| `applyQuaternion(q: Quaternion): Vector3` | Y | rotate |
| `transformDirection(m: Matrix4): Vector3` | Y | as direction `w=0`, then normalize |
| `projectOnVector(v: Vector3): Vector3` | Y | |
| `reflect(normal: Vector3): Vector3` | Y | `normal` assumed unit |

---

## 9. `Vector4`

```
struct Vector4 {
  x: float;
  y: float;
  z: float;
  w: float;
}
```

Constructors: `new Vector4()` → `(0,0,0,0)`; `new Vector4(x,y,z,w)`.

Methods (same pattern as Vector3 where applicable): `set`, `copy`, `clone`, `add`, `addVectors`, `sub`, `subVectors`, `multiplyScalar`, `divideScalar`, `negate`, `dot`, `lengthSq`, `length`, `normalize`, `lerp`, `equals`, `applyMatrix4(m: Matrix4): Vector4`.

---

## 10. `Matrix3`

```
struct Matrix3 {
  elements: float[9];
}
```

Column-major `elements`. Constructors: `new Matrix3()` → **identity**.

| Signature | Mut? | Semantics |
|-----------|------|-----------|
| `identity(): Matrix3` | Y | |
| `set(n11,n12,n13,n21,n22,n23,n31,n32,n33: float): Matrix3` | Y | row-major args → column-major store |
| `copy(m: Matrix3): Matrix3` | Y | |
| `clone(): Matrix3` | N | |
| `multiply(m: Matrix3): Matrix3` | Y | `this = this * m` |
| `premultiply(m: Matrix3): Matrix3` | Y | `this = m * this` |
| `multiplyMatrices(a,b: Matrix3): Matrix3` | Y | `this = a * b` |
| `multiplyScalar(s: float): Matrix3` | Y | |
| `determinant(): float` | N | |
| `transpose(): Matrix3` | Y | |
| `invert(): Matrix3` | Y | singular → all zeros |
| `equals(m: Matrix3): bool` | N | |
| `makeTranslation(x,y: float): Matrix3` | Y | 2D affine |
| `makeRotation(theta: float): Matrix3` | Y | 2D |
| `makeScale(x,y: float): Matrix3` | Y | |
| `setFromMatrix4(m: Matrix4): Matrix3` | Y | upper-left 3×3 |

---

## 11. `Matrix4`

```
struct Matrix4 {
  elements: float[16];
}
```

`new Matrix4()` → identity.

| Signature | Mut? | Semantics |
|-----------|------|-----------|
| `identity(): Matrix4` | Y | |
| `set(n11..n44: float /* 16 args, row-major */): Matrix4` | Y | |
| `copy(m: Matrix4): Matrix4` | Y | |
| `clone(): Matrix4` | N | |
| `multiply(m: Matrix4): Matrix4` | Y | `this * m` |
| `premultiply(m: Matrix4): Matrix4` | Y | `m * this` |
| `multiplyMatrices(a,b: Matrix4): Matrix4` | Y | |
| `multiplyScalar(s: float): Matrix4` | Y | |
| `determinant(): float` | N | |
| `transpose(): Matrix4` | Y | |
| `invert(): Matrix4` | Y | singular → zeros |
| `equals(m: Matrix4): bool` | N | |
| `makeTranslation(x,y,z: float): Matrix4` | Y | |
| `makeRotationX(theta: float): Matrix4` | Y | |
| `makeRotationY(theta: float): Matrix4` | Y | |
| `makeRotationZ(theta: float): Matrix4` | Y | |
| `makeScale(x,y,z: float): Matrix4` | Y | |
| `makePerspective(left,right,top,bottom,near,far: float): Matrix4` | Y | Three.js style |
| `lookAt(eye,target,up: Vector3): Matrix4` | Y | |
| `compose(position: Vector3, quaternion: Quaternion, scale: Vector3): Matrix4` | Y | |
| `decompose(position: Vector3, quaternion: Quaternion, scale: Vector3): Matrix4` | Y | **Special:** parameters are mutated by-ref like method receivers of struct type — see §11.1 |
| `copyPosition(m: Matrix4): Matrix4` | Y | copy translation column |
| `extractRotation(m: Matrix4): Matrix4` | Y | |

### 11.1 `decompose` out-parameters

`decompose` MUST mutate the three struct arguments in place (by-ref parameter passing for these three parameters only). Normative rule for M2:

> A function or method parameter of math struct type MAY be declared with prefix `ref` in **stdlib signatures only** in M2 for `Matrix4.decompose`. User-facing `ref` syntax is **not** general language surface in M2 (Resolved OQ-M2-03). The signature is spelled in this spec as:
>
> `decompose(position: Vector3, quaternion: Quaternion, scale: Vector3): Matrix4`
>
> with the normative note that these three arguments are **in-out** and MUST be passed as lvalues; passing a temporary → `E0408`/`E0202` style reject `E0607`: `decompose argument must be an lvalue`.

| Code | Message |
|------|---------|
| E0607 | decompose argument must be an lvalue |

**Preferred alternative for ports:** callers who dislike out-params can read `compose` inversely later; M3 MAY wrap this.

---

## 12. `Quaternion`

```
struct Quaternion {
  x: float;
  y: float;
  z: float;
  w: float;
}
```

`new Quaternion()` → `(0,0,0,1)` identity; `new Quaternion(x,y,z,w)`.

| Signature | Mut? | Semantics |
|-----------|------|-----------|
| `set(x,y,z,w: float): Quaternion` | Y | |
| `copy(q: Quaternion): Quaternion` | Y | |
| `clone(): Quaternion` | N | |
| `identity(): Quaternion` | Y | `(0,0,0,1)` |
| `setFromEuler(e: Euler): Quaternion` | Y | |
| `setFromAxisAngle(axis: Vector3, angle: float): Quaternion` | Y | `axis` SHOULD be unit |
| `setFromRotationMatrix(m: Matrix4): Quaternion` | Y | uses upper 3×3 |
| `multiply(q: Quaternion): Quaternion` | Y | `this = this * q` |
| `premultiply(q: Quaternion): Quaternion` | Y | |
| `multiplyQuaternions(a,b: Quaternion): Quaternion` | Y | |
| `normalize(): Quaternion` | Y | zero → `(0,0,0,1)` |
| `invert(): Quaternion` | Y | conjugate / lengthSq |
| `conjugate(): Quaternion` | Y | |
| `dot(q: Quaternion): float` | N | |
| `lengthSq(): float` | N | |
| `length(): float` | N | |
| `slerp(qb: Quaternion, t: float): Quaternion` | Y | |
| `equals(q: Quaternion): bool` | N | |

---

## 13. `Color`

```
struct Color {
  r: float;
  g: float;
  b: float;
}
```

### 13.1 Constructors

| Form | Meaning |
|------|---------|
| `new Color()` | `(0,0,0)` |
| `new Color(r,g,b: float)` | components as given |
| `new Color(hex: int)` | raw hex (§13.2) |
| brace literal | named fields |

### 13.2 Hex conversion (raw / no color management)

**Decision (default):** hex is **raw**. No sRGB↔linear conversion in M2.

- `setHex(hex: int): Color` / constructor `new Color(hex)`:
  - Interpret `hex` as `0xRRGGBB` in the low 24 bits (`hex & 0xffffff`).
  - `r = ((hex >> 16) & 255) / 255.0`, similarly `g` (`>> 8`), `b`.
- `getHex(): int` → `round(r*255)<<16 | round(g*255)<<8 | round(b*255)` with each channel clamped to `[0,1]` before scaling.
- `round` means round half away from zero toward nearest int for `.5` ties… **MUST** use `floor(c * 255.0 + 0.5)` for `c` in `[0,1]` after clamp (simple biased round).

**Justification:** M2 has no renderer color pipeline; Three.js r152+ management would disagree with raw buffer math and complicate exact fixtures. M3/M4 MAY revisit (Resolved OQ-M2-05).

| Signature | Mut? |
|-----------|------|
| `set(r,g,b: float): Color` | Y |
| `setHex(hex: int): Color` | Y |
| `getHex(): int` | N |
| `copy(c: Color): Color` | Y |
| `clone(): Color` | N |
| `multiplyScalar(s: float): Color` | Y |
| `lerp(c: Color, alpha: float): Color` | Y |
| `equals(c: Color): bool` | N |

---

## 14. `Euler`

```
struct Euler {
  x: float;
  y: float;
  z: float;
  order: string;
}
```

- `new Euler()` → `(0,0,0,"XYZ")`.
- `new Euler(x,y,z: float)` → order `"XYZ"`.
- `new Euler(x,y,z: float, order: string)` → order as given.
- Valid orders: `"XYZ"`, `"YZX"`, `"ZXY"`, `"XZY"`, `"YXZ"`, `"ZYX"`. Invalid → runtime trap exit **104**, stderr `runtime error: invalid Euler order` (new trap).

| Signature | Mut? |
|-----------|------|
| `set(x,y,z: float, order: string): Euler` | Y |
| `copy(e: Euler): Euler` | Y |
| `clone(): Euler` | N |
| `setFromRotationMatrix(m: Matrix4, order: string): Euler` | Y |
| `setFromQuaternion(q: Quaternion, order: string): Euler` | Y |
| `reorder(newOrder: string): Euler` | Y |
| `equals(e: Euler): bool` | N | angles IEEE `==` and `order` byte-equal |

---

## 15. `Ray`, `Box3`, `Sphere`, `RayHit`

### 15.1 `Ray`

```
struct Ray {
  origin: Vector3;
  direction: Vector3;
}
```

`new Ray()` → origin `0`, direction `(0,0,-1)`; `new Ray(origin, direction)`.

| Signature | Mut? | Semantics |
|-----------|------|-----------|
| `set(origin, direction: Vector3): Ray` | Y | |
| `copy(r: Ray): Ray` | Y | |
| `clone(): Ray` | N | |
| `at(t: float): Vector3` | N | `origin + direction * t` |
| `lookAt(v: Vector3): Ray` | Y | direction = normalize(v-origin) |
| `distanceToPoint(p: Vector3): float` | N | |
| `intersectSphere(sphere: Sphere): RayHit` | N | nearest forward hit `t>=0` |
| `intersectBox(box: Box3): RayHit` | N | |
| `intersectTriangle(a,b,c: Vector3, backfaceCulling: bool): RayHit` | N | |

### 15.2 `Box3`

```
struct Box3 {
  min: Vector3;
  max: Vector3;
}
```

`new Box3()` → empty: `min=(+Inf,+Inf,+Inf)`, `max=(-Inf,-Inf,-Inf)`; `new Box3(min,max)`.

| Signature | Mut? |
|-----------|------|
| `set(min,max: Vector3): Box3` | Y |
| `copy(b: Box3): Box3` | Y |
| `clone(): Box3` | N |
| `makeEmpty(): Box3` | Y |
| `isEmpty(): bool` | N |
| `expandByPoint(p: Vector3): Box3` | Y |
| `expandByScalar(s: float): Box3` | Y |
| `containsPoint(p: Vector3): bool` | N |
| `containsBox(b: Box3): bool` | N |
| `intersectsBox(b: Box3): bool` | N |
| `intersectsSphere(s: Sphere): bool` | N |
| `getCenter(): Vector3` | N | return by value |
| `getSize(): Vector3` | N | return by value |
| `union(b: Box3): Box3` | Y |
| `intersect(b: Box3): Box3` | Y |
| `equals(b: Box3): bool` | N |

### 15.3 `Sphere`

```
struct Sphere {
  center: Vector3;
  radius: float;
}
```

`new Sphere()` → center `0`, radius `-1` (empty Three.js-style); `new Sphere(center, radius)`.

| Signature | Mut? |
|-----------|------|
| `set(center: Vector3, radius: float): Sphere` | Y |
| `copy(s: Sphere): Sphere` | Y |
| `clone(): Sphere` | N |
| `containsPoint(p: Vector3): bool` | N |
| `distanceToPoint(p: Vector3): float` | N |
| `intersectsSphere(s: Sphere): bool` | N |
| `intersectsBox(b: Box3): bool` | N |
| `clampPoint(p: Vector3): Vector3` | N | return by value |
| `getBoundingBox(): Box3` | N | |
| `equals(s: Sphere): bool` | N |

### 15.4 `RayHit`

Fields as §3.6. No methods required. Constructible with `new RayHit(hit, point, distance)` / brace literal.

---

## 16. Test harness extension (`run_approx`)

M1 README kinds are unchanged. M2 adds kind **`run_approx`**, documented in `spec/tests/M2/README.md` only (do not edit M1 README).

Headers:

```
# kind: run_approx
# exit: <integer>
# epsilon: <float>          ## absolute tolerance on float tokens
# stdout:
...
# end
```

Comparison: split stdout into tokens (whitespace-separated). For tokens that parse as floats under M1 float literal rules (plus `NaN`/`Infinity`/`-Infinity`), assert `abs(actual-expected) <= epsilon` or both NaN. Non-float tokens MUST match exactly. Use for libm-dependent prints only.

---

## 17. Runtime traps (additions)

| Exit | Message | When |
|------|---------|------|
| 104 | `invalid Euler order` | Invalid `Euler` order string |

---

## 18. Acceptance criteria

| ID | Criterion |
|----|-----------|
| AC-M2-01 | Windows x64: build `spec/tests/M1/001_hello.fm` with M2-capable `farmc`, `strip` the PE, size in bytes **equals** the M1 baseline hello size measured the same way on the same machine/toolchain (clang **or** gcc as used by the driver). Method: `farmc build … -o hello.exe` then strip (`llvm-strip`/`strip` equivalent) then `(Get-Item hello.exe).Length`. |
| AC-M2-02 | Program importing only `Vector3` and calling only `Vector3` APIs MUST NOT link `Matrix4`/`Quaternion`/… object sections (verify via map file or `dumpbin /SYMBOLS` / `nm` equivalent showing those symbols absent). Soft size budget: stripped exe ≤ **M1 hello + 8 KiB** (Resolved OQ-M2-06). |
| AC-M2-03 | ≥ **40** fixtures under `spec/tests/M2/` pass via local `scripts\build_and_test.ps1` (this pack: **54**). |
| AC-M2-04 | All `kind: run` stdout exact; `run_approx` within epsilon; `compile_error` match line:col:code. |
| AC-M2-05 | `mesh.position.x = 1` pattern (class field of type `Vector3`) mutates the instance (fixture). |
| AC-M2-06 | Chaining `v.add(w).multiplyScalar(s)` mutates `v` (fixture). |
| AC-M2-07 | Assignment copies vectors (fixture). |
| AC-M2-08 | User `operator +` for a user struct works; misuse diagnostics E0601–E0606 with correct columns. |
| AC-M2-09 | Determinism: same source → same stdout on repeated runs (exact fixtures). |
| AC-M2-10 | Unused `farmos:math` import of an unused name SHOULD still DCE that type’s code; empty import list forbidden by grammar. Prefer not importing unused names. |

---

## 19. Test case index

Fixtures live in `spec/tests/M2/`. See `tests/M2/README.md`.

| ID | File | Kind | Focus |
|----|------|------|-------|
| 001 | `001_vector3_print.fm` | run | construct + println |
| 002 | `002_vector3_add_chain.fm` | run | mutating chain |
| 003 | `003_vector3_copy_semantics.fm` | run | assignment copy |
| 004 | `004_vector3_class_field.fm` | run | `mesh.position.x` |
| 005 | `005_vector3_operators.fm` | run | `+` `-` `*` scalar |
| 006 | `006_vector3_dot_cross.fm` | run | dot / cross exact |
| 007 | `007_vector3_normalize_345.fm` | run | 3-4-5 normalize |
| 008 | `008_vector3_lerp.fm` | run | lerp endpoints/mid |
| 009 | `009_vector3_equals.fm` | run | equals / == |
| 010 | `010_vector3_distance.fm` | run | distanceTo |
| 011 | `011_vector2_basics.fm` | run | Vector2 |
| 012 | `012_vector4_basics.fm` | run | Vector4 |
| 013 | `013_matrix4_identity.fm` | run | det / elements |
| 014 | `014_matrix4_multiply.fm` | run | translation compose |
| 015 | `015_matrix4_scale_translate.fm` | run | makeScale/Translation |
| 016 | `016_vector3_apply_matrix4.fm` | run | applyMatrix4 |
| 017 | `017_matrix4_invert_identity.fm` | run | invert I |
| 018 | `018_matrix4_singular_invert.fm` | run | singular → zeros |
| 019 | `019_matrix3_basics.fm` | run | Matrix3 |
| 020 | `020_quaternion_identity.fm` | run | identity |
| 021 | `021_quaternion_multiply.fm` | run | multiply |
| 022 | `022_color_hex.fm` | run | hex ctor / getHex |
| 023 | `023_color_components.fm` | run | r,g,b |
| 024 | `024_euler_default_order.fm` | run | default XYZ |
| 025 | `025_box3_expand_contains.fm` | run | expand/contains |
| 026 | `026_box3_center_size.fm` | run | getCenter/getSize |
| 027 | `027_box3_intersects.fm` | run | intersectsBox |
| 028 | `028_sphere_contains.fm` | run | containsPoint |
| 029 | `029_sphere_intersects.fm` | run | intersectsSphere |
| 030 | `030_ray_at.fm` | run | at(t) |
| 031 | `031_ray_intersect_sphere_hit.fm` | run | RayHit hit |
| 032 | `032_ray_intersect_sphere_miss.fm` | run | RayHit miss |
| 033 | `033_normalize_zero.fm` | run | zero normalize |
| 034 | `034_unary_minus_vector.fm` | run | `-v` |
| 035 | `035_compound_assign_vector.fm` | run | `+=` |
| 036 | `036_operator_user_struct.fm` | run | user operator + |
| 037 | `037_operator_primitive_forbid.fm` | compile_error | E0601 |
| 038 | `038_operator_bad_arity.fm` | compile_error | E0604 |
| 039 | `039_operator_not_overloadable.fm` | compile_error | E0605 |
| 040 | `040_operator_return_type.fm` | compile_error | E0603 |
| 041 | `041_import_math_module.fm` | run | farmos:math import |
| 042 | `042_vector3_length.fm` | run | length 3-4-5 |
| 043 | `043_matrix4_transpose_sym.fm` | run | transpose |
| 044 | `044_color_setHex.fm` | run | setHex |
| 045 | `045_quaternion_axis_angle.fm` | run_approx | setFromAxisAngle |
| 046 | `046_matrix4_rotation_x.fm` | run_approx | makeRotationX(π/2) |
| 047 | `047_vector3_cross_unit.fm` | run | i×j=k |
| 048 | `048_rayhit_print.fm` | run | RayHit println |
| 049 | `049_box3_empty.fm` | run | isEmpty |
| 050 | `050_const_vector_field_error.fm` | compile_error | E0504/E0511 |
| 051 | `051_literal_float_arg.fm` | run | int lit → float arg (§4A) |
| 052 | `052_literal_float_assign.fm` | run | int lit → float field (§4A) |
| 053 | `053_literal_float_arith.fm` | run | `x * 2` literal (§4A) |
| 054 | `054_nonliteral_int_to_float.fm` | compile_error | E0408 non-lit int (§4A) |

**Fixture count:** **54** (matches `spec/tests/M2/*.fm`).

---

## 20. Open questions

None. All M2 open questions are closed (PM 2026-09-24).

---

## 21. Resolved decisions

| ID | Decision | Approved |
|----|----------|----------|
| OQ-M2-01 | Math exposed via `import { … } from "farmos:math"` only; no global math prelude. | PM 2026-09-24 |
| OQ-M2-02 | Mutating methods with receiver-return chaining (§3.3–§3.4); not immutable-only. | PM 2026-09-24 |
| OQ-M2-03 | No general `ref` parameters; return-by-value getters; sole stdlib in-out exception `Matrix4.decompose`. | PM 2026-09-24 |
| OQ-M2-04 | Ray misses use `RayHit { hit, point, distance }`; not sentinel `t`. | PM 2026-09-24 |
| OQ-M2-05 | Color hex is **raw** (no sRGB↔linear); M3/M4 MAY revisit. | PM 2026-09-24 |
| OQ-M2-06 | Vector3-only stripped size budget ≤ hello + 8 KiB (Windows x64, same toolchain/strip). | PM 2026-09-24 |
| OQ-M2-07 | User-defined structs MAY have methods (same grammar as math types). | PM 2026-09-24 |
| OQ-M2-08 | `Matrix4 * Vector3` treats vector as affine point (`w=1`); directions use `transformDirection`. | PM 2026-09-24 |
| OQ-M2-09 | Invalid Euler order → runtime trap exit **104** (not a hard compile error for non-literals). | PM 2026-09-24 |
| OQ-M2-10 | Unused named imports are not hard errors; DCE only (AC-M2-10 soft). | PM 2026-09-24 |
| OQ-M2-11 | Contextual integer-literal → `float` typing (§4A); amends M1 §4.8 for M2+ only. | PM 2026-09-24 |
| OQ-M2-12 | Value-copy divergence from Three.js aliasing is accepted; no aliasing refs; **M3/M6 porting guides MUST document** (§3.7). | PM 2026-09-24 |

---

## 22. Amendments to M1 that land with M2

M1-core.md is **not** edited by this milestone. The following language rules are **M2 amendments** (an M2-conformant `farmc` MUST implement them; an M1-only compiler need not):

| # | Amendment | Where specified |
|---|-----------|-----------------|
| 1 | **Contextual integer-literal → `float`** (exact representability \|v\|≤2⁵³; else E0608; non-literals still E0408/E0402) | §4A (amends M1 §4.8) |
| 2 | **Methods on user `struct`s** (by-ref `this`, receiver-return chaining) | §3.2 (amends M1 “structs have no methods”) |
| 3 | **`farmos:` virtual import paths** (e.g. `"farmos:math"`; need not end in `.fm`) | §2.1 (amends M1 module path rule) |
| 4 | **`operator` keyword** and user-defined operator overloading | §5 (fulfills M1 OQ-M1-15 deferred work) |
| 5 | **`print`/`println`/`str` overloads** for math types | §6 |
| 6 | **Runtime trap 104** (`invalid Euler order`) | §17 |
| 7 | Diagnostics **E0601–E0608** | §5.6 / §4A / §11.1 |

Informative notes (not M1 text edits): Windows-x64-only size ACs for M2 measurement (PLAN.md); Three.js copy-vs-alias porting requirement for M3/M6 (§3.7).

---

## 23. Resolved relative to M1 deferred items

| M1 item | M2 disposition |
|---------|----------------|
| OQ-M1-15 user operator overloading | Specified in §5 |
| Built-in overloads only in M1 | Extended: math + user operators |
| Math types / Color / Ray / … | This document |

---

## Document history


- 2026-09-24: Initial M2 draft (math stdlib + operators + fixtures).
- 2026-09-24: OQ-M2-11/12 drafted (§4A PENDING, §3.7); fixtures 051–054.
- 2026-09-24: FINAL — PM approved OQ-M2-01..12; §4A normative; Resolved decisions; M1 amendments list; fixtures 051–054 ungated.
