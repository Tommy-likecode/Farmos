# Farmos M6 — Size / Resource Hardening + Docs Specification

**Status:** FINAL (PM 2026-10-10: OQ-M6-01 .. OQ-M6-06 all option A)  
**Milestone:** M6 Size/resource hardening + docs  
**Depends on:** `spec/M1-core.md` (FINAL), `spec/M2-math.md` (FINAL), `spec/M3-scene.md` (FINAL), `spec/M3.5-concurrency.md` (FINAL), `spec/M4-ray.md` (FINAL), `spec/M5-physics.md` (FINAL)  
**Normative keywords:** MUST / SHOULD / MAY per RFC 2119.  
**Date:** 2026-10-10

This document specifies Farmos M6: consolidated size and DCE acceptance checks, harness clarifications, language amendments deferred from earlier milestones, documentation and example programs, benchmarks, and disposition of the parked backlog. It does **not** specify compiler, runtime, or stdlib implementation code.

Open questions: none. PM accepted option A for OQ-M6-01 .. OQ-M6-06 on 2026-10-10. Those decisions are recorded in §17. The body below is that decision, not a draft.

Related: `/workspace/farmos/PLAN.md` (M6 section), `spec/tests/M6/README.md`, `docs/`, `examples/`, `benchmarks/`.

**Target platform (PLAN.md):** Windows x64 only. Size ACs use a stripped Windows x64 PE unless noted. Memory ACs use that process's peak working set.

---

## 1. Scope and non-goals

### 1.1 In scope (M6 MUST implement, under option A)

- A single CI size-budget table covering M1–M5 artifacts (§3) and the procedures that enforce them.
- Dead-code elimination (DCE) rules for thread-start symbols when `parallel` / `renderPath` are never reached (§4).
- Harness amendments: default `run_approx` epsilon; optional golden PNG when `# sha256:` is present (§5).
- Language amendments: `continue` inside `for` runs the update clause; dynamic `T[]` assignment aliases; diagnostic split E0605 vs E0202 for bad operators (§6).
- Windows `farmc` driver robustness for ACP-sensitive paths and unusable TEMP (§7).
- Normative documentation tree, five example programs, and a docs-build command (§8, §9).
- Informative benchmarks that run exit 0 in CI (§10).
- Disposition of every parked backlog item (§11).
- Fixtures under `spec/tests/M6/` (§15).

### 1.2 Explicit non-goals

| Not in M6 | Note |
|-----------|------|
| Changing M1–M5 milestone markdown **bodies** | Amendments live in this file (§18); poteto / eggtooth apply product fixes against M6 |
| New graphics, physics, or concurrency features | M3–M5 stay feature-complete |
| GPU backend, windowing, package manager | PLAN stretch / out of scope |
| Timing goldens for benchmarks | benches MUST run; wall times are informative only |
| Folding unresolved M7+ work | none scheduled |
| Rewriting `docs/使用手册.md` | keep as informative Chinese sibling (§8.5, OQ-M6-06) |

### 1.3 Design constraints

- PLAN M6: size budget CI check, benchmarks, language reference, getting-started guide, 5 example programs; AC: all budgets enforced in CI; docs build.
- Hello world and per-milestone size budgets from M1–M5 remain in force; M6 consolidates them into one checklist.
- M2 §3.7 copy-vs-alias MUST appear in the porting guide (carry-forward from OQ-M2-12).
- M1 `T[]` assignment copy-vs-alias was unspecified; M6 decides (OQ-M6-02).
- No new runtime trap codes unless noted. Prefer reuse of existing diagnostics.

---

## 2. Relationship to prior milestones

- M6 does **not** re-open M3–M5 rendering or physics algorithms.
- Product fixes required by §4–§7 and §11 MAY touch `farmc`, the C backend, DCE roots, harness scripts, and docs; they MUST NOT change published M3/M4 golden SHA-256 values or M5 oracle numbers except where an RD explicitly refreshes a known-stale fixture (none remaining after M5 fixture fixes).
- Spec markdown for M1–M5 is not edited; normative amendments are listed in §18 and take effect with M6 FINAL.

---

## 3. Size budget CI table (normative)

