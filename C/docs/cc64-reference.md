# cc64 Reference Manual

`cc64` compiles a small, deliberately-scoped subset of C directly to
6502 assembly in the exact syntax the `c64asm` assembler (`../asm/`)
expects. This document is the syntax/semantics reference for the
language it accepts — for the compiler's own internal architecture
(lexer/parser/codegen phase split, why there's no call stack, why
compilation happens in two passes), see `../README.md` and each
`src/*.c` file's own header comment instead.

`cc64` only ever *emits* `.asm` text; it doesn't link against
`c64asm`. A full C -> `.prg` pipeline needs both built, `cc64` first:

```sh
cd C && make                      # -> bin/cc64
cd ../asm && make                 # -> bin/c64asm
```

---

## 1. Command-line usage

```
cc64 <input.c> -o <output.asm> [-I dir ...]
```

| Argument | Required | Description |
|---|---|---|
| `<input.c>` | yes | Path to the C source file. |
| `-o <output.asm>` | yes | Path to write the generated `c64asm`-syntax assembly. |
| `-I <dir>` / `-Idir` | no | Add a directory to the `#include` search path (§17). May be given more than once. |
| `-h`, `--help` | no | Print a short usage message. |

`cc64` only *generates* assembly — it doesn't invoke `c64asm` itself.
Assemble the output separately, or use `build.sh`, which chains both
steps:

```sh
./cc64 program.c -o program.asm
../asm/bin/c64asm program.asm -o program.prg
# or, in one step from C/:
./build.sh program.c program.prg
```

The resulting `.prg` is built with a BASIC `10 SYS ...` loader stub, so
`LOAD "PROGRAM",8` then `RUN` works on real hardware or in an emulator
exactly like any other C64 program.

On success, `cc64` prints `cc64: wrote <output.asm>` to stderr. On
error, every message has the same shape:

```
cc64: error: line 12: unknown member 'y' of struct Point
```

`line` is omitted for the handful of whole-program errors that aren't
about one specific source line (e.g. "too many string literals").
Unlike `c64asm`, which collects and reports multiple errors per run,
`cc64` stops at the first error it finds.

### `#include` search path

`<angle-bracket>` includes are found automatically in `lib/` next to
the `cc64` binary itself, with no setup — provided `cc64` is invoked
with a path that contains a `/` (`./cc64`, `../build/cc64`,
`/opt/cc64/cc64` all qualify; this covers every example in this
document). If `cc64` is invoked by bare name off `$PATH`, there's no
portable, dependency-free way in C99 to recover its own install
location, so that one case needs an explicit `-I`:

```sh
cc64 program.c -o program.asm -I /path/to/cc64/lib
```

See §17 for the full `#include` search-order rules.

---

## 2. Compilation model

`cc64` scans every top-level declaration (function signatures and
globals) in one forward pass (`pass_a`) before generating any code
(`pass_b`). Practical consequence: **you never need forward
declarations** — any function can call any other function anywhere in
the same file, regardless of which is written first. A prototype like
`int foo(int x);` is accepted for documentation purposes but is never
required.

Every variable — global or local, parameter or plain local — gets
**fixed, static storage** for the whole program's lifetime; there is
no call stack backing ordinary variable storage the way there is in a
conventional C implementation. Recursion is layered on top of this
model rather than replacing it — see §5.3.

---

## 3. Types

| Type | Width | Signed? | Notes |
|---|---|---|---|
| `char` | 8-bit | **always unsigned** | `unsigned char` is accepted as a synonym; adds nothing. No sign-extension logic exists anywhere for `char`. |
| `int` | 16-bit | signed by default | `unsigned int` / bare `unsigned` is also valid — see §3.1. |
| `void` | — | — | Only valid as a function return type or in a pointer-parameter-less function signature; there is no `void *` — see §6. |
| `T *` | 16-bit | — | Single-level pointer to any of the above, or to a `struct`/`union` tag. Pointer-to-pointer (`int **`) is **not supported**. |
| `struct Tag` | sum of members | — | See §8. |
| `union Tag` | widest member | — | See §9. |
| `enum [Tag]` | 16-bit (`int`) | signed | Never a distinct runtime type — see §10. |

