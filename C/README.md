# cc64 - a minimal C compiler for the Commodore 64

`cc64` compiles a small subset of C directly to 6502 assembly in the
exact syntax the `c64asm` assembler (`../asm/`) expects. This is built
incrementally: get a solid, verified minimal subset working first (a
step-1 "int/char, no pointers" core), then add features on top of a
foundation that's already known to generate correct code. Pointers,
full function recursion, `#include`, a small standard library, and
`struct` are all in now.

The source is split into one file per compiler phase under `src/`,
each with a substantial comment explaining what that phase does and
why - see "Source layout" below if you're reading this to learn how a
small compiler like this is put together, not just to use it.

## Building

Portable C99, no dependencies. A Makefile builds `cc64` from `src/*.c`:

```sh
make
(cd ../asm && make)   # builds ../asm/bin/c64asm, if you don't have it yet
```

(`make clean` removes the built binary.) Both build cleanly with
`clang` on Apple Silicon or `gcc`/`cc` on Linux. If you'd rather build
by hand without the Makefile: `cc -std=c99 -O2 -o cc64 src/*.c`.

## Using it

```sh
./cc64 program.c -o program.asm
./c64asm program.asm -o program.prg
```

or, to do both in one step:

```sh
./build.sh program.c program.prg
```

Load `program.prg` in VICE (or a real C64) the normal way; it's built
with a BASIC `10 SYS ...` loader stub, so `LOAD` then `RUN` works.

### Using the standard library

`#include <string.h>` and `#include <print.h>` (in `lib/`) are found
automatically with no setup, as long as `cc64` is invoked with a path
that has a `/` in it - `./cc64`, `../build/cc64`, `/opt/cc64/cc64`,
all fine - which covers every example in this README and `build.sh`.
(If `cc64` is on your `PATH` and invoked by bare name, there's no
portable, dependency-free way to recover its install location from
C99 alone, so that one case needs an explicit `-I`:)

```sh
./cc64 program.c -o program.asm -I /path/to/cc64/lib
```

`#include "local.h"` (quoted) looks next to the file doing the
including first, then falls back to the same search path as `<...>`
- see "The standard library" below for what's in `lib/`, and
`src/cc64.h`'s "HOW #include WORKS" note for the mechanism itself.

## Source layout

Each file below corresponds to one phase you'd recognize from a
compilers course; `src/cc64.h`'s own header comment gives the same
map with more detail on how the phases fit together, plus notes on
the compiler's overall architecture (why everything flows through a
"register", why there's no call stack, why parsing happens in two
passes). Reading the files in roughly this order is a reasonable way
to approach the codebase for the first time:

| File | What it does |
|---|---|
| `src/cc64.h` | Shared types and cross-module declarations; start here for the architecture overview |
| `src/lexer.c` | Source text -> tokens, and `#include` splicing |
| `src/ast.c` | The AST node constructor |
| `src/symtab.c` | Symbol tables, `struct` layouts, plus the minimal type inference used for pointer arithmetic and member lookup |
| `src/parser.c` | Recursive-descent parsing (tokens -> AST), and the two-pass driver (`pass_a`/`pass_b`) |
| `src/codegen.c` | Shared codegen utilities: emitting a line of assembly, label generation |
| `src/codegen_runtime.c` | The fixed 6502 runtime library (multiply, divide, comparisons, string printing) |
| `src/codegen_expr.c` | Expression codegen - the largest file, where most operators and pointer handling live |
| `src/codegen_stmt.c` | Statement codegen, storage layout, and the per-function frame save/restore routines that make recursion work |
| `src/main.c` | The command-line driver tying every phase together |
| `lib/string.h`, `lib/print.h` | The standard library - see below |


## What's supported

- **Types:** `int` (16-bit, signed), `char` (8-bit, **unsigned** - see
  below), `void`, single-level pointers to any of those (`int *`,
  `char *`, `struct Tag *`), `struct`, `union`, and `enum` (always just
  `int` - see "How enum works" below).
- **Declarations:** globals and locals, with an optional 1-D array
  form (`int a[10];`, `char buf[40];`, `struct Point pts[10];`), and
  an optional constant literal initializer for scalar (non-pointer,
  non-struct) globals (`int x = 5;`, `int y = -3;`). Array,
  pointer, and struct initializers on *globals* aren't supported yet
  (arrays and structs start zero-filled; give a pointer its value
  inside a function instead). Local pointer declarations *can* have
  an initializer, including a string literal (`char *s = "hi";`) -
  struct locals still can't, even though they're otherwise ordinary
  local variables (see "struct" below).
- **Functions:** typed parameters (including pointer parameters),
  typed return value (including pointer return types), forward calls
  in any order (any function can call any other regardless of which
  is defined first in the file - see "Two-pass compilation" below),
  and **full recursion**, direct or mutual - see "How recursion
  works" below for both the mechanism and its limits (a 256-byte
  per-call frame cap, and runtime overflow guards that halt with a
  clear on-screen error instead of silently corrupting memory when
  recursion runs away).
