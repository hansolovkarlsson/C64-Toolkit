# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Repository layout

This is not a single project — it's two independent, self-contained C64
toolchain projects plus reference material:

- **`asm/`** — `c64asm`, a two-pass 6502/6510 assembler for the C64, in
  three interchangeable, byte-identical-output implementations (Python,
  single-file C99, and a heavily-commented multi-file C99 split), plus a
  standard library, a from-scratch 6502 emulator used as the test harness,
  a disassembler, and ~15 demo games/programs written in its own asm
  syntax.
- **`C/`** — `cc64`, a small C-to-6502 compiler that targets `c64asm`'s
  exact syntax as its output. Depends on `asm/` at build/run time (see
  below).
- **`Computes Gazette/`, `resources/`** — reference material (old
  magazines, books, other people's C64 projects) — not code belonging to
  this repo's own build.

`cc64` (`C/`) and `c64asm` (`asm/`) are developed together but built
separately; `cc64` only ever *emits* `.asm` text, it doesn't link against
`c64asm`. A full C -> `.prg` pipeline needs both binaries built, `cc64`
first.

## Commands

### `asm/` — the assembler

```sh
cd asm

# Build the split-source (multi-file) C implementation
make                       # -> bin/c64asm
make single                # -> single_src/c64asm (single-file C)
make clean                 # remove build artifacts

# Assemble one file directly, any of the three implementations:
python3 single_src/c64asm.py input.asm -o output.prg [--listing out.lst] [--lib-dir lib]
./single_src/c64asm input.asm -o output.prg
./bin/c64asm input.asm -o output.prg

# Build every example demo to .prg (also builds bin/c64asm first)
make examples

# Run every example .prg through the mini6502 test harness
make test

# Disassemble a .prg back to c64asm-compatible source
python3 single_src/c64disasm.py file.prg -o file.asm [--entry $ADDR]
```

**Split-source C build** (`asm/src/`, one `.c`/`.h` per compiler concern —
see `asm/src/ARCHITECTURE.md`) uses its own copy of the same `Makefile`
targets (`make`, `make clean`) from `asm/` itself; `SOURCES := $(wildcard
src/*.c)` picks up every module automatically.

**Per-demo regression tests** live in `asm/examples/test_*.py` (one per
demo — `test_adventure.py`, `test_pong.py`, etc.), built on `mini6502.py`
(the from-scratch CPU+C64Machine emulator, also in `examples/`). Run a
single one directly:

```sh
cd asm/examples
python3 test_pong.py
```

These play the actual game/demo logic through programmatically (full
win-condition paths, simulated keyboard/joystick input) rather than just
checking that assembly succeeded — this is the primary correctness net
for the standard library and every demo. A demo's `.prg` must be built
(`make examples` from `asm/`, or the direct assemble command above)
before its test can run against it.

**Three-way parity** is a hard invariant for this project: Python,
single-file C, and split-source C must produce byte-identical `.prg` and
`--listing` output for the same input. When changing assembler behavior,
verify a change in all three, not just one.

### `C/` — the compiler

```sh
cd C
make                                    # -> bin/cc64
make clean

./bin/cc64 program.c -o program.asm     # compile C -> asm
../asm/bin/c64asm program.asm -o program.prg   # then assemble (from asm/)

# or, chained in one step:
./build.sh tests/hello.c hello.prg      # note: build.sh prefixes the arg with tests/
```

Run the full test suite (each `.c` file under `tests/`, compiled, assembled,
and executed against `mini6502.py`):

```sh
cd C
for f in hello features forward pointers recursion include structs; do
    ./bin/cc64 tests/$f.c -o tests/$f.asm
    ../asm/bin/c64asm tests/$f.asm -o tests/$f.prg --listing tests/$f.lst
    python3 ../asm/examples/mini6502.py tests/$f.prg tests/$f.lst
done
```

Each `tests/*.c` file targets one compiler area — `features.c` (arithmetic/
bitwise/comparison/control-flow), `pointers.c`, `recursion.c` (including an
80-deep recursion stress case), `include.c` (`#include` + stdlib), `structs.c`,
`forward.c` (declaration-order independence).

## Architecture

### `c64asm` (`asm/`)

Two-pass assembler: pass 1 resolves label addresses, pass 2 emits real
bytes, which is what lets a label be referenced before its own definition
line. Read `asm/src/assembler.h`'s header comment (or `c64asm-reference.md`
§23 for the same idea from the user side) before touching pass logic.

Pipeline, in the order data actually flows through the split-source
implementation (`asm/src/`, see `ARCHITECTURE.md` for the full guided
tour) — the same conceptual stages exist in the Python and single-file C
versions too, just not as separate files:

1. **`fileio` / `includes`** — load the top-level file; `.include` resolves
   relative to the *including* file's own directory (not cwd), is
   automatically include-once, and falls back to `--lib-dir` only when the
   default lookup misses.
2. **`macro`** — expands `.macro`/`.endmacro` and splices `.include`d
   files, recursively, before any real parsing happens. Every raw line
   passes through here first.
3. **`locallabels`** — rewrites `@name` to a scope-specific global name
   (`__local5_loop`) purely as text rewriting, before the parser ever sees
   it. A new scope opens on every global label *and* every macro
   expansion, which is what keeps a macro's internal labels collision-free
   across repeated invocations.
4. **`lineparser`** — splits one line into (label, mnemonic-or-directive,
   operand).