No `long`/`short` (there is only one integer width besides `char`),
and no floating point at all — the 6502 has no hardware float support,
and a software implementation (mantissa/exponent representation,
arithmetic routines from scratch) isn't a small addition. `_Bool`/
`bool` don't exist either, though every truth-testing context
(`if`/`while`/`&&`/`||`) already just tests "zero or not" using plain
`int`/`char` values.

### 3.1 `unsigned`

`unsigned int` (or bare `unsigned`, meaning the same thing) is
supported as a qualifier on `int` — `char` needs no such qualifier
since it's already unconditionally unsigned. `unsigned` combined with
`struct`/`union`/`enum`/`void`/a typedef name is a compile-time error;
it only ever means something in front of a plain `int`.

Signedness only changes generated code for operators whose result
genuinely depends on it: `/`, `%`, `>>`, and the four relational
comparisons (`<` `>` `<=` `>=`). Every bit-pattern-invariant operator
(`+ - * & | ^ << == !=`) produces identical code either way. Mixing a
signed and an unsigned operand in one expression makes the result
unsigned, matching C's usual-arithmetic-conversions rule.

Two narrow, accepted gaps (consistent with this compiler's "light type
checking, not a full type system" philosophy — see §16): unary
`- ~ !` and `&` don't propagate `isUnsigned` through their operand,
so `(-x) / y` can lose `x`'s unsigned-ness if `x` was unsigned; and
`&x`'s resulting pointer type doesn't track the pointee's signedness.
Neither comes up in ordinary code.

---

## 4. Declarations

### 4.1 Globals

```c
int counter;
int total = 5;
int negative = -3;
char flag;
struct Point origin;
int scores[10];
char buffer[40];
```

A scalar (non-pointer, non-struct, non-array) global may have a
constant literal initializer. **Array, pointer, and struct/union
initializers on globals are not supported** — arrays and structs start
zero-filled; give a global pointer its value inside a function (e.g.
`main`) instead.

### 4.2 Locals

Local declarations may appear anywhere inside a block, not just at
its start:

```c
void f(void) {
    int x = 1;
    puts("hi");
    int y = x + 1;   /* fine - not required to be at the top of the block */
}
```

Local pointer declarations **can** have an initializer, including a
string literal:

```c
char *s = "hi";
```

Local struct/union declarations cannot be initialized at declaration
time (they can still be assigned to, member by member, afterward).

### 4.3 Arrays

One-dimensional only — multi-dimensional arrays (`int a[3][4]`) are
not supported. An array's size must be a compile-time constant
(a literal or an `enum` constant, §10).

```c
int scores[10];
char buffer[40];
struct Point pts[10];
```

Arrays decay to a pointer to their first element wherever a pointer is
expected: passing an array as a function argument, assigning an array
to a pointer variable, etc. — see §6.

Array *parameters* must be written `type *name`, not `type name[]` —
the two are identical in what they receive (a decayed pointer), this
is purely a syntax gap.

---

## 5. Functions

```c
int add(int a, int b) {
    return a + b;
}

void greet(char *name) {
    puts(name);
}
```

- Typed parameters, including pointer parameters.
- A typed return value, including pointer return types. `void` means
  no return value.
- Any function may call any other function regardless of declaration
  order in the file (§2).
- **Full recursion**, direct or mutual — see §5.3.

### 5.1 Calling convention

Arguments are copied into the callee's own fixed parameter storage,
then a plain `JSR`. There is no register-window or stack-frame calling
convention to speak of. Return values are left in the primary register
(`__zpR`) at `RTS` time — `return expr;` computes `expr` into `__zpR`
and returns immediately; there is no shared epilogue to tear down,
since there is no stack frame to unwind. Pointer parameters and return
values use the exact same mechanism: a pointer is just a 16-bit value
like `int`, always occupying the full 2-byte slot regardless of what
it points to.

### 5.2 Frame size limit

**A function's frame — its parameters plus all its locals, including
local arrays — cannot exceed 256 bytes.** The frame-copy machinery
that makes recursion work (§5.3) counts bytes with the 6502's 8-bit Y
register, which is the hard ceiling. Exceeding it is a **compile-time
error**, not silent truncation — the usual fix is moving a large local
array to file scope (a global) instead.

### 5.3 Recursion

Every function's parameters and locals still get fixed, static storage
(§2) — reading or writing a variable is a plain absolute load/store,
never frame-pointer-relative. Recursion is layered on top rather than
replacing that model: for every function, the compiler emits a
`pushframe` routine (copy that function's entire parameter+local block
onto a software call stack) and a `popframe` routine (copy it back),
and wraps every call site with them — save the callee's current frame,
write the new arguments, `JSR`, then restore. When a recursive chain
unwinds, each level finds its variables exactly as it left them, even
though every level used the identical physical addresses.

Two limits are enforced at runtime rather than left as silent traps.
Any of the three below prints a `CC64 RUNTIME ERROR` message on-screen
and halts, rather than corrupting memory:

1. The software call stack (a fixed 4 KB buffer) running out.
2. The 6502's own 256-byte hardware stack — which holds one `JSR`
   return address per open call — filling up. This is the binding
   limit for very deep recursion, at roughly a hundred levels.
3. The software *expression operand stack* (used for evaluating nested
   expressions like `a * (b + c)`, independent of the call-frame
   machinery above) running out. Its entries can be held live across a
   recursive call — the `n` in `n * fact(n - 1)` stays parked on it for
   the entire recursion beneath it — so deep recursion with a live
   operand at every level can exhaust it too.

---

## 6. Pointers

```c
int x = 5;
int *p = &x;
*p = 10;          /* x is now 10 */
(*p)++;
```

- `&x` — address-of. Works on scalars and array elements. Safe on
  locals too, since (§2) there is no call stack for a local's address
  to dangle from once its function returns.
- `*p` — dereference, usable both as an rvalue and as an assignment
  target, including compound forms (`*p += n`, `(*p)++`).
- `p[i]` — indexing through a pointer, not just through a true array.
- Pointer arithmetic: `p + n`, `p - n`, `p++`/`p--`, all correctly
  scaled by `sizeof(*p)` (2 bytes for `int*`/a struct pointer, 1 byte
  for `char*`).
- `p2 - p1` — pointer-minus-pointer, giving an **element count**, not a
  byte count.
- Pointer comparisons `< > <= >= == !=` use an **unsigned** comparison
  (addresses aren't signed quantities), unlike plain `int` comparisons,
  which are signed.
- `0` is always accepted as a valid pointer value ("null") wherever the
  compiler's light type checking (§16) checks pointer types.

**Not supported:** pointer-to-pointer (`int **`), function pointers,
arrays of pointers, `void *`.

---

## 7. `struct`

```c
struct Point {
    int x;
    int y;
};

struct Point origin;
origin.x = 1;

struct Point *p = &origin;
p->y = 2;              /* sugar for (*p).y - same code path either way */

struct Point *make_point(int x, int y);   /* pointer-only across function boundaries */
```

A `struct Tag { ... }` is parsed and fully laid out — every member's
byte offset and the whole struct's total size — while scanning
declarations, before any function body is parsed for real. Members are
packed with **no padding** (this is a byte-addressed machine with no
alignment requirements). `.` for direct access, `->` for access
through a pointer, `&s`/`&s.member`/`&p->member` for address-of, and
arrays of structs (`pts[i].x`) with both compile-time and runtime
indexing all work.

**Members must be `int`, `char`, or a pointer** — never another
struct/union held by value, and never an array.

**Structs are pointer-only across function boundaries**: a parameter
or return type must be `struct Tag *`, never `struct Tag` by value.
Real C supports both; by-value passing needs genuine struct-copying
machinery at every call boundary, which wasn't worth adding for a
pattern (linked lists, trees, anything built with `->`) pointer-passing
already covers naturally.

**Self-referential structs work** (`struct Node { int val; struct Node
*next; };`), and so do two structs referencing each other regardless
of which is written first in the file — a `struct Tag` name resolves
to a table entry the moment it's *seen* (creating an incomplete
placeholder if needed), rather than requiring the tag to already be
fully defined. A pointer never needs its pointee's size (a pointer is
always 2 bytes), so this works even while `Tag` itself is still being
defined. Only *using* a struct by value (a variable of that type,
which needs a known size) requires it to be fully defined by that
point — using an incomplete struct by value is a compile-time error.

