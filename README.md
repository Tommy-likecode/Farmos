# Farmos M1 — `farmc`

C++17 compiler for the Farmos M1 core language. Emits C11 and links with the system C compiler.

## Build (Windows)

```powershell
cd D:\Tommy\Farmos
.\scripts\build_and_test.ps1
```

Or:

```powershell
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

## CLI

```
farmc build hello.fm -o hello.exe
farmc run hello.fm
farmc version
```

Requires a C compiler on PATH (`clang` / `gcc` from LLVM-MinGW recommended). Set `FARM_CC` to override.
