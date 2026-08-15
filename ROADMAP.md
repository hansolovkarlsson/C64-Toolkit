# Roadmap

This file tracks direction for the toolkit as a whole — things that
span both `asm/` and `C/`, or decisions that affect how they fit
together. Each subproject keeps its own more detailed roadmap for
work scoped to just that one: [`asm/ROADMAP.md`](asm/ROADMAP.md) and
[`C/ROADMAP.md`](C/ROADMAP.md).

## Recently done

- **Merged `asm/` and `C/` into one repository** (this one), via
  `git subtree`, each keeping its full original commit history. Both
  projects were always developed together and `cc64` has always
  targeted `c64asm`'s syntax directly — this had been an open item on
  `C/`'s own todo list for a while ("bring asm and C into one big
  project, C64Tools").
- `c64asm`'s `.tag`/`.endtag` gained per-field shape checking, not just
  a total-size check (see `asm/ROADMAP.md`'s changelog pointer and
  `asm/docs/CHANGELOG.md`).
- `cc64` picked up `do`/`while`, `switch`/`case`/`default`, and a
  `printf`-lite — the entire original "next steps" list in
  `C/ROADMAP.md` is now done. `printf` in particular is worth knowing
  about toolkit-wide: it exists without `cc64` ever gaining real
  variadic-function support, by requiring a compile-time-constant
  format string and expanding each call at compile time instead - see
  `C/README.md`'s "How printf works".
- `cc64` also picked up `enum` (from `C/ROADMAP.md`'s "not yet
  scheduled" list, done ahead of everything else there) - every
  enumerator is just a compile-time `int` constant, no new runtime
  representation, usable anywhere a plain `int` constant already was
  (expressions, `switch` cases, array sizes, global initializers). See
  `C/README.md`'s "How enum works".
- `cc64` also picked up `union`, chosen next specifically because it
  reused almost all of `struct`'s own machinery (same types, same
  member-access codegen, even the same parsing function now shared
  between both keywords) rather than needing new infrastructure -
  `typedef` and `unsigned`, the other two candidates, are both
  meaningfully more invasive (a context-sensitive parsing problem and a
  new type-system dimension threaded through the whole compiler,
  respectively). See `C/README.md`'s "How union works".
- `cc64` also picked up `typedef` - resolves entirely at parse time
  (zero codegen changes, the same as `enum`/`union`), but needed a real
  fix to the parser's own "declaration or expression?" decision logic:
  a typedef name is a plain identifier, so every place that used to
  check just the current token's KIND now also checks its TEXT against
  the registered-typedef table. See `C/README.md`'s "How typedef
  works".
- `cc64` also picked up `unsigned`/`unsigned int`, the last and hardest
  of that original three-way comparison - unlike `enum`/`union`/
  `typedef`, it isn't a pure parse-time substitution: `isUnsigned` is a
  new flag threaded parallel to `isPointer` through the whole type
  system, and `/`, `%`, and `>>` genuinely route to a different runtime
  routine for unsigned operands (unsigned division reused an existing
  primitive; unsigned right shift needed one small new routine).
  `printf`'s `%u` and `lib/print.h`'s `print_uint()` both became
  possible for the first time as a result. See `C/README.md`'s "How
  unsigned works".
- **`cc64` output ran against `c64emu` (`emu/`) for the first time** —
  previously only verified against `mini6502.py`, which traps `CHROUT`
  in software rather than running real KERNAL code. `C/examples/
  graphics_demo.c` (new) is a small real program exercising `printf`
  and `poke()`-based VIC-II/screen-RAM/color-RAM access, and it
  immediately caught a real bug — not in `cc64`, but in `emu/`'s
  `--prg` injection shortcut, which had never before been used with a
  program that returns normally after `main()` instead of looping
  forever. See `emu/ROADMAP.md`'s "Running `cc64`'s output" for the
  full bisection and fix.
- **Reconciled the two separate `mini6502.py` copies.** `C/bin/mini6502.py`
  used to be its own independently-maintained ~350-line 6502 emulator,
  parallel to (and liable to disagree with) `asm/examples/mini6502.py`'s
  more complete one — verified they actually do agree, by switching
  `C/bin/mini6502.py` to a thin CLI wrapper around
  `asm/examples/mini6502.py`'s `C64Machine` and re-running all thirteen
  of `C/`'s compiler tests through it, all passing with correct output.
  Also caught and fixed a real bug this surfaced: root `CLAUDE.md`'s own
  documented `C/` test-suite command was invoking
  `asm/examples/mini6502.py` directly, which has no CLI entry point at
  all — running it produced no error and no output, a silent no-op that
  looked like a passing test.

- **Decided: `cc64`'s standard library stays independent of `asm/lib/`** — it
  will keep re-deriving hardware access (joystick/keyboard reading,
  graphics/sound primitives) as ordinary `cc64` code in `C/bin/lib/`,
  the same style `string.h`/`print.h` already use, rather than wrapping
  `asm/lib/`'s existing `.inc` routines. Decided because wrapping isn't
  actually available as a choice today, not just less convenient:
  `cc64` has no inline-assembly/foreign-function-call mechanism to JSR
  into arbitrary external assembly at all (still on `C/ROADMAP.md`'s
  unscheduled ideas list), so calling an `asm/lib/` routine would first
  need that whole new compiler feature built. Even with it, `asm/lib/`'s
  routines use a raw-register calling convention and frequently require
  the *caller* to pre-define specific zero-page pointers before
  `.include`ing (see e.g. `input.inc`'s `word_dest_ptr` requirement) —
  a completely different contract from `cc64`'s own per-function
  frame-save/restore convention (`README.md`'s "How recursion works"),
  so "wrapping" would mean a hand-written shim per routine, not a
  generic bridge. The actual hardware facts worth reusing (e.g.
  "joystick port 2 is active-low, shares CIA1 PRA's pins with keyboard
  column-select") are a handful of `peek`/`poke` lines each to
  re-express directly in `cc64`, not a large amount of logic worth the
  cost of building a whole FFI mechanism to avoid re-deriving. See
  `C/ROADMAP.md`'s "Standard library" section.

## Ideas without an owner yet

Pulled from `asm/`'s own "beyond the assembler" notes — these aren't
committed to, just captured so they aren't lost:

- A from-scratch 6502 CPU emulator as its own project (possibly with
  graphics/sound, possibly in Swift) — distinct from `mini6502.py`,
  which is purpose-built as a test harness, not a general emulator.
- A BASIC compiler and/or interpreter for the C64.
- A code editor with C64-aware syntax highlighting and predictive
  lookup, and/or a Mac-native sprite editor — both would be new,
  separate (likely Swift) projects rather than something inside this
  toolkit.

See `asm/ROADMAP.md` and `C/ROADMAP.md` for what's actually planned
for each subproject.
