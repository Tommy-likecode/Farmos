# Farmos M3.5 concurrency test fixtures

Sources use the M1 TypeScript-style surface, M2 math, M3 scene, and the M3.5
`parallel { task { … } … }` statement. See `spec/M3.5-concurrency.md`,
`spec/M1-core.md`, `spec/M2-math.md`, `spec/M3-scene.md`.

This file documents **M3.5-only** harness extensions. It does **not** replace
`spec/tests/README.md` and does not amend the M1/M2/M3 READMEs. All M1–M3 kinds
and headers remain valid unless stated below.

## Layout

```
spec/tests/M3.5/
  README.md                    (this file)
  NNN_name.fm                  single-file fixture
  NNN_name.expected
  NNN_name/main.fm (+ *.fm)    multi-file fixture (main file is main.fm)
  NNN_name.expected            (beside the directory, as in M1)
  022_two_renderers_png.golden.png
  _ref/                        generator + validator (reference data, NOT product code)
    gen_fixtures.py            writes every fixture; all line:col computed from anchors
    validate.py                structural self-check (ids, positions inside source, note placement)
    build_index.py             prints the fixture index table of M3.5-concurrency.md §17
```

Fixture ids `NNN` are unique (001–108). Ids are **not** grouped by outcome.

## Kinds

| kind | Meaning (M3.5 refinements in **bold**) |
|------|-----------------------------------------|
| `run` | Program compiles (exit 0), links, runs. Assert exit + exact stdout; program stderr MUST be empty. **farmc's own stderr diagnostics MUST equal exactly the listed `# warning:` lines (none listed ⇒ none allowed).** |
| `run_png` | M3 kind (`# png:` / `# sha256:`), same **farmc-stderr rule as `run`**. |
| `runtime_trap` | Program compiles and runs; exit = trap code; stderr = `# stderr:` block. **May also carry a `# stdout:` block: when present, stdout is compared exactly (it is the output flushed before the trap).** farmc stderr rule as `run`. |
| `compile_error` | `farmc build` exits 1. Assert the listed `# error:` / `# warning:` lines (see below). |

## New headers

```
# flags: <farmc build flags>     e.g. --werror  (appended to `farmc build <main.fm> -o <bin>`)
# repeat: N                      1 <= N <= 1000
# threads: W1,W2,...             list of positive integers
# diag_exact: true               compile_error only: the diagnostic set must match exactly (see below)
```

Header order: `# kind:` and `# exit:` are always the first two lines; the remaining
headers follow in the order above (`# png:`/`# sha256:` after them), then `##`
comments, diagnostics, and body blocks.

### `# repeat: N`

* Build **once**, then run the binary N times (for `compile_error`: invoke `farmc build` N times).
* **Every** run MUST satisfy all assertions of the fixture (exit, stdout, stderr).
* For `compile_error`, the complete farmc stderr text of all N invocations MUST be byte-identical
  (schedule/hash-order independence of diagnostics, M3.5 §12.4).
* No `repeat` header ⇒ N = 1.

### `# threads: W1,W2,…`

* For each `W` in the list run the binary N times with the environment variable
  `FARMOS_THREADS=W` (M3.5 §11.2) and assert the **same** expected result each time.
  Total runs = N × number of listed W.
* No `threads` header ⇒ one configuration: `FARMOS_THREADS` unset (default worker count).
* Not valid on `compile_error` fixtures.
* Rationale: observable behavior MUST NOT depend on worker count (M3.5 §11.1). `W=1` exercises the
  deadlock-freedom rule (nested/recursive blocks with a single worker).

### `# flags:`

Extra flags passed to `farmc build`. Only `--werror` is used in M3.5 fixtures (100).

## Diagnostic lines

```
# error:   [<path>:]<line>:<col>: <CODE>
# warning: [<path>:]<line>:<col>: <CODE>
# note:    [<path>:]<line>:<col>
```

* `<path>` only in multi-file fixtures (same convention as M1: the path exactly as given to `farmc`,
  e.g. `070_module_summary_conflict/main.fm`).
* The harness parses farmc's stderr (format `path:line:col: error[E0801]: …` /
  `warning[W0801]` / `path:line:col: note: …`, M3.5 §12.1) and compares
  **severity word, code, line, column** of each primary diagnostic. Message text is not compared.
* A `# note:` line binds to the **immediately preceding** `# error:`/`# warning:` line and asserts the
  position of the `note:` line that farmc prints right after that primary diagnostic. A primary
  diagnostic with no `# note:` line in the fixture means notes are not asserted for it.
* Ordering: the listed lines MUST appear in farmc's stderr in the listed order (relative order for the
  non-exact mode).
* Default for `compile_error`: **at least** the listed primary diagnostics must appear, in order; extra
  diagnostics are tolerated (as in M1). With `# diag_exact: true` the primary diagnostics printed by
  farmc MUST be exactly the listed ones, no more, no fewer, in that order.
* `--werror` fixtures list the promoted warning as `# error: L:C: W0801` (severity `error`, code
  unchanged, M3.5 §12.3).
* For `run`/`run_png`/`runtime_trap`, `# warning:` lines are asserted against **farmc build** stderr
  (the build succeeds with exit 0), then the program runs and is checked as usual.

## Comments

Lines starting with `##` are ignored **outside** `# stdout:` / `# stderr:` blocks; inside those blocks
they are literal expected output (unchanged from M1). Fixture generators put `##` comments before the
diagnostics/body, never inside a block.

## Determinism contract of fixtures

Every fixture's expected output follows from the spec alone and is independent of thread scheduling:
outputs inside tasks are only observed through per-task buffers flushed in task-index order
(M3.5 §9); conflict diagnostics are compile-time (M3.5 §6, §12); task-trap selection is by lowest task
index (M3.5 §10.2). Fixtures that would only be deterministic under one schedule do not exist.

## Fixture classes (quick map)

| Class (PM decision 5) | Fixtures |
|---|---|
| no conflict | 001–026, 029, 071, 072, 075, 078–080, 082, 084, 102, 105, 107, 108 |
| same value ⇒ **warning** W0801, exit 0 | 030, 066, 088, 090–092, 095, 097 (+064, 089, 103 combine a warning with an error; 100 = `--werror`) |
| different / unknown value ⇒ **E0801** | 031, 033–039, 044–047, 053, 058–061, 063, 064, 067–070, 074, 076, 077, 085, 086, 089, 093, 094, 096, 098, 101, 104, 106 |
| read/write ⇒ **E0802** | 032, 054, 062, 065, 073, 081, 083, 087, 103 |
| structural errors | 040–043, 048–052, 055–057 |
| runtime traps in tasks | 027, 028, 099 |
| repeat / stress | 016 (200 in-process iterations) and every fixture with `# repeat:` |
