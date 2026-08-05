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
6. Real `printf`-lite — blocked on variadic function support, which is
   a real calling-convention feature (not just a library function),
   unlike the rest of the standard library above.

## Other language ideas, not yet scheduled

- `enum`
- An inline-assembly directive, for dropping raw `c64asm` instructions
  into a `cc64` program directly — useful for the rare hardware access
  pattern (a specific cycle-timed loop, say) that doesn't fit cleanly
  into C.
- More data types beyond the current `int`/`char`/pointer/`struct` set.
- Expanded preprocessor directives beyond the current `#include`-only
  support (see `README.md`'s "Not supported yet" for the full current
  boundary — no `#define`, no conditional compilation).

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
`newline`) — see `README.md`'s "The standard library" for the full API
and why `print_uint` is deliberately not included.

Wanted, not started: an expanded "BASIC-equivalent" convenience
library, a graphics library, a sound library. See the root
[`../ROADMAP.md`](../ROADMAP.md)'s "Open cross-project questions" for
the undecided question of whether these should wrap `asm/lib/`'s
already-tested `c64asm` routines or be built independently for `cc64`.