---

## 8. `union`

```c
union Value {
    int as_int;
    char as_bytes[2];   /* array member: NOT allowed - see below */
};
```

`union Tag { ... }` shares essentially all of `struct`'s own machinery
— same parsing function, same member-type restrictions (`int`/`char`/
pointer only), same `.`/`->`/`&` access — and the same pointer-only-
across-function-boundaries rule. **The one real difference**: every
union member starts at byte offset 0 instead of packing sequentially
(every member overlaps every other one), and the union's own size is
its single widest member's width, not their sum.

```c
union Value v;
v.as_int = 0x1234;
/* v's bytes are now shared storage - reading a narrower member back
   reads part of what the wider write stored, and vice versa */
```

`struct` and `union` tags **share one namespace**, matching real C —
you can't have both a `struct Foo` and a `union Foo`, and using the
wrong keyword for an already-fully-defined tag is a compile-time
error. A tag that's only been forward-referenced so far (not yet given
a real `{ members }` body anywhere) is more lenient about which
keyword was used, since a pointer's generated code (always 2 bytes)
can't go wrong from an early mislabeling either way — only a
completed definition's keyword is ever authoritative.

A "tagged union" pattern (a struct holding a discriminator field
alongside a union) needs a pointer member (`union Tag *data;`), the
same workaround a self-referential struct already needs — a union
can't be another struct/union's member by value.

