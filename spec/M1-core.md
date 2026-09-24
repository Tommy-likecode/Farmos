# Farmos M1 — Core Language Specification

**Status:** FINAL (PM-approved 2026-09-24)  
**Milestone:** M1 Core language  
**File extension:** `.fm`  
**Compiler CLI:** `farmc`  
**Normative keywords:** MUST / SHOULD / MAY per RFC 2119.

This document specifies the Farmos M1 language surface, semantics, CLI, diagnostics, runtime traps, acceptance criteria, and the M1 test index. It does **not** specify compiler implementation.

Related: `/workspace/farmos/PLAN.md`. Operator overloading is **deferred to M2** (see §1.2).

---

## 1. Scope and non-goals

### 1.1 In scope (M1 MUST implement)

- Lexer, parser, AST, type check, Farmos IR, C11 emission, invoke system C compiler.
- Types: `int`, `float`, `bool`, `string`, fixed arrays, dynamic arrays, `struct`, `class`, functions.
- Control flow: `if`/`else`, `while`, `for`, `break`, `continue`, `return`.
- Modules: `import` / `export` across `.fm` files.
- Built-ins: `print`, `println`, string length/concat (minimal).
- `farmc build` producing a native executable on Linux and Windows.
- Diagnostics with path, 1-based line/column, and error codes.
- Runtime traps for **integer** division by zero and array bounds (no GC). Float `/` by zero is IEEE 754 (no trap).
- **TypeScript-style surface syntax** so M3 Three.js-style APIs port with near-identical structure (PLAN.md M3 AC).

### 1.2 Explicit non-goals (M1 MUST NOT require)

| Deferred | Target |
|----------|--------|
| Operator overloading | **M2** |
| `Vector2/3/4`, matrices, quaternions, Color, Ray, Box3, Sphere | M2 |
| Generics / templates | post-M1 |
| Closures / first-class function values | post-M1 |
| Inheritance, interfaces, virtual dispatch | post-M1 |
| `null`, nullable references, `Optional<T>` | post-M1 (see §4.10) |
| Pattern matching, enums/sum types | post-M1 |
| Async, threads, atomics | post-M1 |
| Unicode identifiers beyond ASCII | post-M1 (identifiers ASCII; string *contents* UTF-8) |
| `switch` / `match` | post-M1 |
| Slices distinct from dynamic arrays | post-M1 |
| User-defined destructors / general `dispose` | post-M1 (revisit in M3; see §4.7, Resolved OQ-M1-17) |
| Package manager / versioned crates | post-M1 |
| Automatic Semicolon Insertion (ASI) | out of M1 (semicolons required; Resolved OQ-M1-02) |

### 1.3 Design constraints (from PLAN.md)

- Statically typed; **JS/TS-like surface** so Three.js-style code reads naturally (M3 AC: near-identical structure).
- No GC: program-lifetime arena for class instances (§4.7); value types for structs/arrays/primitives.
- Runtime/stdlib linked only if used (`--gc-sections`).
- Hello-world stripped binary ≤ 20 KB on Linux x64.

---

## 2. Lexical structure

### 2.1 Source encoding

- Source files MUST be UTF-8 (no BOM required; a leading UTF-8 BOM MUST be accepted and ignored).
- Ill-formed UTF-8 MUST be a lexical error `E0001` at the offending byte offset (column computed per §8.2).

### 2.2 Whitespace

Whitespace is Space U+0020, Tab U+0009, CR U+000D, LF U+000A. Whitespace separates tokens and is otherwise insignificant. Line breaks: CR LF → one linebreak; lone CR → one linebreak; LF → one linebreak.

### 2.3 Comments

```
// line comment to end of line
/* block comment, non-nesting */
```

Unterminated `/*` MUST be `E0002`. Block comments MUST NOT nest.

### 2.4 Identifiers

```
identifier ::= (letter | "_") (letter | digit | "_")*
letter     ::= "a"…"z" | "A"…"Z"
digit      ::= "0"…"9"
```

Identifiers are ASCII-only in M1. Identifiers MUST NOT be keywords.

### 2.5 Keywords

Reserved (MUST NOT be used as identifiers):

```
bool         break        class       const        constructor
continue     else         export      false        float
for          function     if          import       int
let          new          return      string       struct
this         true         void        while
```

**Not** keywords: `fn` (unused; not reserved), `from` (contextual only after `import … from`), `self` (unused; not reserved).

Future-reserved (MUST reject as identifiers with `E0003`):

```
as  async  await  enum  extends  implements  in  interface
match  mut  null  optional  private  protected  public  static
super  switch  type  typeof  var  yield
```

