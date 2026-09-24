# AC-M1 map (Rev 2)

Maps acceptance criteria AC-M1-01..11 to M1-core.md Rev 2 sections, PLAN.md, and proof tests in this tree.
Do **not** implement M2 rule OQ-M2-11 (int literal to float) under M1.

| AC | Requirement (short) | Spec (M1-core.md Rev 2) | PLAN.md | Proof |
|----|---------------------|-------------------------|---------|-------|
| AC-M1-01 | Stripped hello.exe ≤ 20 KB on Windows x64 | section 7.1 / section 7.3 size-oriented flags; AC in PLAN | M1 AC amended 2026-09-24 | `hello.fm` build + `llvm-strip`; measured size in claim |
| AC-M1-02 | ≥ 30 language tests pass via local `scripts/build_and_test.ps1` | section 8 fixtures; `spec/tests/M1/` | M1 AC | 49 `m1_*` + local tests; default `ctest -LE conformance_bundle` |
| AC-M1-03 | Clear compile errors with line/column | section 8.1 format `path:line:col: error[E0xxx]` | M1 AC | compile_error fixtures; runner asserts path/line/col/code |
| AC-M1-04 | C backend clang/gcc only; MSVC `cl` → driver config error exit 3 | section 7.1 (around L638), section 7.2 exit 3 | PLAN: clang/gcc; MSVC unsupported | `local_030_farm_cc_cl` (`FARM_CC=cl`, fake `cl.bat` marker absent) |
| AC-M1-05 | Types, control flow, functions, structs/classes | section section 2-5 language core | M1 scope | fixtures `001`-`041` family |
| AC-M1-06 | Modules: import/export, relative paths, cycles | section 3 modules; E0301-E0305 | M1 modules | `042`-`049`; local `020`/`021`/`022` |
| AC-M1-07 | Per-module scoping; private names; C mangling | section 3 visibility (Rev 2); non-exported private | (impl NOTES) | `local_020_priv_helpers`, `021`, `022`; mangled `mN_` symbols |
| AC-M1-08 | IEEE float `/` and `Infinity`/`-Infinity`/`NaN`; print rule 4 ES `Number::toString` | section 6 float; print rules | M1 | float fixtures + `local_002_float_tostring` |
| AC-M1-09 | Compound assignment evaluates non-ident lvalue once | assignment semantics | (impl) | `local_001_compound_assign_once`, `local_003_compound_field_once` |
| AC-M1-10 | E0508 at closing `}` of function/method body | section 8.3 E0508 + location rule | (impl NOTES) | `local_010_e0508_indented`, `local_011_e0508_sameline` |
| AC-M1-11 | `farmc` CLI: build/run/version/help; exit codes 0-4 | section 7.1-section 7.2 | M1 CLI | CLI smoke + exit-code fixtures; MSVC path exit 3 |

## Labels / suite layout

- Default ctest: `-LE conformance_bundle` → all `m1_*` (49) + `local_*` (no double-run of fixtures).
- Opt-in umbrella: `ctest -L conformance_bundle` or `-R m1_conformance`.