---

## 9. `enum`

```c
enum Color { RED, GREEN, BLUE };          /* 0, 1, 2 */
enum Status { OK = 0, FAIL = 1, RETRY };  /* RETRY = 2, auto-increment resumes */

int c = GREEN;
switch (c) {
    case RED: ...
    case GREEN: ...
}
```

`enum [Tag] { NAME [= value], ... };` never creates a real distinct
runtime type — every enumerator is a compile-time `int` constant, and
`enum Tag` used later as a type (a variable, parameter, return type,
or struct/union member) is nothing more than an alias for `int`: no
storage representation, no runtime tag, no way to tell at runtime that
a value "came from" an enum at all. Consequently, an enum constant can
be used absolutely anywhere a plain `int` constant is valid: ordinary
expressions, a `switch` `case` label, an array size, or a global
initializer.

- **The tag is optional**, unlike `struct`'s required one —
  `enum { RED, GREEN, BLUE };` with no tag at all is valid and common,
  when only the constants matter. A tag only needs to exist if you
  want to later write `enum Tag` as a type somewhere.
- **Unlike a struct tag, an enum tag can never be forward-referenced.**
  `enum Tag` used as a type requires `Tag` to already be fully defined
  earlier in the file — there's no "incomplete enum pointer" escape
  hatch the way an incomplete struct has (there's no size question a
  pointer could sidestep in the first place, since an enum-typed
  pointer doesn't exist as a distinct concept — see §6).
- **An enumerator's value must be a literal**, not an expression
  referencing an earlier enumerator (`B = A + 1` doesn't work) — the
  same restriction a global initializer, `case` label, and array size
  already have; there's no constant-expression evaluator. Omitting
  `= value` falls back to C's usual rule: one more than the previous
  enumerator, or 0 for the first.
- **`enum` definitions are top-level only** — there is no local
  `enum { ... };` inside a function body. A constant from a top-level
  enum can still size a *local* array; only the definition itself must
  be top-level.
- Because top-level declarations are all resolved in one forward pass
  (§2) before any function body is parsed for real, a top-level use of
  an enum's tag-as-type or its constants (as an array size or global
  initializer) must come after the `enum` definition in the file.
  Inside a function body, an enum defined **anywhere** in the file —
  even later — can be referenced, since function bodies aren't
  compiled until every top-level declaration has already been scanned.

---

## 10. `typedef`

```c
typedef int Distance;
typedef char *String;
typedef struct Point Point;    /* the "drop the struct keyword" idiom */

Distance d = 5;
String s = "hi";
Point p;
```

`typedef <type> Name;` resolves **entirely at parse time** — by the
time parsing moves past a use of `Name`, it has already been
substituted for whatever it actually means. Nothing downstream (AST,
codegen, type inference) ever sees `Name` or knows a typedef was
involved.

- Works everywhere a type can appear: variables, parameters, return
  types, struct/union members, array element types, even another
  `typedef`'s own underlying type (chaining: `typedef MyInt
  AnotherInt;`).
- **The underlying type must already exist as a plain type
  reference** — `typedef struct { ... } Name;` (an inline anonymous
  definition combined with the typedef in one statement) is **not
  supported**, since this compiler has no anonymous-struct-definition
  syntax at all. Define the tag separately first, then `typedef` it.
- **A typedef'd pointer type can't gain an extra `*` at a use site**:
  given `typedef char *String;`, writing `String *sp;` is rejected as
  pointer-to-pointer (not supported at all — §6).
- **Top-level only**, the same restriction `struct`/`union`/`enum`
  definitions have.

---

## 11. Statements

`if`/`else`, `while`, `do`/`while`, `for`, `switch`/`case`/`default`,
`break`, `continue`, `return`, blocks (`{ ... }`), local declarations
(anywhere in a block — §4.2), and the empty statement (`;`).

```c
if (x > 0) { ... } else { ... }
while (x < 10) { ... }
do { ... } while (x < 10);
for (int i = 0; i < 10; i++) { ... }
```

### 11.1 `switch`

```c
switch (expr) {
    case 1:
        ...
        break;
    case 2:
    case 3:
        ...          /* shared body for two cases */
        break;
    default:
        ...
}
```

Compiles to a straight-line compare-and-branch chain, **not a jump
table** — there is no dispatch-table machinery in this compiler, so
`switch` is syntax sugar over what you'd otherwise write as a chain of
`if (x == C1) ... else if (x == C2) ...`. Consequences:

- **Every `case` label must be a constant integer literal** (optionally
  negated, or a named `enum` constant — §9), the same restriction a
  global initializer already has — `expr` is evaluated once, into the
  primary register, and left untouched through the whole chain.
- **Fallthrough is real**, exactly like C: without an explicit `break`,
  execution continues into the next label's statements.
- `default` doesn't have to be written last, or be present at all —
  its comparison always runs after every `case` has already been
  checked, regardless of where it appears in the source.
- **`case`/`default` labels are only recognized directly inside a
  switch's own `{ }`**, not nested inside an inner block or `if` —
  Duff's-device-style tricks aren't supported.

### 11.2 `break` and `continue`

Both are aware of the loop-vs-switch distinction, matching real C:

- `break` exits the *nearest* enclosing loop **or** switch, whichever
  is innermost.
- `continue` always means the nearest enclosing *loop* — it skips past
  any switch frames on top of it, since a switch has no continue
  target of its own. `case N: continue;` inside a `switch` that's
  itself inside a `while`/`for`/`do`-`while` correctly reaches the
  loop, not just the switch's own end.

---

## 12. Operators

Listed loosest-binding to tightest (standard C precedence and
associativity — all binary operators below are left-associative,
except assignment, which is right-associative):

| Precedence (loose -> tight) | Operators |
|---|---|
| Assignment (right-assoc.) | `= += -= *= /= %= &= \|= ^= <<= >>=` |
| Logical OR | `\|\|` |
| Logical AND | `&&` |
| Bitwise OR | `\|` |
| Bitwise XOR | `^` |
| Bitwise AND | `&` |
| Equality | `== !=` |
| Relational | `< > <= >=` |
| Shift | `<< >>` |
| Additive | `+ -` |
| Multiplicative | `* / %` |
| Unary (right-assoc.) | `- ! ~ ++x --x &x *x` (prefix) |
| Postfix | `x++ x-- a[i] .member ->member f(...)` |

`&&` and `\|\|` short-circuit (the right operand is only evaluated if
the left doesn't already decide the result).

`/` and `%` are correctly **signed** for signed operands (truncate
toward zero; the remainder takes the sign of the dividend, matching
C99) and correctly **unsigned** for unsigned operands (§3.1). `>>` is
arithmetic (sign-extending) for signed operands and logical
(zero-filling) for unsigned ones. Compound assignment (`x /= y`, etc.)
routes through the same signed/unsigned logic as the non-compound
form.

There is no `sizeof`, no ternary `?:`, and no comma operator.

---

## 13. Literals

- **Integers:** decimal (`42`) and `0x`-prefixed hex (`0x2A`).
- **Characters:** `'c'`, with escapes `\n \t \\ \' \" \0`.
- **Strings:** `"text"` — real `char*` values, interned as data (not
  merely accepted as a `puts()` argument). A string literal can be
  assigned to a `char *` local (`char *s = "hi";`) and used as an
  ordinary runtime value, walked, compared, copied, etc.

String literals are stored as their **exact raw bytes**, not
PETSCII-converted at compile time — see §19 for why, and where the
conversion actually happens.

---

## 14. Builtins

These require no `#include`:

| Builtin | Signature | Description |
|---|---|---|
| `putchar(x)` | `void putchar(int x)` | Print one character, PETSCII-converted at runtime (§19). |
| `puts(s)` | `void puts(char *s)` | Print a NUL-terminated string. Accepts a literal, a variable, or a runtime-built buffer — walked up to the first zero byte, same as C's `puts` but without the trailing newline. |
| `peek(addr)` | `int peek(int addr)` | Read one byte from an absolute memory address. |
| `poke(addr, val)` | `void poke(int addr, int val)` | Write one byte to an absolute memory address. |
| `printf(fmt, ...)` | — | See §15. |

### 15. `printf`

`printf(fmt, ...)` works **without any real variadic-function
machinery** — no `...` parameters, no `va_list`/`va_arg`, nothing
resembling a genuine variable-argument-count calling convention exist
anywhere in `cc64`. The trick: **`fmt` must be a compile-time string
literal**, never a variable or other runtime expression. Since its
exact contents are therefore known to the compiler, `printf(...)`
doesn't compile into anything that parses a format string at runtime —
the compiler parses it once, at compile time, and expands the call
into the same fixed sequence of `puts`/specifier-specific calls a
programmer chaining `putchar`/`print_int` by hand would have written.

```c
printf("x=%d, y=%s\n", x, name);
```

compiles to essentially the same code as hand-writing the equivalent
`putchar`/`print_int`/`puts` chain would.

| Specifier | Meaning |
|---|---|
| `%d` | Signed decimal `int` |
| `%u` | Unsigned decimal `int` (§3.1) |
| `%x` | 4 uppercase hex digits |
| `%c` | One character, PETSCII-converted like `putchar` |
| `%s` | A `char*` argument, walked like `puts` |
| `%%` | A literal `%` |

Not supported: `%f`/floating point (there is no floating point in
`cc64` at all — §3), field-width/precision modifiers (`%5d`, `%.2f`).

Argument count and type are checked against the format string at
compile time — too few/too many arguments, a pointer where `%d`/`%x`/
`%c` expects a plain value, or a non-`char*` where `%s` expects one are
all compile-time errors.

---

## 16. Light type checking

`cc64` does not implement a full C type system, but it does catch a
number of common mistakes at compile time rather than letting them
silently corrupt memory:

- Dereferencing a non-pointer.
- `pointer + pointer` (not a valid operation).
- `int - pointer` (only `pointer - pointer` and `pointer ± int` are
  valid).
- Passing a plain value where a function expects a pointer parameter,
  or vice versa.
- Returning a plain value from a function declared to return a
  pointer, or vice versa.
- Using a struct/union by value where there is no single
  register-sized value for it to occupy.
- `.` used on a pointer or a bare array (use `->`, or index the array
  first).
- An unknown member name.
- A struct/union used by value before it's been fully defined (only
  pointers to an incomplete struct/union are allowed — §7).