### 2.6 Integer literals

```
int_lit     ::= decimal_lit | hex_lit
decimal_lit ::= digit+
hex_lit     ::= ("0x" | "0X") hex_digit+
hex_digit   ::= digit | "a"…"f" | "A"…"F"
```

- **Decimal** (`digit+`): value MUST fit in signed 64-bit two's complement range \([-2^{63}, 2^{63}-1]\); otherwise `E0101` at the start of the literal.
- **Hexadecimal** (`0x` / `0X` + one or more hex digits, case-insensitive): type is `int`. The numeric value MUST satisfy \(0 \leq v \leq 2^{63}-1\); otherwise `E0101` at the start of the literal (column of the leading `0`). Rationale: Three.js-style color literals such as `0xff0000`.
- Digit separators `_` are **not** allowed in decimal or hex (consistent).
- `0x` or `0X` with **no** following hex digit is a lexical error `E0104` at the `0` of the prefix.
- Binary (`0b`) and octal literals are **not** in M1 (deferred).
- A leading `0` on a decimal literal is still decimal (e.g. `077` is seventy-seven), not octal.

### 2.7 Float literals

```
float_lit ::= digit+ "." digit+ [exponent]
            | digit+ exponent
exponent  ::= ("e" | "E") ["+" | "-"] digit+
```

- Type is `float` (IEEE 754 binary64).
- Out-of-range at parse MUST be `E0102`.
- A bare `digit+ "."` or `"." digit+` is NOT valid (digits required on both sides of `.` unless using exponent form `1e3`).

### 2.8 String literals

```
string_lit ::= '"' {string_char | escape} '"'
string_char ::= any Unicode scalar except '"' , '\' , CR, LF
escape ::= "\\" | "\"" | "\n" | "\r" | "\t" | "\0"
         | "\u{" hex{1,6} "}"
```

- Strings are immutable UTF-8 (§4.4).
- `\u{...}` MUST be a valid Unicode scalar (not surrogate); else `E0103`.
- Adjacent string literals are NOT concatenated by the lexer.

### 2.9 Operators and punctuation

```
+  -  *  /  %
== != <  <= >  >=
&& || !
=  += -= *= /= %=
(  )  [  ]  {  }
,  .  :  ;
```

No `->` token in M1 (return types use `: Type`). No `++`/`--`. Compound assignment is sugar for `x = x ⊕ y` with single evaluation of `x`.

### 2.10 Semicolon rule

**Semicolons are required** (Resolved OQ-M1-02).

- Every simple statement and declaration listed in §3 MUST be terminated by `;`.
- A block `{ ... }` does **not** take a trailing semicolon after `}`.
- There is **no** ASI.
- Missing semicolon MUST produce `E0201` at the token where `;` was expected.

---

## 3. Grammar (EBNF)

Notation: `X?` optional, `X*` zero-or-more, `X+` one-or-more, `|` alternation, `( )` grouping. Tokens in double quotes.

### 3.1 Compilation unit / modules

```
module          ::= item*

item            ::= import_decl
                  | export_item
                  | decl

import_decl     ::= "import" "{" import_name ("," import_name)* "}" "from" string_lit ";"
import_name     ::= identifier

export_item     ::= "export" decl

decl            ::= function_decl
                  | struct_decl
                  | class_decl
                  | const_decl
```

- `from` is a **contextual keyword**: recognized only in this production.
- `from` string MUST be a relative path ending in `.fm` (e.g. `"./math.fm"`); `..` segments allowed under normal FS resolution (Resolved OQ-M1-14). Absolute and bare package names → `E0301`.
- Import resolution: relative to the importing file's directory. Cycles → `E0302`.
- Only `export`ed declarations are visible to importers.
- Duplicate import of the same name → `E0303`.

### 3.2 Struct and class

```
struct_decl     ::= "struct" identifier "{" struct_field* "}"
struct_field    ::= identifier ":" type ";"

class_decl      ::= "class" identifier "{" class_member* "}"
class_member    ::= field_member | constructor_member | method_member
field_member    ::= identifier ":" type ";"
constructor_member ::= "constructor" "(" param_list? ")" block
method_member   ::= identifier "(" param_list? ")" (":" type)? block
param_list      ::= param ("," param)*
param           ::= identifier ":" type
```

- A class MUST have exactly one `constructor` in M1 (`E0414` if missing or duplicate).
- Methods and constructors use `this` (keyword) to refer to the receiver; there is no `self` parameter.
- Instance method call: `obj.method(args)`. Static methods are out of scope for M1.
- Structs have **no** methods and **no** `constructor` keyword; see construction in §3.6.