All sizes are **stripped Windows x64 PE** file sizes unless noted. Build with the project’s release flags (`-Os`, LTO, `--gc-sections` as used by `scripts\build_and_test.ps1` / `farmc`), then `strip`. Measure with the PE file size on disk.

| ID | Artifact | Budget | Source AC |
|----|----------|--------|-----------|
| B-01 | `spec/tests/M1/001_hello.fm` (no graphics / physics / path / `parallel`) | ≤ **20 KiB** (20480 bytes) | M1, M2, M3.5, M4, M5 |
| B-02 | M3 rotating-cube demo (`examples/02_rotating_cube.fm` or `spec/tests/M3/003_rotating_cube.fm` shape) | ≤ **96 KiB** (98304 bytes) | AC-M3-10 |
| B-03 | M4 glass+mirror demo (`examples/04_glass_mirror.fm` or `spec/tests/M4/009_glass_and_mirror.fm` shape) | ≤ **300 KiB** (307200 bytes) | AC-M4-04 |
| B-04 | M5 `S_demo − S_scene` (stacking/bounce demo minus scene-only hello) | ≤ **64 KiB** (65536 bytes) | AC-M5-05 |
| B-05 | M4 Cornell-shaped 800×600 `renderPath` peak working set | ≤ **64 MiB** (67108864 bytes) | AC-M4-06 |
| B-06 | Hello that `import`s unused `farmos:math` names but never references them | ≤ **20 KiB** | M2 AC (reinstate CI gate) |
| B-07 | Program that never executes `parallel` and never calls `renderPath` | no thread-start imports (§4) | M3.5 §15.1, M4 §15 |

**Informative measurement note (not a budget):** a past M3 rotating-cube build measured **33792 bytes** stripped. That figure is historical context only; the normative ceiling remains ≤ 96 KiB (B-02).

### 3.1 Measurement procedures

- **Stripped size (B-01..B-04, B-06):** same as M4 §15.2 / M5 §14.2 — `farmc build`, link with project flags, `strip`, measure PE bytes.
- **Peak RAM (B-05):** same as M4 §15.3 — `PeakWorkingSetSize` via `GetProcessMemoryInfo` (or equivalent) for one Cornell-shaped `setSize(800,600)` / `setSamples(1)` / `setMaxBounces(4)` / `renderPath` process.
- **Thread-start symbols (B-07):** §4.2.

### 3.2 CI enforcement

M6 MUST provide a documented Windows command (e.g. `scripts\check_size_budgets.ps1`) that:

1. Builds each artifact in the table (or a published equivalent path under `examples/`).
2. Fails (non-zero exit) if any size / RAM / symbol check fails.
3. Prints each measured value and its budget on stdout.

`scripts\build_and_test.ps1` (or the project’s primary CI entry) MUST invoke that check (or an equivalent integrated step) so eggtooth can treat “CI green” as AC evidence.

No `.fm` fixture can assert PE size; B-01..B-07 are **eggtooth / script ACs** (AC-M6-01, AC-M6-02).

---

## 4. DCE and thread-start symbols

### 4.1 Rule (OQ-M6-05 A)

A program that **never executes** a `parallel` statement and **never calls** `Renderer.renderPath` (and does not take the address of / otherwise reference `renderPath` as a reachable entry) MUST NOT:

- link the M3.5 worker-pool object files;
- link the M4 tile-pool / path-tracer worker entry;
- import or define thread-start symbols, including at least:
  - Windows: `_beginthreadex`, `_beginthread`, `CreateThread`, `WaitForMultipleObjects` (as imports pulled only by the threading runtime);
  - POSIX (if ever built): `pthread_create`, `thrd_create`.

This restates and strengthens M3.5 §15.1 and M4 §15.1 for CI.

### 4.2 Check procedure (Windows x64)

After building and stripping a qualifying program (at minimum `001_hello.fm` and a scene-only program that only calls `render`, never `renderPath`):

1. Run `dumpbin /IMPORTS <pe>` (MSVC tools) **or** `llvm-nm` / `nm` on the PE / underlying objects as available on TommyLaptop.
2. Fail if any of the thread-start names in §4.1 appear as **imported** symbols attributable to the Farmos threading runtime.
3. Document the exact command line in `scripts\check_size_budgets.ps1` (or a sibling script) so the check is reproducible.