`0` is always accepted as a valid pointer value in every one of these
checks. Two known, accepted gaps in signedness propagation are covered
in §3.1.

---

## 17. Preprocessor: `#include`

`#include` is the **only** preprocessor directive `cc64` supports —
there is no `#define`, no `#ifdef`/`#ifndef`, no conditional
compilation of any kind. It is handled entirely in the lexer, as
straight text splicing, before any real tokenizing happens.

```c
#include <string.h>     /* angle brackets */
#include "local.h"      /* quoted */
```

- **`"quoted"`** is searched next to the file doing the including
  first, then falls back to the same search path as `<angle-bracket>`.
- **`<angle-bracket>`** is searched via any `-I` directories given on
  the command line (§1, checked in the order given), then the
  automatically-detected `lib/` directory next to the `cc64` binary
  itself.
- Every file is **implicitly include-once**, tracked by resolved
  absolute path — headers never need manual include guards.

---

## 18. Memory model / zero-page usage

`cc64` uses exactly six zero-page bytes, and only these — chosen
because, per the KERNAL's own memory map, `$02` and `$FB`-`$FE` are
the only zero-page bytes confirmed genuinely unused by BASIC/KERNAL:

```
$02/$03  __zpAP   effective-address pointer (array/deref/pointer-index/peek/poke)
$FB/$FC  __zpR    primary register / function return value
$FD/$FE  __zpR2   secondary operand register
```