### 3.3 Functions

```
function_decl   ::= "function" identifier "(" param_list? ")" ":" type block
```

- Return type is **required** (including `: void` for no value).
- `void` is a spellable type name used only as a function return type (not as a value type for bindings).

### 3.4 Types

```
type            ::= "int" | "float" | "bool" | "string" | "void"
                  | identifier                         (* struct or class name *)
                  | type "[" "]"                        (* dynamic array T[] *)
                  | type "[" int_lit "]"                (* fixed array T[N] *)
```

- Dynamic: `int[]`. Fixed: `int[3]` (digits inside brackets).  
  **Justification:** postfix `T[]` matches TypeScript; `T[N]` is the closest TS/C#-readable fixed-size form without generics. (`[T; N]` Rust form is rejected.)
- `void` MUST appear only as a function/method return type (`E0415` otherwise).

### 3.5 Statements

```
block           ::= "{" stmt* "}"

stmt            ::= let_stmt
                  | const_stmt
                  | assign_stmt
                  | expr_stmt
                  | if_stmt
                  | while_stmt
                  | for_stmt
                  | break_stmt
                  | continue_stmt
                  | return_stmt
                  | block

let_stmt        ::= "let" identifier (":" type)? "=" expr ";"
const_stmt      ::= "const" identifier (":" type)? "=" expr ";"
const_decl      ::= "const" identifier (":" type)? "=" expr ";"

assign_stmt     ::= lvalue assign_op expr ";"
assign_op       ::= "=" | "+=" | "-=" | "*=" | "/=" | "%="

expr_stmt       ::= expr ";"

if_stmt         ::= "if" "(" expr ")" block ("else" (if_stmt | block))?
while_stmt      ::= "while" "(" expr ")" block
for_stmt        ::= "for" "(" for_init? ";" expr? ";" for_update? ")" block
for_init        ::= "let" identifier (":" type)? "=" expr | lvalue assign_op expr
for_update      ::= lvalue assign_op expr | call_expr

break_stmt      ::= "break" ";"
continue_stmt   ::= "continue" ";"
return_stmt     ::= "return" expr? ";"
```

`for` is C/TS-style. Omitted condition ≡ `true`. The update clause MUST be an assignment or a call (Resolved OQ-M1-13); other expressions → `E0202`.

### 3.6 Expressions

```
logical_or      ::= logical_and  ("||" logical_and)*
logical_and     ::= equality     ("&&" equality)*
equality        ::= comparison   (("==" | "!=") comparison)*
comparison      ::= term         (("<" | "<=" | ">" | ">=") term)*
term            ::= factor       (("+" | "-") factor)*
factor          ::= unary        (("*" | "/" | "%") unary)*
unary           ::= ("!" | "-" | "+") unary | postfix
postfix         ::= primary postfix_op*
postfix_op      ::= "[" expr "]" | "." identifier | "(" arg_list? ")"
arg_list        ::= expr ("," expr)*

primary         ::= int_lit | float_lit | string_lit | "true" | "false"
                  | identifier
                  | "(" expr ")"
                  | array_lit
                  | struct_lit
                  | new_expr
                  | "this"

array_lit       ::= "[" (expr ("," expr)*)? "]"
struct_lit      ::= identifier "{" field_init ("," field_init)* ","? "}"
field_init      ::= identifier ":" expr

new_expr        ::= "new" identifier "(" arg_list? ")"

call_expr       ::= postfix   (* must reduce to a call *)

lvalue          ::= identifier
                  | postfix "[" expr "]"
                  | postfix "." identifier
                  | "this" "." identifier
```

**Construction rules:**

| Kind | Forms | Notes |
|------|-------|-------|
| `class` | `new ClassName(args)` only | Args match `constructor` parameters. Object-literal construction for classes is **rejected** (`E0416`). |
| `struct` | `new StructName(e0, e1, …)` **or** `StructName { f0: e0, … }` | `new` passes values in **declaration field order**. Literal form requires every field exactly once. |

**Justification for `new` on structs:** M2/M3 Three.js ports use `new Vector3(1, 2, 3)`, `new Mesh(geo, mat)`. Vector types are value types (PLAN.md) but MUST still use `new` at the call site for near-identical structure. Struct `new` is therefore allowed in M1 and is the preferred form for M2 math types; the brace literal remains for explicit named fields.

Assignment is a *statement*, not an expression (no `a = b = c`).

### 3.7 Precedence and associativity