Product DCE roots MUST be fixed if a non-`parallel` / non-`renderPath` program still pulls `_beginthreadex` (known parked defect).

---

## 5. Harness amendments

### 5.1 `run_approx` default epsilon (OQ-M6-03 A)

Amends `spec/tests/M2/README.md` and M2-math.md §16:

- When `# kind: run_approx` and **`# epsilon:` is omitted**, the harness MUST use absolute tolerance **`1e-10`**.
- Fixtures MAY set `# epsilon:` to a tighter or looser value; when present, that value wins.
- Existing fixtures that already set `# epsilon:` are unchanged.
- Rationale: product and M5 oracles have been stable at 1e-10; forcing 1e-12 would require mass fixture refresh without AC benefit (option B rejected unless PM chooses it).

### 5.2 Golden PNG vs `# sha256:` (RD-M6-07)

For `kind: run_png` (M3 / M4 / M3.5):

1. If `# sha256:` is present, the harness MUST hash the output PNG and compare (case-insensitive hex) to that value. **This is the required gate.**
2. A sibling `NNN_name.golden.png` is **optional**. When present, the harness SHOULD also byte-compare the output to it.
3. If both are present and disagree with each other, the fixture pack is ill-formed; poteto / farmspec MUST fix the pack so sha256 matches `_ref/goldens_sha256.txt` and the golden bytes.
4. Published claims and docs MUST quote hashes **exactly** as in `spec/tests/M3/_ref/goldens_sha256.txt` / `spec/tests/M4/_ref/goldens_sha256.txt`. Known eggtooth note: a claim typo’d a black-clear hash (`7cb…` vs correct `7bbd50eb08c7f094…` manifest entry). Any such typo MUST be corrected; the manifest is authoritative.

### 5.3 `run` vs `run_approx` when bit-identical (RD-M6-08)

Fixtures that print decimals known to be bit-identical to the oracle MAY use `kind: run`. Existing `run_approx` fixtures NEED NOT be flipped unless a refresh is already underway. Harness MUST NOT auto-convert kinds. No AC semantic change for M5.

### 5.4 M2 README sync

`spec/tests/M2/README.md` MUST state the §5.1 default (this revision updates that file). Suggested-default prose that said “1e-12” is amended to: default when omitted is **1e-10**; fixtures MAY choose 1e-12 for well-conditioned cases.

---

## 6. Language amendments

### 6.1 `continue` inside `for` (OQ-M6-01 A)

Amends M1-core.md §5.10 (normative via this document):

For a `for (init; condition; update) { … }` loop:

1. `break;` leaves the loop as today.
2. `continue;` MUST:
   - skip the remainder of the current iteration body;
   - **evaluate the `update` clause** (if present) exactly as at the end of a normal iteration;
   - then re-evaluate `condition` and proceed or exit accordingly.

This matches C, TypeScript, and Java. The prior infinite-loop bug (update skipped) is a product defect relative to this rule; the product now increments and MUST keep doing so.

`continue` inside `while` does **not** invent an update clause (unchanged).

Fixture `001_for_continue_increments` proves the update runs.

### 6.2 Dynamic `T[]` assignment aliases (OQ-M6-02 A)

Amends M1-core.md §4.5 for **dynamic** arrays `T[]` only:

| Operation | Semantics |
|-----------|-----------|
| `let b: T[] = a;` where `a: T[]` | **Alias** — `b` and `a` refer to the same dynamic buffer |
| Pass `T[]` as a parameter | Alias (callee mutations of length / elements / `push` are visible to the caller) |
| Return `T[]` | Alias (caller receives the same buffer reference) |
| `a[i] = v` | Writes element `i`; does not rebind the array |
| `push(a, v)` | Mutates the shared buffer |
| Fixed `T[N]` assignment | **Unchanged** — still copies all elements (value semantics) |

Rationale: reference semantics like `class`; deep-copy-on-assign (option B) is expensive and surprising next to `push`. Element values still follow their type’s copy/alias rules (e.g. `int` copies; `Mesh` element stores a reference).

