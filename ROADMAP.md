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

## Open cross-project questions

- **Where should a C64 standard library live?** Both subprojects have
  their own separate list of library ideas — `asm/`'s own `lib/`
  already has graphics/sound/input/keyboard/math/music/text/hardware
  `.inc` files for `c64asm` programs, while `C/`'s own todo notes list
  "expanded basic library", "graphics library", and "sound library" as
  wanted for `cc64` programs, independently. Worth deciding whether
  `cc64`'s standard library should wrap/reuse `asm/lib/`'s
  already-tested routines (less duplicated logic, but couples `cc64`'s
  library to `c64asm`-specific macro conventions) or stay independent
  (simpler dependency story, but re-deriving already-solved problems
  like joystick/keyboard reading a second time). Not yet decided
  either way.
- **Two separate `mini6502.py` copies exist** — `asm/examples/mini6502.py`
  (actively developed: CIA keyboard/joystick emulation, zero-page
  KERNAL poisoning, ~1300 lines) and `C/bin/mini6502.py` (a much
  smaller, ~350-line copy, apparently untouched since `cc64`'s very
  first two commits). `C/README.md`'s documented test loop invokes a
  bare `mini6502.py`, which resolves to whichever copy sits next to it
  depending on where the loop is actually run from — the two aren't
  guaranteed to agree on what they can execute. Worth deciding whether
  `C/`'s copy should be replaced with (or symlinked to) `asm/`'s more
  complete one, or whether the two genuinely need to diverge.

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