| Prec | Operators | Assoc |
|------|-----------|-------|
| 1 (low) | `\|\|` | left |
| 2 | `&&` | left |
| 3 | `==` `!=` | left |
| 4 | `<` `<=` `>` `>=` | left |
| 5 | `+` `-` | left |
| 6 | `*` `/` `%` | left |
| 7 | unary `!` `-` `+` | right |
| 8 | `new` | n/a (prefix primary) |
| 9 (high) | `[]` `.` `()` | left |

`new Foo()` binds as a primary; `new Foo().method()` is `(new Foo()).method()`.  
No ternary `?:` in M1.

---

## 4. Type system

### 4.1 Primitive types — widths

| Type | Representation | Justification |
|------|----------------|---------------|
| `int` | signed 64-bit two's complement | One width; simple ABI; enough for indices. |
| `float` | IEEE 754 binary64 | Ray-tracing precision later; one float width. |
| `bool` | `true` / `false`; ABI 8-bit 0/1 | No third state. |
| `string` | immutable UTF-8; §4.4 | Text without mutability complexity. |
| `void` | no values | Return-type only. |

No `i32`/`f32`/`u64` in M1 (Resolved OQ-M1-03).

### 4.2 `bool`

Only `true` / `false`. Conditions MUST be `bool` (`E0401`). No truthiness coercion.

### 4.3 Numeric operations

- `+ - * / %` on `int`×`int` → `int`, `float`×`float` → `float`.
- Mixed `int`/`float` arithmetic → `E0402` (use explicit conversions §4.8).
- Unary `-` / `+` on `int`/`float`.
- `%` on `float` → `E0403`.
- Comparisons: §5.12 for strings; numerics and bool as usual.

### 4.4 `string`

- Immutable UTF-8.
- Representation (informative): `{ ptr: *u8, len: usize }`.
- Byte length via `len(s)`; no string indexing in M1.
- Concatenation: `s1 + s2` → new `string`.

### 4.5 Arrays

**Fixed** `T[N]`:

- `N ≥ 0` integer literal.
- Value type: assignment copies all elements.
- Literal `[e0, …, eN-1]` infers `T[N]` when all `ei : T` (default for bare literals; Resolved OQ-M1-04).

**Dynamic** `T[]`:

- Arena-backed growable storage for the owning binding's data buffer; see §4.7 for *class* lifetime (arrays of class refs store references into the program arena).
- Empty `[]` requires annotation: `let a: int[] = [];`.
- Explicit: `let a: int[] = [1, 2, 3];` copies into dynamic storage.

**Indexing / length / push:** as before — `a[i]`, `len(a)`, `push(a, v)` for `T[]` mutable bindings. OOB → trap 102.

### 4.6 Structs (value semantics)

- Assignment and parameter passing **copy** all fields (class-typed fields copy the reference).
- No methods. Access: `s.field`.
- Construction: `new Point(3, 4)` or `Point { x: 3, y: 4 }` (§3.6). Wrong arity for `new` → `E0411`. Missing/duplicate field in literal → `E0404`.

### 4.7 Classes (reference semantics) — program-lifetime arena

- Class instances are **references** to objects allocated with `new`.
- Assignment / parameter passing / returns copy the reference (aliasing).
- **Lifetime (M1, Resolved OQ-M1-17):** instances are allocated from a **program-lifetime arena** and are freed only at **process exit**. There is no per-object free, no GC, and no reference counting in M1.
- This arena policy **MUST be revisited in the M3 specification** (scoped arenas and/or explicit `dispose()` matching Three.js `.dispose()`), before long-running M3/M5 workloads rely on unbounded growth.
- Instances **MAY** escape their allocating scope: returned from functions, stored in fields, stored in arrays, etc. This is required for Three.js-style patterns (e.g. a helper returning `new Mesh(geo, mat)`, or `scene.add(mesh)` where `scene` outlives the helper).
- No `null`. Every class-typed binding MUST be definitely assigned before use (§5.3).
- Fields: `this.field` / `obj.field`; methods: `obj.method(args)`.
- Construction: `new Name(args)` only.

**Non-goals in M1:** scoped reclaim, `dispose()`, refcount (deferred to M3 revisit).

### 4.8 Functions

- Named functions and methods only (no function values).
- Parameter and return types as declared. `: void` functions MUST NOT return a value; non-void MUST return on every path (§5.7).

### 4.9 Type inference for `let` / `const`

- With `: type`, initializer MUST be assignable to `type`.
- Without, infer from initializer (literals, calls, ops, `new`, arrays, struct literals).
- Failure → `E0406`.
- Top-level `const` MUST have a compile-time initializer: literals or struct/array literals of those; **no** `new` of a class at top level (`E0407`). Struct `new` with constant args is allowed for top-level const.