Initializer from an array **literal** still allocates a fresh buffer and copies elements into it (`let a: int[] = [1, 2, 3];`).

Fixtures `003`–`006` cover alias assign, param, return, and contrast with fixed arrays.

### 6.3 Diagnostics: E0605 vs E0202 (OQ-M6-04 A)

Amends clarification of M2-math.md §5.2 / §5.6 and M1 E0202:

| Situation | Code |
|-----------|------|
| `operator` declaration whose operator token is a **recognized operator** that is not in the overloadable set (`!`, `=`, `<`, `<=`, `>`, `>=`, `&&`, `\|\|`, `[]`, `()`, compound assigns, etc.) | **E0605** `operator {op} is not overloadable` at the operator token |
| Bare / ill-formed syntax involving `!` or `=` outside an `operator` declaration (e.g. unexpected token in expression position) | **E0202** (unchanged) |
| Overload of a built-in primitive operator | **E0601** (unchanged) |

Product defect to fix: emitting **E0202** for `operator !` / `operator =` / other non-overloadable `operator …` forms MUST become **E0605**.

M2 fixture `039_operator_not_overloadable` (`operator <`) remains authoritative for E0605. M6 fixtures `007` / `008` cover `!` and `=`.

### 6.4 Conformance summary / stale status files (RD-M6-05)

- M1 fixture `039_mixed_arith_error` is **E0402** mixed arith — not an operator test. Any `m1_conformance` summary that lists “039” under operators MUST be corrected.
- M2 fixture `039_operator_not_overloadable` is the E0605 operator case.
- If `M2_IMPLEMENTATION_STATUS.md` (or similarly named stale status) exists in the tree, M6 MUST **delete** it or replace it with a one-line pointer to `spec/M2-math.md`. Prefer delete. (Not present on the farmspec box copy at DRAFT time; poteto MUST still ensure the product tree complies.)

---

## 7. Windows `farmc` driver rules (RD-M6-06)

### 7.1 ACP-sensitive paths

On Windows, `FARM_CC` and argv paths passed to the C compiler / linker MUST either:

- be representable in the active ANSI code page (ACP) when narrow Win32 APIs are used; **or**
- be passed via **wide** Win32 APIs (`CreateProcessW`, etc.) so non-ACP paths work.

Silent truncation or wrong-path compiles are forbidden. Prefer wide APIs.

### 7.2 Unusable TEMP

If the temporary directory used for intermediate `.c` / object files is missing, not writable, or otherwise unusable, `farmc` MUST:

- exit with code **1**;
- print exactly this line on stderr (additional lines MAY follow):  
  `error: temporary directory unavailable`
- not leave a partial output binary as if the build succeeded.

(No new E0xxx required; this is a driver environment failure surfaced as a compile-time abort.)

### 7.3 Stale comments / scripts

Stale `.cmd` / PowerShell comments that contradict current driver behavior MUST be fixed as part of M6 cleanup (product tree). No fixture required beyond the Windows AC checklist (AC-M6-10).

### 7.4 Verification

Eggtooth verifies on TommyLaptop (Windows):

- Building under a writable TEMP succeeds.
- With TEMP pointed at a non-writable / missing directory, `farmc build` exits 1 and stderr contains the message in §7.2.
- A path containing characters outside ACP either works (wide APIs) or fails with a clear error (not a cryptic assembler failure). Exact non-ACP sample path is chosen by eggtooth.

These checks are **Windows-only**; they are not automated on the Linux farmspec box.

---

## 8. Documentation requirements

### 8.1 Language reference (normative outline)

Path: `docs/language-reference.md` (English, normative user docs).

MUST cover, at outline level (poteto fills prose):

1. Introduction and `farmc` CLI.
2. Lexical structure, types (`int`/`float`/`bool`/`string`/`T[N]`/`T[]`/`struct`/`class`), modules.
3. Control flow including **`for` + `continue` update** (§6.1).
4. Arrays: fixed copy vs dynamic alias (§6.2).
5. Diagnostics overview (E0xxx families) and runtime traps 101+.
6. `farmos:math` surface and §3.7 copy gotcha pointer.
7. `farmos:scene` (M3) + path tracing (M4) summary.
8. `parallel` / conflict model (M3.5) summary.
9. `farmos:physics` (M5) summary + scene sync.
10. Size / DCE expectations (pointer to this spec §3–§4).