An earlier version also used `$F3`-`$FA`, on the mistaken assumption
they were safe scratch space adjacent to the known-safe range. They
aren't: `$F3`/`$F4` is the KERNAL's current-line-in-color-RAM pointer
(updated by `CHROUT` on every printed character), and `$F5`/`$F6` is
the keyboard-matrix-to-PETSCII conversion table pointer (touched by
the keyboard scan on every background IRQ, ~60x/sec, regardless of
what the program does). This caused a real memory-corruption bug —
`puts()` kept its walking pointer live across multiple `JSR CHROUT`
calls in a loop, exactly the scenario where that collision corrupts
memory mid-string. Everything that doesn't strictly need indirect
`(zp),Y` addressing now lives in ordinary non-zero-page RAM instead
(this program's own memory is never contested, unlike zero page).
Don't assume any other zero-page address is free without checking this
section first.

All compiler-generated assembly symbols use C's own reserved-identifier
namespace (`__fn_`, `__g_`, `__L`, `__rt_`, `__zp` — leading double
underscore, or underscore + capital), so they can never collide with a
valid user identifier.

---

## 19. PETSCII and character case

The C64 boots into its default "uppercase/graphics" character set,
where only `$41`-`$5A` renders as letters (and only uppercase) — the
`$C1`-`$DA` range renders as **graphics symbols**, not lowercase
letters, until the machine is switched into its second character set
via PETSCII control code 14. `cc64` emits that switch once,
automatically, as the very first thing every compiled program does
(before `main` runs), so this never needs to be thought about.

With that switch in place, `putchar`/`puts`/`printf`'s `%c`/`%s` all
PETSCII-convert **at runtime**, via one shared conversion routine —
text prints in the same case it was written in the source, regardless
of whether it came from a string literal, a runtime-built buffer, or a
computed character value. String literals themselves are stored as
their exact raw bytes (§13), not pre-converted at compile time,
specifically so this one runtime routine can handle every source
uniformly.

**Plain `char` variables/arrays are never auto-converted** — a byte is
just a byte until it passes through the print path. Only `putchar`/
`puts`/`printf` apply PETSCII mapping.

---

## 20. Standard library

Both headers below are **header-only**: `#include` splices their text
directly into the including program (§17), so every function pulled in
is fully compiled whether it's called or not — there's no linker to
strip unused ones. Immaterial for a handful of small functions; worth
knowing if this library ever grows substantially.

### `lib/string.h`

`strlen`, `strcpy`, `strcat`, `strcmp`, `strchr`, `memset`, `memcpy` —
matching the real C library's names and contracts wherever `cc64`'s
type system allows it, including "no bounds checking, ever": the
caller is responsible for destinations being large enough, exactly
like the real thing.

### `lib/print.h`

| Function | Description |
|---|---|
| `print_int(x)` | Signed decimal. |
| `print_uint(x)` | Unsigned decimal — same digit-collection algorithm as `print_int`, minus sign handling. Needs `unsigned int` (§3.1) to exist as a real type, so it's a genuinely separate function, not a cast of `print_int`. |
| `print_hex(x)` | 4 hex digits, the exact bit pattern regardless of sign. |
| `newline()` | Prints a newline. |

There is no `%u`/`print_uint` equivalent for anything narrower than
`int`, and no `print_uint` was ever added prior to `unsigned` existing
as a real type — there was no correct way to write "divide this bit
pattern as if unsigned" before then.

---

## 21. What's not supported

Collected in one place for reference — all of these are open items in
[`../ROADMAP.md`](../ROADMAP.md), not planned regressions:

- Pointer-to-pointer (`int **`).
- Function pointers.
- Arrays of pointers.
- Array *parameters* written with `[]` syntax (use `type *name`
  instead — identical result).
- By-value `struct`/`union` parameters and return values (use
  `struct Tag *`/`union Tag *`).
- `struct`/`union` members that are themselves a struct/union held by
  value, or an array.
- Multi-dimensional arrays.
- Floating point (`float`/`double`).
- `long`/`short` — `int` is always exactly 16 bits.
- `_Bool`/`bool` as named types (though `int`/`char` already serve as
  truth values everywhere one is needed).
- Real variadic user-defined functions (a `...` parameter) — see §15
  for how `printf` gets away without this machinery existing at all.
- Any preprocessor directive besides `#include` — no `#define`, no
  `#ifdef`/`#ifndef`, no conditional compilation.
- `void *` (only typed pointers exist).
- `sizeof`, the ternary `?:` operator, and the comma operator.
- Anonymous inline `struct`/`union` definitions combined with a
  `typedef` in one statement.

---

## 22. Testing this compiler

There is no VICE/x64 (or other third-party 6502 emulator) dependency
in `cc64`'s own test suite — verification is done via `bin/mini6502.py`,
a thin CLI wrapper around `../asm/examples/mini6502.py`'s C64Machine (a
purpose-built 6502 + C64Machine emulator) that traps `$FFD2` (`CHROUT`)
in software and simulates the same zero-page KERNAL-poisoning behavior
described in §18, so a regression back into that territory shows up in
testing rather than only on real hardware. See `../README.md`'s
"Testing" section for the full list of `tests/*.c` files and what each
one exercises, and
`../ROADMAP.md` for open language/tooling ideas not yet scheduled.
