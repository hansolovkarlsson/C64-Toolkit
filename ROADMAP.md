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
- **Shared testing infrastructure.** `mini6502.py` (in `asm/examples/`)
  already tests both `c64asm` output directly and `cc64`-generated
  `.asm`/`.prg` files (`C/`'s own test loop calls it). It's a genuine
  shared piece of infrastructure living inside one subproject's
  directory rather than somewhere neutral — fine for now, but worth
  reconsidering if a third consumer ever shows up.

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