5. **`expr`** — hand-written recursive-descent expression evaluator
   (`parse_expr` -> `parse_term` -> `parse_unary` -> `parse_atom`, one
   precedence level per function).
6. **`operand`** — determines addressing mode from operand punctuation
   (`#`, parens, trailing `,X`/`,Y`).
7. **`symtab`** — label/constant storage; a linear scan is deliberate at
   this scale (see comments in `symtab.c`). Also home to
   `find_symbol_defined_before()`, which is what makes `.ifdef` give a
   *pass-consistent* answer even though the symbol table itself persists
   across both passes.
8. **`assembler`** — the two-pass driver tying everything above together.

Directives worth knowing the shape of before extending them: `.macro`,
`.repeat`/`.dup`, `.struct`/`.endstruct` (named byte offsets into a data
table, `Room.north` instead of a bare number), `.tag`/`.endtag` (binds a
data block to a `.struct`, checks at `.endtag` that the block's total size
matches — see `c64asm-reference.md` §12 for exactly what it does and does
not check), `.assert`, `.if`/`.elif`/`.else`/`.endif` and `.ifdef`/
`.ifndef`, `.error`/`.warning`, `.incbin`. `c64asm-reference.md` is the
authoritative syntax spec for all of these — check it before assuming a
directive's behavior from the code alone.

**Style conventions carried through every module** (see `asm/src/ARCHITECTURE.md`
"A note on style" for the full rationale): fixed-size buffers everywhere
except the assembled output itself (which uses a real growable buffer,
`bytebuf.c`); a handful of deliberate module-level globals (`g_lines`,
`symtab`, `g_listing`) rather than threading state through every call,
since exactly one of each exists per run; `expr.c`'s `EParser` is the one
exception, bundled into a struct because expression evaluation recurses
and must be reentrant.

**Docs** (`asm/docs/`): `c64asm-reference.md` (syntax reference — the
source of truth for directive behavior), `c64asm-opcode-reference.md`
(6502 opcode/addressing-mode reference), `c64-memory-reference.md` (C64
hardware: VIC-II, SID, KERNAL routines), `lib-reference.md` (standard
library API, inside `c64asm-stdlib.zip`), `CHANGELOG.md` (every notable
feature/fix, newest first — check this before assuming something is
unimplemented), `GETTING-STARTED.md`.

### `cc64` (`C/`)

Also two-pass, for the same forward-reference reason (`pass_a`/`pass_b`
in `src/parser.c` scan all top-level declarations before generating any
code, so call order in the source file doesn't matter). See `src/cc64.h`'s
header comment for the full phase map: `lexer.c` (tokens + `#include`
splicing) -> `ast.c` -> `symtab.c` (symbol tables, struct layout, minimal
type inference) -> `parser.c` -> `codegen.c` / `codegen_runtime.c` /
`codegen_expr.c` / `codegen_stmt.c`.

Load-bearing design choices, not oversights — check these before "fixing"
something that looks like a limitation:

- **No call stack; recursion via per-function frame save/restore.**
  Every variable has fixed static storage; a call wraps `JSR` with a
  `pushframe`/`popframe` that copies the callee's whole parameter+local
  block to/from a software call stack. A function's frame is capped at
  256 bytes (the copy loop counts with the 6502's 8-bit Y register) —
  enforced as a compile-time error, not a silent truncation. See the
  comment above `emit_function()` in `src/codegen_stmt.c`.
- **Structs are pointer-only across function boundaries** (`struct Tag *`,
  never by-value) — deliberate scope limit, not missing functionality.
- **`char` is unsigned** (no sign-extension logic needed).
- **PETSCII conversion happens at runtime**, in `putchar`/`puts` only —
  plain `char` variables are never auto-converted. `cc64` emits the
  charset-switch control code once, automatically, before `main` runs.
- **Zero page usage is deliberately minimal and specific**: only `$02`,
  `$FB`-`$FE`. An earlier version also used `$F3`-`$FA` on the assumption
  they were safe scratch space; they aren't (`$F3`/`$F4` is KERNAL's
  current-line-color-RAM pointer, `$F5`/`$F6` is the keyboard-matrix
  conversion table, both touched on every CHROUT / background IRQ) — this
  caused a real memory-corruption bug on hardware. Don't claim a
  zero-page address is unused without checking `C/README.md`'s "Zero-page
  usage" section first.

### Cross-project test harness: `mini6502.py`

`asm/examples/mini6502.py` is a from-scratch 6502 CPU + C64Machine
emulator (CIA keyboard/joystick emulation, `CHROUT`/`CHRIN` trapping,
zero-page KERNAL-poisoning simulation) purpose-built to test-drive this
project's own output — not a general VICE replacement. Both `asm/`'s demo
tests and `C/`'s compiler tests run against it. It is not a substitute for
real-hardware/VICE testing — several real bugs in this project's history
were only caught on actual hardware and then back-ported into the
emulator as a new simulated failure mode (e.g. the zero-page KERNAL
poisoning above). If you fix a bug that a real device caught but
`mini6502.py` didn't, consider whether the emulator should be taught to
catch it too.

### VICE (manual/real-hardware verification)

`asm/Makefile`'s `disk`/`examples` targets shell out to a local VICE
install (`c1541`, hardcoded path
`/Applications/vice-arm64-gtk3-3.10/bin/c1541`) to build a `.d64` disk
image of every example — used for testing beyond what `mini6502.py`
simulates (real disk I/O timing, raster behavior, etc.). This path is
machine-specific; expect to adjust `VICE_C1541` in `asm/Makefile` on a
different setup.