### 4.10 Implicit vs explicit conversions

- No implicit `int`↔`float`.
- Built-ins: `int(f)`, `float(i)`, `int(b)`, `str(i|f|b)` (§6).

### 4.11 Integer overflow

- Wrap modulo 2^64 two's complement (silent) (Resolved OQ-M1-05).

### 4.12 Division by zero

- **Integer** `/` or `%` by zero MUST trap at runtime: exit code **101**, stderr `runtime error: division by zero` (Resolved OQ-M1-16).
- **Float** `/` by zero MUST follow **IEEE 754 binary64**: no trap; yields `+Infinity`, `-Infinity`, or a NaN as defined by IEEE 754 (e.g. `1.0/0.0` → `+Infinity`, `-1.0/0.0` → `-Infinity`, `0.0/0.0` → NaN). Float `%` is not in M1 (`E0403`).
- The C backend MUST NOT enable `-ffast-math` (or equivalent flags that break IEEE division or NaN/`Infinity` semantics).

### 4.12.1 Float relational semantics (IEEE 754)

- Ordered comparisons `<`, `<=`, `>`, `>=` on `float`: if either operand is NaN, the result MUST be `false`.
- `==` on `float`: `false` if either operand is NaN; otherwise IEEE equality (`+0.0 == -0.0` is `true`).
- `!=` on `float`: `true` if either operand is NaN; otherwise the negation of `==`.
- `Infinity` compares greater than all finite values; `-Infinity` compares less than all finite values.

### 4.13 Array bounds checking

- Out of bounds → trap, exit **102**, message `index out of bounds`.

### 4.14 Null / optional policy

- No `null`, no optionals. Class references always valid after definite assignment.

---

## 5. Semantic rules

### 5.1 Scoping

Block-structured lexical scoping. `let`/`const` visible from declaration to end of block (use before decl → `E0501`). Parameters in function/method/constructor body. `for` init `let` scoped to the `for`. `this` valid only inside class methods and constructors (`E0417` elsewhere).

### 5.2 Shadowing

Inner MAY shadow outer. Same-scope duplicate → `E0502`.

### 5.3 Definite assignment

Locals MUST be assigned before read. Initializer required on `let`/`const` syntax. Class fields assigned in `constructor` before use on `this` (simple analysis: every field written on all paths before method returns, or at least assigned in constructor body — M1 MUST assign each field at least once in the constructor (`E0418`)).

### 5.4 Mutability

- `let` mutable; `const` binding immutable (`E0504` on reassign).
- **Shallow const (Resolved OQ-M1-06):** `const` class ref forbids rebinding but allows `obj.field = …`. `const` struct forbids field assignment.

### 5.5 Name resolution across modules

Local → module (incl. imports) → `E0505` if missing. No qualified imports beyond `{ name }`.

### 5.6 Entry point

- Main file (CLI `<file>`) MUST define exactly:

```
function main(): int { ... }
```

- Zero parameters; return type `int` MUST be present (`E0506` if missing/wrong) (Resolved OQ-M1-11).
- Returned `int` is the process exit code.
- Other modules MUST NOT define `main` (`E0507`).

### 5.7 Required return

- Non-`void` functions/methods: every path returns (`E0508`).
- `return;` only in `: void`; `return expr;` type-checked.

### 5.8 Unreachable code

- Hard unreachable after `return` in the same block → `E0509` (error).

### 5.9 Recursion

Allowed; no guaranteed TCO. Stack overflow is host abort (non-normative).

### 5.10 `break` / `continue`

Only in `while`/`for`; else `E0510`.

### 5.11 Short-circuit

`&&` / `||` MUST short-circuit; operands `bool`.

### 5.12 String comparison

`==` / `!=` by UTF-8 bytes; ordered compares lexicographic by unsigned byte values.

---

## 6. Built-in minimal stdlib (M1)

Implicit prelude; linked only if used.

### 6.1 `print` / `println`

```
function print(value: int): void
function print(value: float): void
function print(value: bool): void
function print(value: string): void

function println(value: int): void
function println(value: float): void
function println(value: bool): void
function println(value: string): void
```

Built-in overloading only; user overloading deferred to M2+ (Resolved OQ-M1-15).