- **Pointers:** `&x` (address-of - works on scalars and array
  elements; safe on locals too, since there's no call stack for them
  to dangle from), `*p` (dereference, usable as both an rvalue and an
  assignment target, including `*p += n`, `(*p)++`, etc.), `p[i]`
  indexing through a pointer (not just through a true array),
  pointer arithmetic (`p + n`, `p - n`, `p++`/`p--`, all correctly
  scaled by `sizeof(*p)` - 2 bytes for `int*`, 1 for `char*`),
  pointer-minus-pointer (`p2 - p1`, giving an element count, not a
  byte count), and pointer comparisons (`<` `>` `<=` `>=` `==` `!=`,
  using an **unsigned** comparison since addresses aren't signed
  quantities - unlike plain `int` comparisons, which are signed).
  Arrays decay to a pointer to their first element wherever a pointer
  is expected (passing an array as a function argument, assigning an
  array to a pointer variable, etc.).
- **`struct`:** `struct Tag { int/char/pointer members; };`, parsed
  and fully laid out (every member's byte offset, and the whole
  struct's size) at the point it's declared - members are packed with
  no padding, since this is a byte-addressed machine with no alignment
  requirements to satisfy. `.` for direct access, `->` for access
  through a pointer (parsed as sugar for `(*p).member`, so both share
  one code path), `&s`/`&s.member`/`&p->member` for address-of, and
  arrays of structs (`pts[i].x`) with both compile-time and runtime
  indexing. Self-referential structs work (`struct Node { struct Node
  *next; };`), and so do two structs that reference each other
  regardless of which is written first in the file - see "How structs
  work" below. **Structs are pointer-only across function boundaries**
  (a parameter or return type must be `struct Tag *`, never `struct
  Tag` by value) and **members must be `int`, `char`, or a pointer**
  (never another struct held by value, and never an array) - both
  deliberate scope boundaries for this step, not oversights, and both
  give a clear compile-time error rather than silently doing the
  wrong thing.
- **`union`:** `union Tag { int/char/pointer members; };` - same
  syntax, same member restrictions, and the exact same `.`/`->`/`&`
  access and pointer-only-across-function-boundaries rules as `struct`
  (they share essentially all of the same machinery - see "How union
  works" below), except every member starts at byte offset 0 instead
  of packing sequentially, and the union's own size is its widest
  member's width, not their sum.
- **`enum`:** `enum [Tag] { NAME [= value], ... };`, with a tag
  optional (unlike `struct`'s own required one) and C's usual auto-
  increment (each enumerator defaults to one more than the last, zero
  for the first, unless given an explicit `= value`). Each enumerator
  becomes a compile-time integer constant usable anywhere one is
  needed: in ordinary expressions, as a `switch` `case` value, as an
  array size, or as a global initializer - see "How enum works" below
  for exactly how far that goes and the one real restriction it has
  (an enumerator's own value must be a literal, not another
  enumerator's expression).
- **`typedef`:** `typedef <existing type> Name;` - `Name` becomes a
  usable alias for that exact type (`int`, `char *`, `struct Tag`,
  `union Tag`, `enum Tag`, or even another typedef) absolutely
  everywhere a type can appear: variables, parameters, return types,
  struct/union members, array element types, even another `typedef`'s
  own underlying type. `typedef struct Tag Tag;` (the same tag name for
  both) is the common "drop the `struct` keyword afterward" idiom and
  works fine. **The underlying type must already exist as a plain type
  reference** - `typedef struct { ... } Name;` (an inline anonymous
  definition combined with the typedef in one statement) isn't
  supported, since this compiler has no anonymous-struct-definition
  syntax at all in the first place (unlike `enum`, `struct`/`union`
  always require a tag); define the tag separately first, then
  `typedef` it. `typedef` is top-level only, the same restriction
  `struct`/`union`/`enum` definitions already have - see "How typedef
  works" below for the full design.
- **Statements:** `if`/`else`, `while`, `do`/`while`, `for`, `switch`/
  `case`/`default` (integer-constant cases only, real fallthrough,
  `break` exits the switch - see "How switch works" below), `break`,
  `continue` (both aware of the loop-vs-switch distinction - `break`
  exits the nearest enclosing loop *or* switch, `continue` always
  means the nearest enclosing *loop*, skipping over any switch in
  between, matching real C), `return`, blocks, local declarations
  (anywhere in a block), empty statement.
- **Operators:** `+ - * / % & | ^ ~ ! << >> && || == != < > <= >=`
  `= += -= *= /= %= &= |= ^= <<= >>=` and pre/post `++`/`--`.
  `/` and `%` are correctly signed (truncate toward zero; remainder
  takes the sign of the dividend, matching C99).
- **Literals:** decimal and `0x` hex integers, `'c'` char literals
  with `\n \t \\ \' \" \0` escapes, and `"string"` literals, which are
  real `char*` values now (interned as data, not just accepted by
  `puts()` - see below).
- **Builtins:** `putchar(x)`, `puts(s)` (accepts any `char*` - a
  literal, a variable, a buffer you built at runtime, all walked at
  runtime up to the first zero byte), `peek(addr)`, `poke(addr, val)`,
  `printf(fmt, ...)` (`fmt` must be a string literal - see "How printf
  works" below for the full specifier list and why that restriction is
  what makes this possible at all without real variadic functions).
- **Light type checking:** the compiler doesn't do full C type
  checking, but it does catch some common mistakes at compile time
  rather than letting them corrupt memory silently: dereferencing a
  non-pointer, `pointer + pointer`, `int - pointer`, passing a plain
  value where a function expects a pointer (or vice versa), returning
  a plain value from a function declared to return a pointer (or vice
  versa), using a struct by value where there's no single register-
  sized value for it to go, `.` on a pointer or bare array (use `->`,
  or index the array first), an unknown member name, and a struct used
  by value before it's been fully defined (only pointers to an
  incomplete struct are allowed - see "How structs work" below). `0`
  is always accepted as a valid pointer value ("null") in these checks.
- **`#include`**, both `"quoted"` (searched next to the including
  file, then falling back to the same path as angle brackets) and
  `<angle-bracket>` (searched via `-I` directories and an
  automatically-detected `lib/` next to the compiler itself - see
  "Using the standard library" above). Every file is implicitly
  include-once by resolved path, so headers don't need manual include
  guards. This is the *only* preprocessor functionality - no `#define`,
  no `#ifdef`, nothing else starting with `#` - see `src/cc64.h`'s
  "HOW #include WORKS" note for the (simple) mechanism.
- **A small standard library** (`lib/string.h`, `lib/print.h`) - see
  "The standard library" below for the full list.

## Not supported yet (planned for later steps)

Pointer-to-pointer, function pointers, arrays of pointers, array
*parameters* written with `[]` syntax (use `type *name` instead - it
receives exactly the same decayed pointer), by-value struct/union
parameters and return values (use `struct Tag *`/`union Tag *`
instead), struct/union members that are themselves a struct/union-by-
value or an array, multi-dimensional arrays, floating point, and real
variadic functions
(a user-defined function can't take a `...` parameter the way `printf`
does - see "How printf works" below for how `printf` itself gets away
without that machinery existing at all). The preprocessor is limited
to `#include` - no `#define`, no macros, no conditional compilation.

## Design notes

### How recursion works

Every function's parameters and locals still get **fixed, static
storage** (like old-style non-reentrant compilers) - reading or
writing a variable is a plain absolute load/store, with no frame
pointer or stack-relative addressing anywhere in expression codegen.
Recursion is layered *on top of* that model rather than replacing it:
for every function, the compiler emits a `pushframe` routine (copy
that function's entire parameter+local block onto a software call
stack) and a `popframe` routine (copy it back), and wraps every call
site with them - save the callee's current frame, write the new
arguments, `JSR`, then restore. When a recursive chain unwinds, each
level finds its variables exactly as it left them, even though every
level used the same physical addresses. The long comment above
`emit_function()` in `src/codegen_stmt.c` walks through a factorial
call step by step if you want to see the mechanism in motion.

Two practical limits fall out of the design, both enforced rather
than left as silent traps:

- **A function's frame (parameters + locals, including local arrays)
  can't exceed 256 bytes** - the frame copy loops count with the
  6502's 8-bit Y register. Exceeding it is a *compile-time* error
  suggesting the usual fix (make large local arrays global). This
  doubles as a performance guard rail: every call copies the whole
  frame twice, so a huge frame would make every call painfully slow
  anyway.
- **Runaway recursion halts with an on-screen error** instead of
  silently corrupting memory. The runtime checks three exhaustion
  risks on every call: the call stack outgrowing its 4 KB buffer, the
  6502's own 256-byte hardware stack (which holds one `JSR` return
  address per open call - the binding limit for deep recursion, at
  roughly a hundred levels), and the expression operand stack (whose
  entries can now be held across recursive calls - the `n` in
  `n * fact(n - 1)` stays parked for the whole recursion beneath it).
  Any of the three prints a `CC64 RUNTIME ERROR` message and stops.

A software *operand* stack is still used for evaluating nested
expressions (`a * (b + c)` etc.), exactly as before - it's
independent of the call-frame machinery, though as noted above the
two now interact when an operand is held across a recursive call.

### How structs work

A `struct Tag { ... }` is parsed and fully laid out (every member's
byte offset, and the whole struct's total size) entirely while
scanning declarations, before any function body is really parsed -
member declarations are just a list, with no expressions or
statements that would need deferring. Members are restricted to
`char`, `int`, and pointers - never another struct held by value, and
never an array - specifically so this layout computation stays simple:
no padding or alignment to reason about either, since this is a
byte-addressed 8-bit machine where every type's size is exactly 1 or
2 bytes.

A struct *variable* is nothing exotic once it's declared - just a
fixed-size block of memory at one address, the same way an array
already was. `structVar.member` compiles to "load from that address
plus this member's fixed offset", which for a global or local struct
(a compile-time-known address) is exactly as cheap as accessing any
other plain variable - no runtime indirection at all. `p->member` (or
the equivalent `(*p).member` - the parser desugars `->` into that
form directly, so codegen only ever handles one shape) is the one
case that genuinely needs a runtime address computation, and it
reuses the exact same `__zpAP`-based machinery pointer dereferencing
and array indexing already needed - a struct member reached through a
pointer or an array is really just "the usual indirect access, plus a
constant offset added on top."

Self-referential structs (`struct Node { int val; struct Node *next;
};`) and structs that reference each other regardless of which is
written first work because a `struct Tag` name is resolved to an
entry in the struct table the moment it's *seen* - creating an
incomplete placeholder entry if that's the first time - rather than
requiring the tag to already be fully defined. A pointer to a struct
never needs to know that struct's size (a pointer is always 2 bytes
no matter what it points to), so `struct Node *next;` inside `Node`'s
own body works even though `Node` itself isn't finished being defined
yet. Only *using* a struct by value - a variable of that type, which
needs a known size to allocate storage for - requires it to be fully
defined by that point; see `find_or_create_struct_tag()`'s comment in
`src/symtab.c` for the full mechanism.

**Structs are pointer-only across function boundaries** (parameters
and return values must be `struct Tag *`) rather than supporting
by-value passing. Real C supports both; by-value would mean copying
the whole struct at every call boundary, which is genuine additional
machinery for a pattern (linked lists, trees, anything built with
`->`) that pointer-passing already covers naturally - shipping that
first, cleanly, seemed better than shipping both halfway.

### How union works

`union Tag { ... }` reuses essentially all of `struct`'s own machinery
- the same `StructDef`/`StructMember` types (`cc64.h`), the same
`parse_struct_or_union_def()` (`src/parser.c`, one function handling
both keywords, parameterized by which one this definition actually
used), the same member-type restrictions, and the exact same `.`/`->`/
`&` member-access codegen, completely unchanged, because that codegen
was already generic on "read from base address plus this member's
offset" and never had any reason to care whether that offset came from
a struct's or a union's own layout rule. **The one real difference is
how a member's offset (and the whole aggregate's size) is computed**:
a struct member gets the next free byte, accumulating as it goes; a
union member always starts at offset 0 - every member overlaps every
other one, which is the entire point of a union - and the aggregate's
total size is its single WIDEST member's width, not their sum.

`struct` and `union` tags **share one namespace**, matching real C: you
can't have both a `struct Foo` and a `union Foo` in the same program,
and using the wrong keyword for an already-fully-defined tag
(`union Foo` where `Foo` is a real `struct`) is a compile-time error
immediately, in `parse_type_prefix()`. A tag that's only been forward-
referenced so far (e.g. `union Foo *p;` before `Foo`'s own
`{ members }` has been seen anywhere) is more lenient, the same way an
incomplete struct tag already is - this compiler doesn't chase down
every possible inconsistency between an early forward reference's own
keyword and the tag's eventual real definition, since a pointer's
generated code (always just 2 bytes) can't actually go wrong from that
mislabeling either way; only a real, completed definition's keyword is
ever treated as authoritative.

**Unions are pointer-only across function boundaries and can't be a
struct/union member by value**, for the identical reasons (and via the
identical checks) `struct` already has both restrictions - see
"Structs are pointer-only..." just above. A "tagged union" pattern (a
struct holding a union's current value alongside a discriminator field
telling you which member is meaningful) still works fine in cc64, just
via a pointer member (`union Tag *data;`) rather than one held by
value, the same workaround a self-referential struct already needs.

### How typedef works

`typedef <type-prefix> Name;` resolves ENTIRELY at parse time, inside
`parse_type_prefix()` itself (`src/parser.c`) - the same function every
other kind of declaration already calls to parse its own type prefix.
By the time parsing moves past a use of `Name`, it has already been
substituted for whatever `Name` actually means (`int`, `char *`,
`struct Tag`, ...); nothing downstream - not the AST, not codegen, not
even `infer_type()` - ever sees `Name` or knows a typedef was involved
at all. This is why `typedef` needed no codegen changes whatsoever
(the same was true of `enum`, for the same underlying reason: a
compile-time-only substitution, with zero runtime representation).

**The real difficulty was parsing, not codegen.** A typedef name is a
plain identifier - lexically indistinguishable from a variable or
function name - so every place this compiler decides "am I looking at
a declaration or an expression?" now has to check more than just the
current token's KIND (`int`/`char`/`struct`/... are unambiguous
keywords) - it has to check the token's actual TEXT against the table
of registered typedef names too. `cur_is_type()` (`src/parser.c`) is
that combined check, and replaced every place that used to ask
`is_type_kw(cur()->kind)` alone: `pass_a()`'s top-level dispatch and
parameter-type parsing, and `parse_stmt()`'s local-declaration and
`for`-loop-init detection. (Struct/union member parsing needed no such
change - a struct/union body is unconditionally a list of member
declarations already, nothing else, so it can call
`parse_type_prefix()` directly without first asking "is this a
declaration.")

**Chaining works** (`typedef MyInt AnotherInt;`) because
`parse_type_prefix()`'s typedef branch is reached by exactly the same
path a fresh `int`/`struct Tag`/... reference would be - it doesn't
matter that `MyInt` itself came from an earlier typedef, only that
`cur_is_type()`/`find_typedef()` can look it up by name right now.

**A typedef'd pointer type can't gain an extra `*` at a use site**
(`typedef char *String; ... String *sp;` is rejected as pointer-to-
pointer, which this compiler doesn't support at all) - the typedef
branch handles its own trailing-`*` check separately from every other
branch's shared one, specifically so it can tell "this name was
already a pointer" apart from "this name is a plain type gaining its
first pointer star here."

**`typedef` is top-level only**, the same restriction `struct`/`union`/
`enum` definitions already have - `g_typedefs` (`cc64.h`) is one flat,
file-wide table with no per-function scoping at all, matching how this
compiler already handles every other kind of collision (rejecting a
name reused across categories, rather than implementing real lexical
scoping where an inner declaration can shadow an outer one). The
underlying type must also already exist as a plain type reference, not
an inline anonymous definition combined with the typedef in one
statement (`typedef struct { ... } Name;`) - see the "What's supported"
list above for why (this compiler has no anonymous-struct-definition
syntax at all, `enum`'s own optional tag aside).

### How enum works

`enum [Tag] { NAME [= value], ... };` never creates a real distinct
type at runtime - every enumerator is just a compile-time `int`
constant (see `EnumConst` in `cc64.h`), and `enum Tag` used later as a
type (a variable, parameter, return type, or struct member) is nothing
more than an alias for `int`: no new storage representation, no
runtime tag, no way to tell at runtime that a value "came from" an
enum at all - exactly like real C. Two consequences fall out of that
directly: an enum constant can be used absolutely anywhere a plain
`int` constant makes sense (ordinary expressions, a `switch` `case`
label, an array size, a global initializer), and a `switch` over an
`enum`-typed value works with zero special-casing in `codegen_stmt.c`
- it's already just switching on an `int`.

**The tag is optional**, unlike `struct`'s own required one -
`enum { RED, GREEN, BLUE };` (no tag at all) is completely valid, and
common, since the constants themselves are what typically matter, not
a name for the type. A tag only needs to exist at all if you want to
later declare something `enum Tag`-typed; giving one costs nothing
otherwise. **Unlike a struct tag, an enum tag can never be forward-
referenced** - real C has no equivalent of `struct Tag;`'s incomplete
forward declaration for enums, so `enum Tag` used as a type requires
that tag to be *fully* defined earlier in the file already (checked in
`parse_type_prefix()`, `src/parser.c`); there's no "incomplete enum
pointer" escape hatch the way an incomplete struct has, because enums
are never pointer-only-until-complete in the first place - there's
nothing about an enum's own "size" that a pointer could sidestep.

**An enumerator's own value must be a literal**, not an expression
referencing an earlier enumerator in the same `enum` (`B = A + 1`
doesn't work) - the same "no constant-expression evaluator yet"
restriction a global initializer, a `case` label, and an array size
all already have (see `parse_const_value()`, `src/parser.c`, which is
also what lets all three of *those* contexts accept an enum constant
by name, not just a bare literal, in the first place). Omitting
`= value` falls back to C's usual auto-increment: one more than the
previous enumerator, or zero for the very first.

Because top-level declarations (globals, function signatures, struct
members) are all resolved in a single forward pass through the file
(`pass_a()`, see "Two-pass compilation" below), an `enum` must be
*defined* before any TOP-LEVEL use of its tag as a type or its
constants as an array size/initializer - unlike enum constants used
inside a function body (as an ordinary expression, or a `switch` case),
which can reference an `enum` defined anywhere in the file at all,
since function bodies aren't parsed for real until `pass_b()`, which
only starts once `pass_a()` (and therefore every `enum` in the file)
is already finished.

**`enum` definitions are top-level only**, same restriction `struct`
has - there's no such thing as a local `enum { ... };` declared inside
a function body. A constant defined by a top-level `enum`, though, can
still be used to size a *local* array just fine; only the *definition*
itself needs to be at the top level, not every place its constants get
used.

### How switch works

`switch (expr) { ... }` compiles to a straight-line compare-and-branch
chain, not a jump table - there's no dispatch-table machinery anywhere
in this compiler, so `switch` is really syntax sugar over what you'd
otherwise write as a chain of `if (x == C1) ... else if (x == C2) ...`.
`expr` is evaluated once into the primary register and left there
untouched through the whole chain: every `case` label has to be a
constant integer literal (optionally negated) - the same restriction a
global variable's initializer already has, and for the same underlying
reason - so nothing between the initial evaluation and the last
comparison ever needs to re-evaluate an expression and disturb it.
`default` doesn't have to be written last (or be present at all); its
comparison always runs after every `case` has already been checked,
regardless of where it appears in the source, matching real C.

**Fallthrough is real**, the same as C: without an explicit `break`,
execution continues into the next label's statements rather than
exiting the switch. This falls out for free from how the body compiles
- every `case`/`default` is just a label, and the statements between
labels are emitted in source order with nothing in between them, so
"keep going into the next block" is simply what happens if nothing
jumps elsewhere first.

**`break` and `continue` are aware of the loop-vs-switch distinction.**
`break` exits the *nearest* enclosing loop or switch, whichever is
innermost - real C's own rule. `continue` is narrower: it always means
the nearest enclosing *loop*, skipping past any switch frames on top of
it, since a switch has no continue target of its own. A `switch`
directly inside a `while`/`for`/`do`-`while` body needs exactly this
distinction to behave correctly - `case N: continue;` must reach the
loop, not just fall through to the switch's own end - and it's checked
in `codegen_stmt.c`'s `N_SWITCH`/`N_CONTINUE` handling, not left to
accident.

**`case`/`default` labels are only recognized directly inside a
switch's own `{ }`**, not nested inside an inner block or an `if` -
Duff's-device-style tricks (a label reached by falling into the middle
of a loop body from outside it) aren't supported. Real C's grammar
technically allows labels anywhere a statement can appear; skipping
that keeps both the parser and codegen's "build the compare chain,
then walk the body in order" strategy considerably simpler, at the cost
of a style of C essentially nothing outside deliberately obscure code
relies on.

### How printf works

`printf(fmt, ...)` exists without cc64 having any real variadic-function
machinery at all - no `...` parameters, no `va_list`/`va_arg`, nothing
resembling a real C calling convention for a variable argument count.
The trick: **`fmt` must be a string literal**, not a variable or any
other runtime expression. Since the format string's exact contents are
therefore known to the compiler itself, at compile time, a `printf()`
call doesn't need to be compiled into anything that parses a format
string at runtime - the compiler parses it once, right there in
`codegen_expr.c`'s `gen_printf_call()`, and emits a fixed, specific
sequence of ordinary calls for that one call site: a literal run of
text between specifiers becomes an interned string literal plus
`JSR __rt_puts` (exactly what a separate `puts("...")` call for that
text would generate), and each specifier evaluates the next actual
argument and dispatches to whichever runtime routine matches it. In
other words, `printf("x=%d, y=%s\n", x, name)` compiles to essentially
the same code `putchar`/`print_int`-chaining by hand always could have
- `printf` just writes that chain out for you from the format string,
at compile time, rather than you writing it out by hand every time.

Supported specifiers: `%d` (signed decimal - see `print_int()` in
`lib/print.h`, which this shares its actual digit-collection algorithm
with, reimplemented directly in 6502 assembly as `__rt_print_int16`
rather than requiring `#include <print.h>`), `%x` (4 uppercase hex
digits, same as `print_hex()`), `%c` (one character, PETSCII-converted
the same way `putchar()` converts its argument), `%s` (a `char*`
argument, walked the same way `puts()` walks one), and `%%` for a
literal `%`. **`%u` is deliberately not supported**, for the exact same
reason `lib/print.h` has no `print_uint()` - see that header's own
comment for the full explanation of why cc64's signed-only `int` makes
an unsigned decimal formatter impossible to write correctly. Anything
else (`%f`, width/precision modifiers like `%5d`, `%.2f`) isn't
supported either - there's no floating point in cc64 at all, and
nothing else here has ever supported field widths or precision.

Argument count and type are checked against the format string at
compile time, the same way an ordinary function call's arguments are
checked against its declared parameters: too few or too many arguments
for the specifiers present, a pointer where `%d`/`%x`/`%c` expects a
plain value, or a non-`char*` where `%s` expects one, are all compile-
time errors rather than runtime surprises.

### Two-pass compilation

`cc64` scans all top-level declarations first (function signatures
and globals) before generating any code. That means you don't need
forward declarations for the common case - any function can call any
other function anywhere in the file. (You *can* still write a
prototype like `int foo(int x);` if you want one for documentation;
it's just not required.)

### `char` is unsigned

To keep the first step simple, `char` is treated as unsigned 8-bit
(no sign extension logic needed). This is a common choice for small
8-bit-target C compilers and matches how many people use `char` on
this platform anyway (as a byte type, not as `signed char`).

### PETSCII and case

The C64 boots into its default character set ("uppercase/graphics"),
where only the codes in the `$41`-`$5A` range render as letters (and
only as uppercase) - the `$C1`-`$DA` range the assembler's own
`ascii_to_petscii()` uses for uppercase source characters is
**graphics symbols**, not lowercase letters, in that default mode.
Those codes only render as letters once the C64 has been switched
into its second character set, via PETSCII control code 14. `cc64`
emits that switch once, automatically, as the very first thing your
program does (before `main` even runs), so you don't have to think
about it. With that in place, `putchar(x)` and `puts(s)` both
PETSCII-convert at **runtime** (via the same small conversion
routine), so text displays in the **same case you wrote it** whether
it came from a string literal, a buffer you built yourself, or a
single computed character - no flipping, no workaround needed. String
literals are stored as their exact raw bytes (not pre-converted at
compile time) precisely so that this one runtime routine handles
every case uniformly. Plain `char` variables/arrays are **not**
auto-converted (a byte is just a byte until you print it) - only the
print path applies PETSCII mapping.

(This one was actually caught after the fact: my first version didn't
emit the charset switch, so all-uppercase test output rendered as
graphics characters on real hardware. My verification emulator didn't
catch it either, because it was decoding `$C1`-`$DA` as lowercase
unconditionally rather than modeling the two real character sets and
the control code that switches between them - both the compiler and
the emulator are fixed now.)

### Zero-page usage

```
$02/$03  __zpAP   effective-address pointer (array/deref/pointer-index/peek/poke)
$FB/$FC  __zpR    primary register / function return value
$FD/$FE  __zpR2   secondary operand register
```

Only these 6 bytes, and only these. Per the KERNAL's own memory map,
`$02` and `$FB`-`$FE` are the *only* zero-page bytes confirmed
genuinely unused by BASIC/KERNAL. An earlier version of this compiler
also used `$F3`-`$FA` for scratch registers, on the assumption that
they were "adjacent to the known-safe `$FB`-`$FE` range" and therefore
probably fine - they looked free (nothing obviously touches them) but
weren't: `$F3`/`$F4` is the current-line-in-color-RAM pointer, updated
by CHROUT itself whenever it colors a printed character, and `$F5`/
`$F6` is the keyboard-matrix-to-PETSCII conversion table pointer,
touched by the keyboard scan on every background IRQ (~60x/sec)
regardless of what the program does. `puts()` kept its walking pointer
live across multiple `JSR CHROUT` calls in a loop - exactly the
scenario where that collision corrupts memory mid-string and produces
garbage output after the first character. This shipped and was caught
by real-hardware testing, not by the verification here, since the
purpose-built emulator didn't model real KERNAL zero-page usage either
(it now poisons those bytes on every simulated `CHROUT` call
specifically so this class of bug can't slip through silently again).

Everything that doesn't strictly need `(zp),Y` indirection (the old
`__zpAP2`/`__zpT0`/`__zpT1` scratch, and the operand-stack pointer)
now lives in ordinary, non-zero-page RAM instead, which is always safe
since it's memory this program exclusively owns - only zero page is
contested territory. The operand stack itself moved from zero-page
indirect-indexed addressing to plain `absolute,X` addressing (with the
stack index held in a regular byte, not zero page) to make this
possible; it now holds 128 slots instead of 256, which is still far
more than any realistic expression nests.

All compiler-generated assembly symbols (`__fn_`, `__g_`, `__L`, `__rt_`, `__zp`)
live in C's own reserved-identifier namespace (leading double
underscore, or underscore + capital), so they can never collide with
a valid user identifier.

### Calling convention

Arguments are copied into the callee's fixed parameter slots, then a
plain `JSR`. Return values are left in the primary register `__zpR`
at `RTS` time - every `return expr;` just computes `expr` into `__zpR`
and returns; there's no shared epilogue to tear down, since there's
no stack frame to unwind. Pointer parameters/return values use this
exact same mechanism - a pointer is just a 16-bit value like an `int`,
always stored in the full 2-byte slot regardless of what it points to
(a `char*` variable itself takes 2 bytes, even though each byte *it
points to* is 1 byte).

### The standard library

`lib/string.h`: `strlen`, `strcpy`, `strcat`, `strcmp`, `strchr`,
`memset`, `memcpy` - matching the real C library's names and
contracts (including "no bounds checking, ever" - the caller is
responsible for destinations being big enough, exactly like the real
thing) everywhere cc64's type system allows it.

`lib/print.h`: `print_int` (signed decimal), `print_hex` (4 hex
digits, the exact bit pattern regardless of sign), and `newline`.
Deliberately missing: `print_uint`. cc64's `int` is always signed -
there's no unsigned type and no cast operator - so there's no correct
way to decimal-print a 16-bit value in the 32768-65535 range using
cc64's own (always-signed) `/`, `%`, or `<`/`>`; shipping a
`print_uint` built from ordinary cc64 code would silently misprint
exactly that range. `print_hex` doesn't have this problem (see its
comment in `lib/print.h` for why bitwise `&`/`>>` are safe here when
`/` and `%` aren't) and covers most of the same real need - inspecting
a raw 16-bit value - so it's the one shipped instead.

Both headers are **header-only**: `#include` splices their text
directly into your program (see "HOW #include WORKS" in `src/cc64.h`),
so every function you include gets fully compiled into your program
whether you call it or not - there's no linker to strip the unused
ones out. Immaterial for a handful of small functions; worth knowing
if this library ever grows into something bigger.

## Testing

There's no VICE/x64 or other 6502 emulator in this environment, so
verification here was done with a small purpose-built emulator,
`mini6502.py`, that implements exactly the opcode/addressing-mode
subset `cc64` emits and traps `$FFD2` (CHROUT) in software. Since a
real zero-page collision with CHROUT/the KERNAL shipped once already
(see "Zero-page usage" above) without this emulator catching it, it
now also poisons the zero-page bytes CHROUT and the keyboard-scan IRQ
are documented to touch (`$F3`-`$FA`) on every simulated CHROUT call,
so a future regression back into that territory would show up here
instead of only on real hardware:

```sh
python3 mini6502.py program.prg program.lst
```

(`c64asm --listing program.lst` produces the listing it needs to find
the real code entry point, skipping the non-executable BASIC stub.)

`tests/features.c` is a comprehensive check covering signed
arithmetic and truncation rules, bitwise ops and shifts, all six
comparisons (including negative operands), `&&`/`||` short-circuit
evaluation (verified via a side-effect counter), non-recursive
function calls, array fill/read/compound-assign/inc-dec, `peek`/
`poke`, `break`/`continue`, and pre/post `++`/`--`. `tests/pointers.c`
covers `&`/`*`, pointer arithmetic scaling (`int*` vs `char*`),
pointer-minus-pointer, unsigned pointer comparisons, arrays decaying
to pointers across a function call, a classic pointer-swap, a
function returning a pointer, and a string literal used as a real
runtime `char*` value copied through a hand-written `copy_str`. Every
computed value in both was checked against hand-calculated expected
output. `tests/recursion.c` covers direct recursion (factorial),
double recursion (fibonacci - an operand held on the expression stack
across an entire recursive subtree), mutual recursion, recursion with
per-level local arrays that must survive inner calls, recursive
pointer walking, 80-deep recursion, and a regression check for a
nested array-target assignment bug found while building the frame
machinery. `tests/include.c` (with the small local header
`tests/testinc.h`) covers both quoted and angle-bracket `#include`,
include-once behavior (the same header is pulled in three times
across the two files - directly, repeated, and via a nested include -
and must not cause a redefinition error), and exercises every
function in `lib/string.h` and `lib/print.h`, including the
sign-extension edge cases in `print_hex` (checked at `-1` and `-4096`,
not just positive values). `tests/structs.c` covers direct struct
member access on locals and globals (both the compile-time
"label+offset" fast path), `->` through a pointer, `&s.member`/
`&p->member`/`&arr[i].member` (address of a member, not the whole
struct/element), struct parameters and pointer returns, arrays of
structs with both constant and runtime indexing, a self-referential
linked list summed both iteratively and recursively, two structs that
reference each other despite being declared in the "wrong" order for
a naive one-pass compiler, and mixed-width members (`char`+`int`+
pointer) to check offset computation. `tests/forward.c` checks that
forward/backward call references both work regardless of declaration
order. `tests/hello.c` is the basic smoke test for the whole pipeline
(BASIC stub, zero-page init, stack init, `puts`/`putchar`, PETSCII
case mapping). `tests/dowhile_switch.c` covers `do`/`while` (body runs
at least once even when the condition starts false, `continue`
re-testing the condition rather than skipping it, `break`), `switch`
(basic dispatch, `default` written *before* its `case`s still checked
last, negative case constants, real fallthrough with a missing
`break`), a `switch` nested directly inside a `while` loop (confirming
`break` exits only the switch while `continue` still reaches the
loop - the case most likely to silently break if the loop/switch
context stack were wrong), and a switch nested inside another switch
(confirming the inner switch's own `break` doesn't leak out to the
outer one). `tests/printf.c` covers every specifier (`%d`/`%x`/`%c`/
`%s`/`%%`), negative and zero values, hex output at all four digit
widths (a value needing 1, 2, 3, or 4 hex digits), a format string with
no specifiers at all and one with only a specifier and no literal text,
computed expression arguments (not just literals), and `%%` at both the
very start and very end of a format string. `tests/enum.c` covers an
anonymous enum (plain auto-increment) and a tagged one (explicit
values, including negative, mixed with auto-increment continuing from
the last explicit one), an enum constant used in ordinary expressions,
as every case in a real `switch` (including two cases sharing one
body), as both a global and a local array's size, as a global
initializer, and as a `struct` member's type and a function parameter's
type (`enum Status`) - the last two exercising the enum-tag-must-
already-be-defined check specifically, alongside separate error-path
checks (not part of the passing suite) for a misspelled/undefined tag,
a duplicate tag, a duplicate enumerator, an enumerator colliding with
a global or builtin name, and trying to assign to or take the address
of an enum constant. `tests/union.c` covers overlapping storage
(writing one member and reading a *different* one back, confirmed both
ways - a wide write then a narrow read, and a narrow write that only
touches part of a wider member, leaving the rest alone), a pointer
member, a union accessed through a pointer parameter (`union Tag *`),
and a struct holding a union via a pointer member (the "tagged union"
pattern, worked around the same way a self-referential struct already
has to be - see "How union works"). Separate error-path checks (not
part of the passing suite) cover a union member held by value inside a
struct, a `struct`/`union` tag used with the wrong keyword (both
directions), redefining the same tag, a by-value union parameter or
return value, an array member, a duplicate member name, and member
access on a non-aggregate value. `tests/typedef.c` covers a plain
scalar alias, a pointer alias, `typedef struct/union/enum Tag Tag;`
(the "drop the keyword afterward" idiom, for all three), a typedef
built from another typedef, and a typedef used as a global's type
(with an array size and an initializer), a function's parameter and
return type, and a struct member's type (through a pointer, respecting
struct's own by-value-member restriction). Separate error-path checks
(not part of the passing suite) cover pointer-to-pointer via a
typedef'd pointer type, redefining a typedef, a typedef colliding with
a global/function/builtin/enum-constant name, and an array typedef.

Run all twelve:

```sh
for f in hello features forward pointers recursion include structs dowhile_switch printf enum union typedef; do
    ./cc64 tests/$f.c -o tests/$f.asm
    ./c64asm tests/$f.asm -o tests/$f.prg --listing tests/$f.lst
    python3 mini6502.py tests/$f.prg tests/$f.lst
done
```

## Roadmap

See [`ROADMAP.md`](ROADMAP.md) for other language/tooling ideas not
yet scheduled and the standard library's own open items - every item
from the original "next steps" list (`do`/`while`, `switch`, a
`printf`-lite) is now done, and `enum`/`union`/`typedef` (picked up
separately from that list) are too.
