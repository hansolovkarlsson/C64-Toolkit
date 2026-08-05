# `cc64` roadmap

## Next steps, in a sensible order

1. ~~Pointers and `&`/`*`~~ — done. Unlocked passing arrays to
   functions, `char*` strings as real runtime values, pointer
   arithmetic, and was the prerequisite for...
2. ~~Function recursion~~ — done, via per-function frame save/restore
   around every call (see `README.md`'s "How recursion works") rather
   than a full stack-frame rewrite, so the fixed-address storage model
   and all its codegen simplicity survived intact.
3. ~~`#include` and a small standard library~~ — done. Handled entirely
   in the lexer (see "HOW #include WORKS" in `src/cc64.h`), which
   meant zero changes to parsing or codegen; `lib/string.h` and
   `lib/print.h` are ordinary `cc64` programs that happen to be
   headers.
4. ~~`struct`~~ — done, pointer-only across function boundaries (see
   `README.md`'s "How structs work") rather than also supporting
   by-value passing, which would need real struct-copying machinery at
   every call boundary for comparatively little added benefit given
   pointers already cover the common linked-structure use cases.
5. ~~`do`/`while`, `switch`~~ — done. `switch` compiles to a
   straight-line compare-and-branch chain (no jump table), evaluating
   its expression once and holding it in the primary register
   untouched through the whole chain — which is why case labels must
   be constant literals, the same restriction a global initializer
   already has. `break`/`continue` both had to learn the loop-vs-switch
   distinction (`break` exits the nearest enclosing loop *or* switch;
   `continue` always means the nearest enclosing *loop*, skipping past
   any switch in between) — see `README.md`'s "How switch works" for
   the full design, and `tests/dowhile_switch.c` for the regression
   coverage, including a switch nested inside a loop and inside another
   switch.
6. ~~Real `printf`-lite~~ — done, without ever building real variadic-
   function machinery: `printf`'s format string must be a compile-time
   string literal, so the compiler parses it once, at compile time, and
   expands each call into the same fixed sequence of `puts`/`putchar`-
   equivalent calls a programmer chaining `print_int`/`print_hex` by
   hand would have written anyway. `%d`/`%x`/`%c`/`%s`/`%%` are
   supported; `%u` deliberately isn't, for the same reason `print_uint`
   was never added to `lib/print.h` (see that header's own comment).
   See `README.md`'s "How printf works" for the full design and
   `tests/printf.c` for the regression coverage.
7. ~~`enum`~~ — done, picked up separately since this list is done
   otherwise. `enum [Tag] { NAME [= value], ... }` never creates a real
   type: every enumerator is a compile-time `int` constant, and
   `enum Tag` used as a type is nothing but an alias for `int` - no
   runtime representation, no way to tell at runtime a value "came
   from" an enum at all, matching real C. That's what lets an
   enumerator be used absolutely anywhere a plain `int` constant is
   valid: ordinary expressions, `switch` case labels, array sizes,
   global initializers - all four now accept either a literal or a
   named enum constant via one shared `parse_const_value()` helper
   (`src/parser.c`). The tag is optional (unlike `struct`'s required
   one) but, when given, can never be forward-referenced the way a
   struct tag can - real C has no incomplete-enum-declaration concept,
   so `enum Tag` used as a type requires that tag to already be fully
   defined. See `README.md`'s "How enum works" for the full design and
   `tests/enum.c` for the regression coverage.

## Other language ideas, not yet scheduled

None of these are committed to or ordered against each other - just
captured so they aren't lost. See `README.md`'s "Not supported yet"
for the authoritative current boundary; this expands on a few entries
where there's something specific worth knowing before starting on one.

### Data types

- `union`
- `long`/`short` — `int` is currently always exactly 16 bits; there's
  no smaller or larger integer type.
- `unsigned` — partially closer than it looks: the unsigned *comparison*
  routines already exist in the runtime (`__rt_ult16`/`__rt_ugt16`/
  `__rt_ule16`/`__rt_uge16`, used today only for pointer comparisons,
  which are inherently unsigned - see `gen_binop()`'s `unsignedCmp` in
  `codegen_expr.c`), and unsigned division (`__rt_udiv16`) already
  exists too, as the primitive `__rt_sdivmod16` itself is built on. An
  actual `unsigned int` type would mainly need `/`/`%`/`>>` to route to
  the unsigned runtime routines instead of hardcoding the signed ones
  the way they do now for every `int`, plus wiring up
  `infer_type()`/`gen_binop()` to know a value is unsigned in the first
  place. `print.h`'s missing `print_uint()` (see that header's own
  comment) and `printf`'s missing `%u` would both become fixable once
  this exists.
- Floating point (`float`/`double`) — no hardware float on the 6502;
  this would need a real software float implementation (mantissa/
  exponent representation, add/sub/mul/div/compare routines) from
  scratch, a substantially bigger undertaking than anything else on
  this list.
- `_Bool`/`bool` — not present, though `int`/`char` already serve as
  truth values everywhere (`if`/`while`/`&&`/`||` all just test "zero
  or not"), so this would mostly be a naming/spelling convenience
  (`stdbool.h`-style) rather than new capability.

### Type system / declarations

- `typedef` — every `struct` variable/parameter currently needs the
  full `struct Tag` spelled out, no bare `Tag`.
- Function pointers.
- Pointer-to-pointer (`int **`).
- Arrays of pointers.
- Array *parameters* written with `[]` syntax - `type *name` already
  receives the identical decayed pointer, so this is a syntax gap, not
  a capability one.
- By-value struct parameters and return values (`struct Tag *` only,
  today) — would need real struct-copying machinery at every call
  boundary; see `README.md`'s "How structs work" for why that was
  skipped so far.
- Struct members that are themselves a struct-by-value or an array.
- Multi-dimensional arrays.
- Real variadic user functions (a `...` parameter) — `printf`'s own
  compile-time-constant format-string restriction is what let it ship
  without this; see `README.md`'s "How printf works".

### Preprocessor

- `#define`, `#ifdef`/`#ifndef`, and conditional compilation generally
  — `#include` is the only directive supported today (see `src/cc64.h`'s
  "HOW #include WORKS").

### Other

- An inline-assembly directive, for dropping raw `c64asm` instructions
  into a `cc64` program directly — useful for the rare hardware access
  pattern (a specific cycle-timed loop, say) that doesn't fit cleanly
  into C.

## Tooling ideas

- A performance-testing tool — some way to measure/compare generated
  code's actual cycle cost, beyond reading a `c64asm --listing`'s
  cycle-count annotations by hand.
- General codegen optimization — `codegen_expr.c`/`codegen_stmt.c`
  generate code directly from the AST with no separate optimization
  pass; a measured, deliberate optimization step is a reasonable
  future direction once there's a performance-testing tool (above) to
  measure it against.

## Standard library

Currently: `lib/string.h` (`strlen`/`strcpy`/`strcat`/`strcmp`/`strchr`/
`memset`/`memcpy`) and `lib/print.h` (`print_int`/`print_hex`/
`newline` - `printf` itself is a compiler builtin now, not part of this
header, and needs no `#include` at all) — see `README.md`'s "The
standard library" for the full API and why `print_uint` is
deliberately not included.

Wanted, not started: an expanded "BASIC-equivalent" convenience
library, a graphics library, a sound library. See the root
[`../ROADMAP.md`](../ROADMAP.md)'s "Open cross-project questions" for
the undecided question of whether these should wrap `asm/lib/`'s
already-tested `c64asm` routines or be built independently for `cc64`.
