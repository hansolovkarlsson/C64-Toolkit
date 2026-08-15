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
8. ~~`union`~~ — done, also picked up separately, chosen next ahead of
   `typedef`/`unsigned` specifically because it reuses almost all of
   `struct`'s own machinery rather than needing new infrastructure:
   same `StructDef`/`StructMember` types, same member-type
   restrictions, same `.`/`->`/`&` access codegen (already generic on
   "base address plus this member's offset", so it needed zero changes
   at all), even the same parsing function
   (`parse_struct_or_union_def()`, `src/parser.c`, one function
   handling both keywords now instead of two near-duplicate ones). The
   one real difference is the layout rule: every union member starts
   at offset 0 instead of packing sequentially, and the union's size is
   its widest member's width, not their sum. `struct`/`union` tags
   share one namespace, matching real C - using the wrong keyword for
   an already-fully-defined tag is a compile-time error. Also inherits
   struct's own restrictions (pointer-only across function boundaries,
   no aggregate-by-value member) unchanged, so a "tagged union" pattern
   needs a pointer member, the same workaround self-referential structs
   already use. See `README.md`'s "How union works" for the full design
   and `tests/union.c` for the regression coverage (including
   overlapping-storage checks in both directions - a wide write read
   back narrow, and a narrow write read back wide).
9. ~~`typedef`~~ — done, the harder of the first two remaining
   candidates but tractable: resolves entirely at parse time, inside
   `parse_type_prefix()` itself, so - like `enum` - it needed zero
   codegen changes; the real work was parsing, not codegen. A typedef
   name is a plain identifier, lexically indistinguishable from a
   variable/function name, so every place this compiler decides
   "declaration or expression?" had to start checking the current
   token's TEXT against the registered-typedef table, not just its
   KIND - `cur_is_type()` (`src/parser.c`) is that combined check,
   replacing four separate `is_type_kw(cur()->kind)` call sites. A
   typedef'd pointer type can't gain an extra `*` at a use site
   (rejected as pointer-to-pointer, which isn't supported at all).
   Top-level only, same as struct/union/enum; the underlying type
   must already exist as a plain reference, not an inline anonymous
   definition combined with the typedef in one statement (this compiler
   has no anonymous-struct-definition syntax to combine with in the
   first place). See `README.md`'s "How typedef works" for the full
   design and `tests/typedef.c` for the regression coverage.
10. ~~`unsigned`~~ — done, the hardest of the three candidates this
    list started with, since it's the one that isn't a pure parse-time
    substitution: `isUnsigned` is a new flag threaded parallel to
    `isPointer` through `CType` and every declaration-carrying struct
    (`GSym`/`LSym`/`StructMember`/`TypedefEntry`/`FnSym`), and `/`, `%`,
    and `>>` genuinely need to route to a different runtime routine for
    unsigned operands (`+`/`-`/`*`/`&`/`|`/`^`/`<<`/`==`/`!=` don't -
    they're bit-pattern-invariant regardless of signedness). Unsigned
    division/modulo needed no new runtime code - `__rt_udiv16` already
    existed as the primitive `__rt_sdivmod16` itself is built on, so
    routing to it was a straight JSR-target swap; unsigned (logical,
    zero-filling) right shift needed one new routine, `__rt_ushr16`,
    alongside the existing arithmetic (sign-extending) `__rt_shr16`.
    `printf`'s `%u` and `print_uint()` (`lib/print.h`) both became
    possible for the first time. See `README.md`'s "How unsigned
    works" for the full design and `tests/unsigned.c` for the
    regression coverage.

## Other language ideas, not yet scheduled

None of these are committed to or ordered against each other - just
captured so they aren't lost. See `README.md`'s "Not supported yet"
for the authoritative current boundary; this expands on a few entries
where there's something specific worth knowing before starting on one.

### Data types

- `long`/`short` — `int` is currently always exactly 16 bits; there's
  no smaller or larger integer type.
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

- Function pointers.
- Pointer-to-pointer (`int **`).
- Arrays of pointers.
- Array *parameters* written with `[]` syntax - `type *name` already
  receives the identical decayed pointer, so this is a syntax gap, not
  a capability one.
- By-value struct/union parameters and return values (`struct Tag *`/
  `union Tag *` only, today) — would need real aggregate-copying
  machinery at every call boundary; see `README.md`'s "How structs
  work" for why that was skipped so far.
- Struct/union members that are themselves a struct/union-by-value or
  an array.
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
`memset`/`memcpy`), `lib/print.h` (`print_int`/`print_uint`/`print_hex`/
`newline` - `printf` itself is a compiler builtin now, not part of this
header, and needs no `#include` at all), `lib/graphics.h` (VIC-II
text-screen and hardware-sprite helpers - border/background color,
direct screen-RAM/color-RAM writes, all 8 sprites' position/color/
pointer/multicolor/expand/priority), and `lib/sound.h` (SID helpers,
all 3 voices - frequency/pulse width, ADSR envelope packing, gate/
silence, and a one-shot `sid_play`) — see `README.md`'s "The standard
library" for the full API.

`graphics.h`/`sound.h` are independent, ordinary `cc64` code
(`peek`/`poke`-based, same style as `lib/string.h`/`lib/print.h`)
rather than wrappers around `asm/lib/`'s `c64asm` routines — see the
root [`../ROADMAP.md`](../ROADMAP.md)'s "Recently done" for why
wrapping wasn't actually on the table (`cc64` has no inline-assembly/
foreign-function-call mechanism, and `asm/lib/`'s raw-register calling
convention doesn't match `cc64`'s own anyway).

Still wanted, not started: an expanded "BASIC-equivalent" convenience
library (typed-line input, a few common KERNAL wrappers) - see
`asm/lib/input.inc`/`text.inc` for the kind of thing this would cover,
re-derived rather than wrapped for the same reason above.