- `println` = `print` + `\n`.
- `bool` → `true` / `false`.
- `int` → decimal, optional `-`, no `+`.
- `string` → raw bytes.
- **`float` (exact, deterministic):**
  1. NaN → `NaN`
  2. `+Infinity` → `Infinity`; `-Infinity` → `-Infinity`
  3. `+0.0` → `0`; `-0.0` → `-0` (Resolved OQ-M1-10)
  4. Finite non-zero: ECMAScript 2024 `Number::toString` (shortest round-trip; scientific when decimal exponent \(k < -6\) or \(k \geq 21\)).

### 6.2 String ops

```
function len(s: string): int
function str(value: int): string
function str(value: float): string
function str(value: bool): string
```

Concat via `+`.

### 6.3 Arrays and conversions

```
function len(a: T[N]): int
function len(a: T[]): int
function push(a: T[], value: T): void
function int(value: float): int       (* toward zero; non-finite → trap 103 *)
function float(value: int): float
function int(value: bool): int
```

Trap 103: `runtime error: integer conversion of non-finite float`.

### 6.4 Dynamic array growth

Only `push` + `len` (Resolved OQ-M1-07).

---

## 7. CLI

### 7.1 Commands

```
farmc build <file.fm> [-o <out>] [--emit-c [<path>]] [--keep-c] [-v]
farmc run <file.fm> [-- <args...>]
farmc version
farmc help
```

`farmc run` SHOULD be implemented (Resolved OQ-M1-09): build to a temp artifact, execute, forward the child exit code.

- Default output: Linux `./<basename>`; Windows `.\<basename>.exe`.
- `--emit-c`: write C11 and still link by default; `--keep-c` retains `.c`.

### 7.2 farmc exit codes

| Code | Meaning |
|------|---------|
| 0 | Success |
| 1 | Compile error(s) |
| 2 | CLI usage error |
| 3 | Driver failure (missing C compiler, I/O, link) |
| 4 | Internal compiler error |

### 7.3 C compiler flags (goals)

Linux: `-std=c11 -Os -flto -ffunction-sections -fdata-sections -Wl,--gc-sections` then `strip`. Windows: size-oriented MSVC/clang-cl equivalents.

The driver MUST NOT pass `-ffast-math`, `/fp:fast`, or equivalents that violate IEEE 754 float division or `NaN`/`Infinity` printing (Resolved OQ-M1-16).

---

## 8. Diagnostics format

### 8.1 Format (exact)

```
<path>:<line>:<col>: error[E0xxx]: <message>
```

- `line` / `col` **1-based**.
- `col` counts **Unicode scalar values**; tab = 1 column (Resolved OQ-M1-08).

### 8.2 Multiple errors

SHOULD report multiple; MAY stop after a cascade limit (suggested ≥ 20). Exit 1 if any error.

### 8.3 Error code table (M1)

| Code | Message template |
|------|------------------|
| E0001 | invalid UTF-8 sequence |
| E0002 | unterminated block comment |
| E0003 | reserved keyword `{name}` cannot be used as identifier |
| E0101 | integer literal out of range for `int` |
| E0102 | float literal out of range |
| E0103 | invalid Unicode escape in string |
| E0104 | incomplete hex integer literal |
| E0201 | expected `;` |
| E0202 | unexpected token `{tok}` |
| E0203 | unterminated string literal |
| E0301 | invalid module path `{path}` |
| E0302 | cyclic module import |
| E0303 | duplicate import of `{name}` |
| E0304 | module `{path}` not found |
| E0305 | `{name}` is not exported from `{path}` |
| E0401 | condition must be `bool`, found `{type}` |
| E0402 | mixed `int`/`float` arithmetic without explicit conversion |
| E0403 | operator `{op}` not defined for `{type}` |
| E0404 | struct literal missing or duplicate field `{field}` |
| E0406 | cannot infer type of `{name}` |
| E0407 | top-level const initializer is not compile-time constant |
| E0408 | type mismatch: expected `{expected}`, found `{found}` |
| E0409 | undefined type `{name}` |
| E0410 | index expression requires array type |
| E0411 | wrong number of arguments: expected {n}, found {m} |
| E0414 | class `{name}` must have exactly one constructor |
| E0415 | `void` type not allowed here |
| E0416 | classes must be constructed with `new` |
| E0417 | `this` not allowed outside class method/constructor |
| E0418 | constructor does not assign field `{field}` |
| E0501 | use of `{name}` before its declaration |
| E0502 | duplicate definition of `{name}` |
| E0503 | `{name}` used before definite assignment |
| E0504 | cannot assign to const `{name}` |
| E0505 | undefined name `{name}` |
| E0506 | missing or invalid `function main(): int` in main file |
| E0507 | `main` is only allowed in the main file |
| E0508 | missing return on some paths in `{name}` |
| E0509 | unreachable statement |
| E0510 | `break`/`continue` outside loop |
| E0511 | cannot assign to immutable field path |
| E0513 | arity or type mismatch for built-in `{name}` |