Skeleton shipped under `docs/language-reference.md`.

### 8.2 Getting started

Path: `docs/getting-started.md`.

MUST cover: install / build `farmc` on Windows; hello world; `scripts\build_and_test.ps1`; where fixtures and examples live.

### 8.3 Porting guide

Path: `docs/porting-guide.md`.

MUST document at least:

1. Three.js → Farmos scene mapping (Mesh, materials, lights, `render` vs `renderPath`).
2. **M2 §3.7 copy gotcha** — `let p = mesh.position; p.x = …` does **not** mutate the mesh; use field paths or write-back.
3. Physics sync: `RigidBody.setObject(mesh)` and `world.step()` pose write-back (M5).
4. Integer literals in float contexts (M2 §4A).

### 8.4 Docs build AC

A documented Windows command (e.g. `scripts\build_docs.ps1`) MUST exit 0 when docs are well-formed. Minimum bar:

- required files in §8.1–§8.3 exist;
- Markdown link check or equivalent smoke (broken local links → non-zero).

Full static-site generation is MAY; lint + existence + link check is enough for AC-M6-08.

### 8.5 Chinese handbook (OQ-M6-06 A)

`docs/使用手册.md` already exists and MUST **not** be deleted. English docs in §8.1–§8.3 are **normative**. The Chinese handbook is **informative**; poteto MAY update it to mention M4–M6 but M6 ACs do not require parity.

---

## 9. Example programs

