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
