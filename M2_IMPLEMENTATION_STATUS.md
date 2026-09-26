# M2 Math Standard Library Implementation Status

## Completed
1. ✅ Created M2 development branch: cursor/m2-math-stdlib-0e19
2. ✅ Added M2 specification and 54 test fixtures to spec/tests/M2/
3. ✅ Extended token kinds with KwOperator
4. ✅ Extended AST to support OperatorDecl and struct methods
5. ✅ Updated lexer to recognize 'operator' keyword
6. ✅ Extended parser to parse operator declarations and struct methods
7. ✅ Created runtime math scaffolding (farm_math.h/c)

## In Progress / Remaining

### Core Compiler Changes Needed
1. **Semantic Analysis** (sema.cpp)
   - [ ] Handle "farmos:math" virtual module imports
   - [ ] Create synthetic math module with all types
   - [ ] Implement operator overload resolution
   - [ ] Support struct method calls
   - [ ] Implement integer literal → float coercion (§4A of spec)
   - [ ] Add print/println/str overload resolution

2. **C Code Emitter** (emit_c.cpp)
   - [ ] Emit operator overload functions
   - [ ] Emit struct methods
   - [ ] Generate C struct definitions for math types
   - [ ] Link farm_math.c into compilation
   - [ ] Emit print/println/str implementations for math types

3. **Math Module Implementation**
   - [ ] Complete runtime C implementations for all math operations
   - [ ] Synthetic module builder for farmos:math
   - [ ] All types: Vector2/3/4, Matrix3/4, Quaternion, Color, Euler, Ray, Box3, Sphere, RayHit

### Test Infrastructure
1. **Test Runner**
   - [ ] Integrate M2 fixtures into CMake
   - [ ] Implement run_approx test kind for floating-point tolerance
   - [ ] Update PowerShell test scripts

### Acceptance Criteria (from spec)
- [ ] AC-M2-01: Hello world binary size unchanged on Windows x64
- [ ] AC-M2-02: Vector3-only program doesn't link Matrix4/Quaternion
- [ ] AC-M2-03: All 54 M2 fixtures pass
- [ ] AC-M2-04: Exact stdout for `run`, epsilon for `run_approx`
- [ ] AC-M2-05: `mesh.position.x = 1` mutates instance
- [ ] AC-M2-06: Method chaining works
- [ ] AC-M2-07: Assignment copies vectors
- [ ] AC-M2-08: User operator overloads work
- [ ] AC-M2-09: Deterministic output
- [ ] AC-M2-10: Unused imports DCE'd

## Implementation Strategy

### Phase 1: Get First Test Passing (001_vector3_print.fm)
Requires:
1. Recognize "farmos:math" import
2. Create Vector3 struct
3. Support new Vector3() and new Vector3(x,y,z)
4. Implement println(Vector3)

### Phase 2: Complete Basic Vector Operations
- Vector2, Vector3, Vector4
- Basic methods (add, sub, multiplyScalar, etc.)
- Operator overloading for vectors

### Phase 3: Matrix and Quaternion
- Matrix3, Matrix4
- Quaternion
- Complex operations

### Phase 4: Geometry and Colors
- Color (with hex conversion)
- Euler
- Ray, Box3, Sphere, RayHit
- Intersection tests

### Phase 5: Polish and Testing
- All 54 tests passing
- Size verification
- M1 regression tests

## Key Technical Challenges
1. **Virtual Module System**: farmos:math doesn't exist on disk
2. **Operator Overloading**: Complex resolution rules
3. **Struct Methods**: By-reference `this` semantics
4. **Value Semantics**: Deep copy on assignment, mutation through lvalue paths
5. **Dead Code Elimination**: Unused math must not bloat binary
6. **Integer Literal Coercion**: Context-dependent typing

## Notes
- Target platform: Windows x64 (clang/gcc only, MSVC unsupported)
- M1 baseline: commit 2398a7f, 110/110 tests passing
- Math operations use libm (transcendental functions)
- Column-major matrix storage (Three.js compatible)
