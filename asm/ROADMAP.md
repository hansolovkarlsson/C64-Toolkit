# `c64asm` roadmap

Ideas and open work for the assembler, standard library, and demo
programs. See [`docs/CHANGELOG.md`](docs/CHANGELOG.md) for what's
already shipped (newest first) — check there before assuming
something below isn't done yet.

## Assembler & docs

- **A dedicated assembler test suite.** The existing test coverage
  (`examples/test_*.py`) is demo-level: it plays each shipped game/demo
  through `mini6502.py` end to end, which is a strong regression net
  for the standard library and the demos themselves, but isn't a
  focused, directive-by-directive unit test suite for the assembler's
  own syntax (every addressing mode, every directive's edge cases and
  error messages, in isolation). Worth having both.
- **Keep `c64asm-memory-reference.md`/`c64asm-reference.md` current**
  with any new issue or edge case discovered while building future
  examples — this project's own established practice (see the
  reference docs' own worked examples, each backed by something that
  actually broke once).
- **Compare against Kick Assembler** — what syntax or features does it
  have that `c64asm` doesn't? Not a goal of feature-parity for its own
  sake, but worth knowing what's genuinely missing versus what's a
  deliberate scope difference (see `c64asm-reference.md`'s "Known
  limitations" for what's already a documented, deliberate one).

## Standard library (`lib/`)

Already covers: bitmap/sprite graphics (`graphics.inc`), hardware
register constants (`hardware.inc`), joystick + keyboard-matrix +
typed-line input (`input.inc`, `keyboard.inc`), small integer multiply/
divide by a constant (`math.inc`), two-voice SID music (`music.inc`),
SID sound effects (`sound.inc`), and PETSCII text output + basic string
comparison (`text.inc`).

Still open, from the original library wishlist:
- **Floating point** — nothing exists yet; the 6502 has no hardware
  support, so this would be a real software-float implementation.
- **A fuller string library** — `text.inc` has `str_equal`; things like
  concatenation, length, substring search/copy don't exist yet as
  reusable routines (each demo that's needed something like this has
  hand-rolled it).
- **File I/O as a reusable library**, not just per-program KERNAL
  calls — `editor.asm` and `maze.asm`/`dir_demo.asm` each hand-roll
  their own `SETLFS`/`SETNAM`/`OPEN`/`CHKIN`/... sequences directly
  rather than going through a shared `lib/` file. Worth extracting the
  common pattern once a third program needs it.

## Demo games

Shipped: `hello`, `demo`, `sprites`, `music_demo`, `bounce`, `pong`,
`adventure`, `lander`, `editor` (with file load/save + directory
listing), `maze` (in progress — see its own entry in `README.md`'s
project structure table for current stage).

Ideas not started yet: Minesweeper, Othello, Tic-tac-toe, Snake,
Breakout, Dig Dug, Boulder Dash, Centipede, Pac-Man, a Zelda/Links-style
game, a Donkey Kong-style platformer, a match-3 puzzle game (Royal
Match-style), Space Invaders.

## Beyond the assembler

Ideas that would be separate projects, not part of `c64asm` itself —
kept here so they aren't lost, not committed to:

- A from-scratch 6502 CPU emulator (possibly with graphics/sound,
  possibly in Swift) — see the root [`ROADMAP.md`](../ROADMAP.md).
- A C compiler targeting `c64asm` — **done**, see [`../C/`](../C/README.md).
- A pre-compiler/CPP-style macro preprocessor.
- A Mac-native sprite editor, code editor with C64-aware syntax
  highlighting/predictive lookup, a BASIC compiler, a BASIC
  interpreter — see the root `ROADMAP.md`'s "Ideas without an owner
  yet".