Normative paths under `/workspace/farmos/examples/` (also `D:\Tommy\Farmos\examples\` on TommyLaptop):

| File | Milestone shape | Size budget |
|------|-----------------|-------------|
| `01_hello.fm` | M1 hello | B-01 |
| `02_rotating_cube.fm` | M3 rotating cube → PNG | B-02 |
| `03_cornell_path.fm` | M4 Cornell `renderPath` | counts toward B-03 family / hero docs |
| `04_glass_mirror.fm` | M4 glass + mirror | B-03 |
| `05_stacking_bounce.fm` | M5 stacking + bounce | used for B-04 / demos |

Each MUST:

- compile with `farmc build`;
- exit 0 on a smoke run (PNG demos write a PNG; physics demo may omit PNG);
- stay within the applicable budget when measured as in §3.

Skeletons / README are shipped by farmspec; poteto fills production-quality content (may start from the corresponding fixture sources).

---

## 10. Benchmarks

Path: `benchmarks/` (scripts + small `.fm` drivers).

MUST provide a Windows entry (e.g. `scripts\run_benchmarks.ps1` or `benchmarks\run.ps1`) that builds and runs:

| ID | Bench | Notes |
|----|-------|-------|
| BM-a | Empty loop | tight `for`/`while` counting |
| BM-b | M2 vector ops | add / normalize / mat4 multiply chain |
| BM-c | M5 600-step stack | shape of `006_sphere_stack_600` |
| BM-d | M4 tiny path render | small `renderPath` (e.g. 32×32, few samples) |

CI MUST assert each bench process **exit code 0**. Printed timings are **informative** (no golden times, no flaky thresholds).

---

## 11. Backlog disposition

| # | Item | Disposition |
|---|------|-------------|
| 1 | `for` + `continue` update | **OQ-M6-01** → amend §6.1; fixture `001` |
| 2 | Windows DCE / `_beginthreadex` | **OQ-M6-05** → §4; CI symbol check |
| 3 | `run_approx` epsilon 1e-10 | **OQ-M6-03** → §5.1; update M2 README |
| 4 | M3 leftovers (gimbal, 33792 note, unused math import, m1_039) | **RD-M6-04**: gimbal fixture `009`; informative 33792 note §3; B-06 unused-math gate; m1_039 naming clarified §6.4 |
| 5 | E0202 vs E0605; conformance 039; stale M2 status | **OQ-M6-04** + **RD-M6-05** |
| 6 | FARM_CC / ACP / TEMP / stale comments | **RD-M6-06** §7 |
| 7 | M4/M3 sha typo; optional golden | **RD-M6-07** §5.2 |
| 8 | eggtooth M5 harness kind / changelog sync | **RD-M6-08** §5.3; verify box `M5-physics.md` history matches shipped (no AC change) |
| — | `T[]` assign copy vs alias | **OQ-M6-02** §6.2 |
| — | Porting guide §3.7 | **done-in-M6** §8.3 (required) |

Nothing in this table is silently dropped.

---

## 12. Traps and diagnostics

### 12.1 New runtime traps

None.

### 12.2 New / clarified compile diagnostics

| Code | Role in M6 |
|------|------------|
| E0605 | Non-overloadable `operator` forms (§6.3) — clarified; product fix if E0202 was emitted |
| E0202 | Unchanged for true syntax errors |
| (driver message) | `error: temporary directory unavailable` (§7.2) — not an E0xxx |

No E09xx / E10xx series added in M6.

---

## 13. Acceptance criteria

| ID | Criterion |
|----|-----------|
| AC-M6-01 | Size budget script enforces B-01..B-04 and B-06; fails CI on violation (§3) |
| AC-M6-02 | B-05 peak RAM check documented and runnable on Windows; B-07 thread-start symbol check passes for hello and non-path scene programs (§3, §4) |
| AC-M6-03 | Fixtures `001`–`015` in §15 pass under their harness kinds |
| AC-M6-04 | `continue` in `for` evaluates update (§6.1); fixture `001` |
| AC-M6-05 | `T[]` assignment / param / return aliases (§6.2); fixtures `003`–`006` |
| AC-M6-06 | `operator !` and `operator =` → E0605 (§6.3); fixtures `007`, `008` |
| AC-M6-07 | Five examples under `examples/01`..`05` build and smoke-run (§9) |
| AC-M6-08 | Docs files §8.1–§8.3 exist; `scripts\build_docs.ps1` (or documented equivalent) exits 0 |
| AC-M6-09 | Benchmarks BM-a..BM-d run exit 0 in CI (§10) |
| AC-M6-10 | Windows driver TEMP / path checklist (§7.4) verified by eggtooth on TommyLaptop |
| AC-M6-11 | Porting guide documents M2 §3.7 and physics sync (§8.3) |
| AC-M6-12 | Harness default epsilon 1e-10 when omitted; M2 README updated (§5.1) |
| AC-M6-13 | Golden optional / sha256-required rule (§5.2); published hashes match manifests |
| AC-M6-14 | Backlog table §11 fully disposed (OQ, RD, or done-in-M6); no silent drops |
| AC-M6-15 | `docs/使用手册.md` retained; English docs normative (OQ-M6-06 A) |

---

## 14. Test harness

See `spec/tests/M6/README.md`.

Kinds used: `run`, `run_approx`, `compile_error`. No `runtime_trap` and no `run_png` in the M6 DRAFT set (size/RAM are script ACs).

---

## 15. Test index

| ID | Name | Kind | Notes |
|----|------|------|-------|
| 001 | `001_for_continue_increments` | run | `continue` runs `for` update |
| 002 | `002_for_continue_sum` | run | skipped body iterations; update still counts |
| 003 | `003_tarray_assign_aliases` | run | `b = a` then `push` visible in both |
| 004 | `004_tarray_param_aliases` | run | callee `push` visible to caller |
| 005 | `005_tarray_return_aliases` | run | returned array aliases local |
| 006 | `006_fixed_array_copies` | run | `T[N]` assign still copies |
| 007 | `007_operator_bang_not_overloadable` | compile_error | E0605 for `operator !` |
| 008 | `008_operator_assign_not_overloadable` | compile_error | E0605 for `operator =` |
| 009 | `009_euler_gimbal_near_pitch` | run_approx | pitch ≈ π/2; quat finite / synced |
| 010 | `010_while_continue_no_extra` | run | `while`+`continue` does not invent update |
| 011 | `011_tarray_literal_fresh` | run | literal init is a fresh buffer |
| 012 | `012_operator_lt_not_overloadable` | compile_error | E0605 for `operator <` (M6 mirror of M2 039) |
| 013 | `013_for_continue_nested` | run | `continue` inner `for` only |
| 014 | `014_porting_position_copy_gotcha` | run | M2 §3.7 demo (mesh.position unchanged) |
| 015 | `015_tarray_element_write` | run | element write through alias |

**Fixture count:** **15** — 11 `run`, 1 `run_approx`, 3 `compile_error`.

Eggtooth-only (no `.fm`): size B-01..B-07, TEMP/ACP checklist.

---

## 16. Open questions

**None.** PM decided OQ-M6-01 .. OQ-M6-06, all option A, on 2026-10-10. See §17.

---

## 17. Resolved decisions

PM 2026-10-10 accepted option A for every OQ below. The RD rows are housekeeping those questions did not cover.

| ID | Decision |
|----|----------|
| OQ-M6-01 | `continue` inside `for` evaluates the update clause, then re-tests the condition (C/TS/Java). |
| OQ-M6-02 | Dynamic `T[]` assignment / parameter / return **aliases**; fixed `T[N]` still copies. |
| OQ-M6-03 | `run_approx` default epsilon when `# epsilon:` omitted is **1e-10**. |
| OQ-M6-04 | Non-overloadable `operator !` / `operator =` (and peers) → **E0605**; bare syntax errors stay **E0202**. |
| OQ-M6-05 | CI fails if hello / non-`parallel` / non-`renderPath` programs import thread-start symbols (`_beginthreadex` / equivalent). |
| OQ-M6-06 | English docs normative (`language-reference` / `getting-started` / `porting-guide`); keep `docs/使用手册.md` informative. |
| RD-M6-04 | Gimbal near-pitch fixture `009`; cube **33792 B** informative only; unused `farmos:math` import size gate B-06; clarify m1_039 vs m2_039 naming in summaries |
| RD-M6-05 | Fix product E0202→E0605 for non-overloadable `operator` forms; correct conformance lists; delete/replace stale `M2_IMPLEMENTATION_STATUS.md` |
| RD-M6-06 | Windows FARM_CC/argv ACP-safe or wide APIs; TEMP unusable → exit 1 + `error: temporary directory unavailable`; fix stale script comments |
| RD-M6-07 | sha256 header is required gate; sibling golden optional (SHOULD byte-compare); fix any published hash typos to match `_ref/goldens_sha256.txt` |
| RD-M6-08 | MAY use `run` when bit-identical; do not mass-flip existing `run_approx`; keep M5 changelog/history consistent with shipped box copy |
| RD-M6-09 | Porting guide MUST document M2 §3.7 (carry-forward OQ-M2-12) |
| RD-M6-10 | Five example filenames in §9 are normative; poteto owns final demo content |
| RD-M6-11 | Benchmarks are smoke-timed only; CI checks exit 0 |
| RD-M6-12 | No new runtime trap codes in M6 |

---

## 18. Amendments relative to M1–M5

M1–M5 markdown files are not edited; the following become normative with M6 FINAL:

| # | Amendment |
|---|-----------|
| 1 | M1 §5.10: `continue` in `for` evaluates `update` then re-tests condition (§6.1) |
| 2 | M1 §4.5: dynamic `T[]` assign/param/return **alias**; fixed `T[N]` still copies (§6.2) |
| 3 | M2 §5.6 / harness: non-overloadable `operator` → E0605; `run_approx` default epsilon 1e-10 (§5.1, §6.3) |
| 4 | M3/M4 harness: `# sha256:` required when present; golden file optional (§5.2) |
| 5 | M3.5/M4 size: CI symbol check for thread-start imports (§4) |
| 6 | M2 README epsilon prose updated to match §5.1 |
| 7 | Docs + examples + benchmarks as §8–§10 |

---

## 19. Document history

- 2026-10-10: **DRAFT**. Option A throughout for OQ-M6-01..06. Fixtures 001–015. Backlog 1–8 + T[] + porting disposed.
- 2026-10-10: **FINAL**. PM accepted OQ-M6-01 .. OQ-M6-06, all option A. Open questions cleared.