**Removed:** `E0405` (reference escapes allocating scope) — obsolete under program-lifetime arena.

---

## 9. Runtime error behavior (traps)

Write one stderr line `runtime error: <message>\n`, then exit:

| Exit | Message | When |
|------|---------|------|
| 101 | `division by zero` | Integer `/` or `%` by zero only (float `/` by zero does not trap) |
| 102 | `index out of bounds` | Array index OOB |
| 103 | `integer conversion of non-finite float` | `int(non-finite float)` |

---

## 10. Acceptance criteria

| ID | Criterion |
|----|-----------|
| AC-M1-01 | Linux x64: `farmc build spec/tests/M1/001_hello.fm -o /tmp/hello && strip --strip-all /tmp/hello && wc -c < /tmp/hello` ≤ **20480**. |
| AC-M1-02 | ≥ **30** M1 tests pass in CI (this pack ≥ 40). |
| AC-M1-03 | Compile failures use `path:line:col: error[E0xxx]:` with correct 1-based line/col. |
| AC-M1-04 | Native binary on Linux and Windows. |
| AC-M1-05 | `kind: run` stdout exact match (float rules §6.1). |
| AC-M1-06 | Hello binary has no graphics/math stdlib; size proxy AC-M1-01. |
| AC-M1-07 | `farmc` exit codes §7.2; user `main` return = process exit. |
| AC-M1-08 | Integer div-by-zero and OOB traps: exact stderr + exit 101/102; float `/0` prints Infinity/NaN (no trap). |
| AC-M1-09 | Multi-file import/export passes. |
| AC-M1-10 | Soft: hello build ≤ 5 s typical CI (non-binding). |
| AC-M1-11 | Class instances may be returned/stored across scopes (program arena); no E0405. |

---

## 11. Test case index

Fixtures: `spec/tests/README.md`. Multi-file: `022_modules/main.fm` + `022_modules.expected`.

| ID | File | Category | Expected |
|----|------|----------|----------|
| 001 | `001_hello.fm` | run | `Hello, Farmos` |
| 002 | `002_int_literals.fm` | run | ints |
| 003 | `003_float_literals.fm` | run | floats |
| 004 | `004_bool_literals.fm` | run | bools |
| 005 | `005_string_escapes.fm` | run | escapes |
| 006 | `006_arithmetic_int.fm` | run | int arith |
| 007 | `007_arithmetic_float.fm` | run | float arith |
| 008 | `008_comparison.fm` | run | compares |
| 009 | `009_logic_short_circuit.fm` | run | short-circuit |
| 010 | `010_if_else.fm` | run | branching |
| 011 | `011_while.fm` | run | while |
| 012 | `012_for.fm` | run | for |
| 013 | `013_break_continue.fm` | run | break/continue |
| 014 | `014_functions.fm` | run | functions |
| 015 | `015_recursion.fm` | run | fib(10)=55 |
| 016 | `016_array_fixed.fm` | run | `int[3]` |
| 017 | `017_array_dynamic.fm` | run | `int[]` + push |
| 018 | `018_array_bounds_trap.fm` | runtime_trap | 102 |
| 019 | `019_string_ops.fm` | run | concat/len |
| 020 | `020_struct.fm` | run | struct + `new` |
| 021 | `021_class_methods.fm` | run | class/`this`/`new` |
| 022 | `022_modules` | run | import/export |
| 023 | `023_type_mismatch.fm` | compile_error | E0408 |
| 024 | `024_undefined_name.fm` | compile_error | E0505 |
| 025 | `025_syntax_missing_semi.fm` | compile_error | E0201 |
| 026 | `026_missing_return.fm` | compile_error | E0508 |
| 027 | `027_mutability.fm` | compile_error | E0504 |
| 028 | `028_main_exit_code.fm` | run | exit 42 |
| 029 | `029_div_by_zero_int.fm` | runtime_trap | 101 |
| 030 | `030_overflow_wrap.fm` | run | wrap |
| 031 | `031_shadowing.fm` | run | shadow |
| 032 | `032_const_ok.fm` | run | const |
| 033 | `033_unary.fm` | run | unary |
| 034 | `034_assign_ops.fm` | run | += |
| 035 | `035_nested_if.fm` | run | nested if |
| 036 | `036_string_compare.fm` | run | string cmp |
| 037 | `037_empty_main.fm` | run | silence |
| 038 | `038_float_ieee.fm` | run | Infinity / -Infinity / NaN |
| 039 | `039_mixed_arith_error.fm` | compile_error | E0402 |
| 040 | `040_break_outside.fm` | compile_error | E0510 |
| 041 | `041_class_escape.fm` | run | return `new` across scopes |
| 042 | `042_hex_literals.fm` | run | hex print values |
| 043 | `043_hex_overflow.fm` | compile_error | E0101 |
| 044 | `044_hex_incomplete.fm` | compile_error | E0104 |

