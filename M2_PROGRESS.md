# M2 Math Stdlib Implementation Progress

## Status Summary

**Tests Passing:** 3/54 (5.6%)
- ✅ 001_vector3_print.fm - Struct printing with proper formatting
- ✅ 003_vector3_copy_semantics.fm - Value semantics, field mutation
- ✅ 041_import_math_module.fm - Virtual module import

**Core Infrastructure Complete:**
- ✅ Virtual module loading (`farmos:*` paths)
- ✅ Struct definitions for Vector2/3/4 in `farmos:math`
- ✅ Struct field access (read and write)
- ✅ Struct default and parameterized constructors
- ✅ Copy-by-value semantics
- ✅ Struct printing with `println`/`print` (with fflush fix for buffering)

## What's Implemented

### Language Features
1. **Struct Value Semantics** - Assignment copies values, field mutation works
2. **Struct Printing** - Custom print helpers with proper float formatting
3. **Virtual Built-in Modules** - `import { Vector3 } from "farmos:math"`
4. **Buffering Fix** - printf/write() synchronization with fflush

### Math Types (Partial)
- Vector2, Vector3, Vector4 struct definitions with {x,y,z,w} fields

## What's Missing (Blocking Tests)

### Critical Language Extensions
1. **Struct Methods** (§3.2) - Blocks ~40 tests
   - Parser: ✅ MethodDecl in AST, struct.methods field exists
   - Sema: ❌ Method resolution, `this` binding
   - Emit: ❌ Method call code generation
   - Example: `v.add(w).multiplyScalar(2.0)`

2. **Operator Overloading** (§5) - Blocks ~15 tests
   - Parser: ✅ `operator` keyword added
   - Sema: ❌ Overload resolution  
   - Emit: ❌ Operator call lowering
   - Example: `a + b`, `v * 2.0`

3. **Int→Float Literal Coercion** (§4A) - Blocks ~25 tests
   - Needed for: `new Vector3(1, 2, 3)` (literals without `.0`)
   - Rule: IntLit in float context with |v| ≤ 2^53 → FloatLit
   - Contexts: float params, assignments, returns, binary ops with float

### Missing Math Implementations
- All struct methods (add, multiplyScalar, dot, cross, normalize, etc.)
- Built-in operator definitions for math types
- Matrix3, Matrix4, Quaternion, Color, Euler, Ray, Box3, Sphere, RayHit types
- ~100+ methods across all types per spec

## Technical Debt

- Method bodies for synthetic `farmos:math` types need implementation strategy
- Test harness needs `run_approx` for float comparison  
- M1 regression testing not yet run
- Binary size verification (dead code elimination) not measured

## Next Steps

1. Implement struct method calls (highest impact: unblocks 40 tests)
2. Implement int→float coercion (medium impact: unblocks 25 tests)  
3. Implement operator overloading (medium impact: unblocks 15 tests)
4. Add method implementations for Vector3 (add, multiplyScalar, dot, etc.)
5. Expand to all math types per spec
6. Build `run_approx` test runner
7. Verify M1 tests still pass
8. Measure binary sizes

## Commit Log

- 5dcc2fb: M2: Fix struct println formatting with fflush for buffering
- Previous: Parser/AST support for operators and methods, synthetic module infrastructure
