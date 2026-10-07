# Farmos

## M3 — `farmos:scene`

Three.js-like scene graph and CPU rasterizer (`import { … } from "farmos:scene"`). Spec: `spec/M3-scene.md`. Fixtures: `spec/tests/M3/` (`run_png` goldens plus API tests).

Unused `farmos:scene` imports are dead-code eliminated (AC-M3-01 / AC-M3-11): they must not change hello size or M1/M2 compile flags / linked runtime. Scene programs additionally pass `-ffp-contract=off` and link `farm_math.c` + `farm_scene.c`.

Linux: `scripts/run_m3_linux.sh build/farmc` after a CMake build. DCE/size gate: `python3 scripts/test_unused_scene_dce.py build/farmc .` (Windows: `scripts/test_unused_scene_dce.ps1`).

---

# Farmos M1 — `farmc`

C++17 compiler for the Farmos M1 core language. Pipeline: lexer → parser → AST → semantic check → emit portable C11 → invoke system C compiler.

## Build (Windows)

```powershell
cd D:\Tommy\Farmos
# Ensure clang/gcc (LLVM-MinGW) and CMake are on PATH
.\scripts\build_and_test.ps1
```

Or:

```powershell
cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_C_COMPILER=clang
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

## CLI

```
farmc build hello.fm -o hello.exe
farmc run hello.fm
farmc version
```

Set `FARM_CC` to override the C compiler; set `FARM_RUNTIME` to the `runtime/` directory if needed.

## C backend requirement (important)

The C code emitted by `farmc` uses **GNU statement-expressions** (`({ ... })`) and therefore requires **clang or gcc** (e.g. LLVM-MinGW on Windows) as the C backend.

**MSVC `cl` is not supported** as the C backend for generated code. (You may still build the `farmc` compiler itself with other toolchains, but linking user programs goes through clang/gcc.)

See `docs/NOTES.md` for more implementation notes.

## Tests

- Spec conformance: `spec/tests/M1/` (49 fixtures) via ctest `m1_*`; umbrella `m1_conformance` is opt-in (`ctest -L conformance_bundle`)
- Local regressions: `tests/local/` via ctest `local_*`