---

## 12. Resolved decisions

All M1 open questions are closed. **None** remain.

| ID | Decision | Approved |
|----|----------|----------|
| OQ-M1-01 | Hex integer literals `0x`/`0X`+hex digits are in M1 (type `int`, value in \([0, 2^{63}-1]\)); binary deferred; no `_` separators; bare `0x` → `E0104`. | PM 2026-09-24 |
| OQ-M1-02 | Semicolons required; no ASI. | PM 2026-09-24 |
| OQ-M1-03 | Only `int` (i64) and `float` (f64); no `i32`/`f32`. | PM 2026-09-24 |
| OQ-M1-04 | Bare array literal → fixed `T[N]`; dynamic needs `: T[]`. | PM 2026-09-24 |
| OQ-M1-05 | Integer overflow wraps (two's complement). | PM 2026-09-24 |
| OQ-M1-06 | Shallow `const` on class references. | PM 2026-09-24 |
| OQ-M1-07 | Dynamic array API: `push` + `len` only. | PM 2026-09-24 |
| OQ-M1-08 | Diagnostic columns count Unicode scalar values. | PM 2026-09-24 |
| OQ-M1-09 | `farmc run` SHOULD be implemented. | PM 2026-09-24 |
| OQ-M1-10 | Print IEEE `-0.0` as `-0`. | PM 2026-09-24 |
| OQ-M1-11 | `function main(): int` required (explicit `: int`). | PM 2026-09-24 |
| OQ-M1-12 | *(withdrawn)* Escape analysis removed; program-lifetime arena. | PM 2026-09-24 |
| OQ-M1-13 | `for` update: assignments or calls only. | PM 2026-09-24 |
| OQ-M1-14 | Module paths: relative `.fm` (incl. `..`). | PM 2026-09-24 |
| OQ-M1-15 | Built-in overloading only; user overloading deferred to M2. | PM 2026-09-24 |
| OQ-M1-16 | Float `/` by zero is IEEE 754 (no trap); integer `/` `%` by zero traps 101; print `Infinity`/`-Infinity`/`NaN`; no `-ffast-math`. | PM 2026-09-24 |
| OQ-M1-17 | M1 program-lifetime arena; **MUST be revisited in M3 spec**. | PM 2026-09-24 |

### PLAN.md alignment

1. JS/TS-like surface with `function` / `new` / `this` / hex colors for Three.js ports.
2. Operator overloading remains M2 (built-in overload sets only in M1).
3. Struct value types + `new Vector3(...)` call shape for M2.
4. No GC via program arena (M3 must revisit reclaim).


---

## Appendix A — Three.js porting sketch (non-normative)

Illustrative only; `Scene`, `Mesh`, `BoxGeometry`, … are M3 names. Shows M1 surface + placeholder APIs:

```
import { Scene, PerspectiveCamera, BoxGeometry, MeshBasicMaterial, Mesh, Renderer } from "./three.fm";

function main(): int {
  const scene: Scene = new Scene();
  const camera: PerspectiveCamera = new PerspectiveCamera(75.0, 1.333, 0.1, 1000.0);
  const geometry: BoxGeometry = new BoxGeometry(1.0, 1.0, 1.0);
  const material: MeshBasicMaterial = new MeshBasicMaterial(0xff0000);
  const cube: Mesh = new Mesh(geometry, material);
  scene.add(cube);
  const renderer: Renderer = new Renderer(800, 600);
  renderer.render(scene, camera);
  return 0;
}
```

A helper that returns a mesh (legal under program-lifetime arena):

```
function makeCube(): Mesh {
  return new Mesh(new BoxGeometry(1.0, 1.0, 1.0), new MeshBasicMaterial(0xff0000));
}
```

---

## Document history

- 2026-09-24: Initial M1 draft.
- 2026-09-24: Rev. 2 — TypeScript-style surface; program-lifetime class arena; Three.js appendix; E0405/OQ-M1-12 removed; OQ-M1-17 added; test 041.
- 2026-09-24: FINAL — PM resolved all OQs; hex literals; IEEE float `/0`; resolved-decisions table; tests 042–044; 038 → run.
