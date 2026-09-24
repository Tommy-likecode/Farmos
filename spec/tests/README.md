# Farmos language test fixtures

Sources use the M1 TypeScript-style surface (`function`, `new`, `this`, `: Type`). See `spec/M1-core.md`.

M1 fixtures include hex integer literals and IEEE float `/0` (print `Infinity`/`-Infinity`/`NaN`); integer `/0` remains `runtime_trap` exit 101.

## Layout

```
spec/tests/
  README.md          (this file)
  M1/
    NNN_name.fm           source under test (single-file)
    NNN_name.expected     expected result
    NNN_name/             multi-file test directory (optional)
      main.fm             entry (main module)
      *.fm                other modules
      NNN_name.expected   OR expected beside the directory as NNN_name.expected
```

For multi-file tests (e.g. `022_modules`), the runner MUST use `022_modules/main.fm` as the main file, and the expected file is `022_modules.expected` next to the directory (i.e. `spec/tests/M1/022_modules.expected`).

## Expected file format

Lines are UTF-8 text. A fixture begins with header fields, then optional body sections.

### Headers (required first lines)

```
# kind: run | compile_error | runtime_trap
# exit: <integer>
```

| kind | Meaning |
|------|---------|
| `run` | Program compiles, links, runs. Assert process exit code and stdout. stderr MUST be empty. |
| `compile_error` | `farmc build` fails with exit code 1. Assert at least the listed diagnostic(s). stdout of the program is N/A. |
| `runtime_trap` | Program compiles and runs; process exits with trap code; stderr matches trap line(s). |

Optional headers:

```
# stderr_exact: true
```

When `stderr_exact` is true (default for `runtime_trap`), stderr must match the `# stderr:` block exactly. For `run`, stderr must be empty (no `# stderr:` block).

### Body sections

**Stdout** (`run` only):

```
# stdout:
<meta: empty line after header starts the block>
literal lines...
# end
```

Everything between `# stdout:` and `# end` is compared **exactly** to process stdout (including newlines). If the program prints nothing, use:

```
# stdout:
# end
```

**Stderr** (`runtime_trap`):

```
# stderr:
runtime error: division by zero
# end
```

**Compile diagnostics** (`compile_error`):

One or more:

```
# error: <line>:<col>: E0xxx
```

The runner MUST verify that `farmc` emits a diagnostic whose line, column, and code match. The message text SHOULD match the template in `M1-core.md` but tests only *require* line, column, and code.

Column numbers are **1-based Unicode scalar values** (see M1-core.md §8).

### Comments

Lines starting with `##` (two hashes) are human comments and MUST be ignored by the harness.

### Example

```
# kind: run
# exit: 0
# stdout:
Hello, Farmos
# end
```

## Harness contract (informative)

```
farmc build <main.fm> -o <tmp_bin>   # expect exit 0 for run/runtime_trap; 1 for compile_error
<tmp_bin>                             # capture exit, stdout, stderr
```

Compare against `.expected`. No implementation ships in this tree; eggtooth/CI will wire the harness.
