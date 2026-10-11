# Getting started with Farmos

## Prerequisites

- **Windows x64** (TommyLaptop). Linux is used for compiler development and fixture CI; published size ACs are stripped Windows PE.
- **clang or gcc** as the C backend for `farmc` (LLVM-MinGW on Windows). MSVC `cl` is unsupported.
- CMake 3.16+, a C++17 compiler for `farmc` itself.
- Local repo: `D:\Tommy\Farmos` on TommyLaptop.

## Build the compiler

```
cd D:\Tommy\Farmos
.\scripts\build_and_test.ps1
```

Or:

```
cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_C_COMPILER=clang
cmake --build build --config Release
```

The `farmc` binary is under `build\` (or `build\Release\`). The driver copies `runtime\` next to it.

Linux (developer box):

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure -LE conformance_bundle
```

## Hello world

```
function main(): int {
  println("Hello, Farmos");
  return 0;
}
```

```
farmc build hello.fm -o hello.exe
.\hello.exe
```

The same program ships as `examples/01_hello.fm` and `spec/tests/M1/001_hello.fm`. Stripped Windows size must stay ≤ 20 KiB (B-01).

Set `FARM_CC` to override the C compiler; set `FARM_RUNTIME` if `runtime/farm_rt.c` is not next to `farmc`.

## Run the test suite

```
scripts\build_and_test.ps1
```

That configures, builds, and runs default ctest (`-LE conformance_bundle`): M1–M6 fixtures plus local gates (DCE, size budgets, docs, examples, benchmarks, TEMP abort).

One fixture:

```
scripts\run_one_m1.ps1 -Farmc .\build\farmc.exe -TestsDir spec\tests\M6 -Name 001_for_continue_increments
```

Linux: `bash scripts/run_one_linux.sh build/farmc spec/tests/M6 001_for_continue_increments`.

## Docs, examples, benchmarks

| Command | What |
|---------|------|
| `scripts\build_docs.ps1` | Existence + local Markdown link check (AC-M6-08) |
| `scripts\run_examples.ps1 -Farmc … -RepoRoot …` | Build and smoke-run `examples/01`..`05` |
| `scripts\run_benchmarks.ps1 -Farmc … -RepoRoot …` | BM-a..d; CI checks exit 0 only |
| `scripts\check_size_budgets.ps1 -Farmc … -RepoRoot …` | B-01..B-07 (add `-CheckPeakRam` for B-05 on TommyLaptop) |

## Where things live

- Language / stdlib specs: `spec/M1-core.md` … `spec/M6-hardening.md`
- Fixtures: `spec/tests/M1` … `spec/tests/M6`
- Examples: `examples/`
- Benchmarks: `benchmarks/`
- This guide: [language-reference.md](language-reference.md), [porting-guide.md](porting-guide.md)

## Next steps

1. Read [language-reference.md](language-reference.md) for types, `for`+`continue`, and `T[]` aliasing.
2. Port a Three.js snippet using [porting-guide.md](porting-guide.md) — especially the position copy gotcha.
3. Run `examples/02_rotating_cube.fm` then `examples/03_cornell_path.fm`.
