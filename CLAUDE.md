# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Repository layout

This repo bundles three C64 toolchain projects. `asm/` and `C/` are
independent, self-contained, and each merged in via `git subtree` (so
`git log`/`blame` on a file still reaches that project's pre-merge
history — see `git log <path> --follow` limitations if a plain `git
log` on a path looks truncated; `git blame` always works). `emu/` is
newer and was scaffolded directly in this repo (no subtree history).

- **`asm/`** — `c64asm`, a two-pass 6502/6510 assembler for the C64, in
  two interchangeable, byte-identical-output implementations (Python,
  and a heavily-commented multi-file C99 split), plus a standard
  library, a from-scratch 6502 emulator used as the test harness, a
  disassembler, and ~15 demo games/programs written in its own asm
  syntax.
- **`C/`** — `cc64`, a small C-to-6502 compiler that targets `c64asm`'s
  exact syntax as its output. Depends on `asm/` at build/run time (see
  below).
- **`emu/`** — `c64emu`, a from-scratch, general-purpose C64 emulator
  (cycle-stepped 6502/6510 CPU, memory/bank-switching, a minimal GTK4
  shell, eventually CIA/VIC-II/SID). Does not share code with `asm/`'s
  `mini6502.py` test harness — see "`c64emu` (`emu/`)" below for why
  they're deliberately separate. Early-stage: no real graphics,
  keyboard input, or sound yet — the GTK shell drives the CPU and
  shows raw screen-RAM bytes, not real VIC-II output.

`cc64` (`C/`) and `c64asm` (`asm/`) are developed together but built
separately; `cc64` only ever *emits* `.asm` text, it doesn't link against
`c64asm`. A full C -> `.prg` pipeline needs both binaries built, `cc64`
first. `emu/` is unrelated to that pipeline — it's a consumer of the
`.prg` files the other two produce, not a dependency of either.

**Roadmaps**: [`ROADMAP.md`](ROADMAP.md) tracks cross-project direction
(open questions spanning `asm/`/`C/`, ideas without an owner yet);
[`asm/ROADMAP.md`](asm/ROADMAP.md), [`C/ROADMAP.md`](C/ROADMAP.md), and
[`emu/ROADMAP.md`](emu/ROADMAP.md) track each subproject's own open
work. Check the relevant one before assuming something is unplanned,
already decided, or still missing — e.g. `C/ROADMAP.md` has the real
next-language-features order for `cc64`, the root `ROADMAP.md` has the
still-undecided question of whether `cc64`'s future standard library
should wrap `asm/lib/` or stay independent, and `emu/ROADMAP.md` has
`c64emu`'s staged build order (CPU -> memory -> GTK shell -> CIA ->
VIC-II -> SID).

## Commands

### `asm/` — the assembler

```sh
cd asm

# Build the split-source (multi-file) C implementation
make                       # -> bin/c64asm
make clean                 # remove build artifacts

# Assemble one file directly, either implementation:
python3 single_src/c64asm.py input.asm -o output.prg [--listing out.lst] [--lib-dir lib]
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

**Two-way parity** is a hard invariant for this project: Python and
split-source C must produce byte-identical `.prg` and `--listing`
output for the same input. When changing assembler behavior, verify a
change in both, not just one. (A third, single-file C implementation
used to exist and made this a three-way check; it was retired as
redundant with split-source C — see `asm/docs/CHANGELOG.md`'s "Retired
the single-file C build".)

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

### `emu/` — the emulator

```sh
cd emu
make                       # -> bin/c64emu (needs GTK4 dev libs, e.g. `brew install gtk4`)
make clean

./bin/c64emu [rom-dir]     # rom-dir defaults to "roms"; runs fine with 0/3 ROMs loaded
```

Each core module also has its own standalone correctness gate,
independent of the GTK build, run directly rather than through the
top-level Makefile:

```sh
# CPU core: Klaus Dormann's 6502 functional test suite + a hand-written
# interrupt/reset check
cd emu/tests/cpu
make fetch      # downloads 6502_functional_test.bin (not vendored, GPL-3.0)
make run-all    # builds + runs both test_cpu and test_interrupts

# Memory map / bank switching: hand-written checks against the full
# bank-switching mode table
cd emu/tests/memory
make run
```

Both gates must pass before building on top of either module — see
`emu/tests/cpu/README.md` and `emu/tests/memory/README.md` for what
"pass" looks like and how to re-derive the CPU suite's success address
if a future revision of it moves.

## Architecture

### `c64asm` (`asm/`)

Two-pass assembler: pass 1 resolves label addresses, pass 2 emits real
bytes, which is what lets a label be referenced before its own definition
line. Read `asm/src/assembler.h`'s header comment (or `c64asm-reference.md`
§23 for the same idea from the user side) before touching pass logic.

Pipeline, in the order data actually flows through the split-source
implementation (`asm/src/`, see `ARCHITECTURE.md` for the full guided
tour) — the same conceptual stages exist in the Python version too,
just not as separate files:

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

### `c64emu` (`emu/`)

A real, general-purpose C64 emulator meant to run actual games/demos
with a GUI — a different goal from `mini6502.py` below, and the two
share no code. Staged build order (see `emu/ROADMAP.md`): CPU core ->
memory/bank-switching -> minimal GTK4 shell (all three done) -> CIA
1/2 -> VIC-II -> SID. PAL timing only; cartridge and 1541 disk-drive
emulation are explicitly out of scope for now.

- **CPU core** (`src/cpu.c`/`src/cpu.h`): the full legal 6502/6510
  instruction set, addressing modes, and per-instruction cycle counts
  (including conditional +1 for page-crossing and branch-taken cases),
  talking to memory only through a `CpuBus` read/write vtable — it has
  no idea whether it's driving a flat test-harness RAM array or the
  real bank-switched map, which is what let it be verified in
  isolation before `memory.c` existed. Decimal-mode ADC/SBC follow the
  documented NMOS quirk where N/V/Z are derived from different
  (partially uncorrected) intermediate values than the accumulator
  result itself — see `src/cpu.c`'s header comment and
  `op_adc()`/`op_sbc()`. IRQ is level-triggered (`cpu->irq_line`), NMI
  is edge-triggered (`cpu_nmi()`); both are only sampled at instruction
  boundaries, matching real hardware. Verified against Klaus Dormann's
  6502 functional test suite plus a hand-written interrupt/reset check
  (`tests/cpu/test_interrupts.c`) — Dormann's suite deliberately never
  triggers a real interrupt mid-test, so it can't exercise that path.
- **Memory map / bank switching** (`src/memory.c`/`src/memory.h`): the
  64K RAM array, ROM loading, and the 6510 I/O port at `$00`/`$01`
  (LORAM/HIRAM/CHAREN) deciding what's visible at
  `$A000`-`$BFFF`/`$D000`-`$DFFF`/`$E000`-`$FFFF`. Two non-obvious
  details worth knowing before touching this: BASIC ROM needs **both**
  LORAM and HIRAM set, not LORAM alone; and a write always lands in RAM
  even when ROM is what's visible for reads at that same address
  ("write under ROM") — `memory_write()` doesn't special-case the ROM
  regions at all because of this, only `$D000`-`$DFFF` when I/O
  (rather than RAM or character ROM) is currently banked in there.
  VIC-II/SID/CIA don't exist yet, so `$D000`-`$DFFF`'s I/O mode is
  currently an inert placeholder (`IoBus`, unregistered — reads return
  `$FF`, writes are dropped).
- **ROM images** (`roms/`) are not checked in — Commodore's copyrighted
  binaries. See `emu/roms/README.md`.
- **GTK4 shell** (`gtk/main.c`): drives the CPU via a `g_timeout_add`
  loop at ~50 Hz (PAL frame rate; `CYCLES_PER_FRAME` cycles of
  `cpu_step()` per tick, not cycle-exact — there's no raster to
  synchronize against until VIC-II exists). Renders screen RAM
  (`$0400`-`$07E7`) as a raw 40x25 grid of grayscale cells, one byte
  value per cell — deliberately not real VIC-II text-mode decoding,
  just proof that the core can be driven and displayed from a real GUI
  event loop. Runs fine with 0/3 ROMs loaded (the CPU just executes a
  harmless BRK loop on zeroed memory). Keyboard events are captured
  via `GtkEventControllerKey` and logged to stdout only — not wired to
  the C64 keyboard matrix, since CIA (which owns that) doesn't exist
  yet. Built with `-std=c11`, not this toolkit's usual `-std=c99` —
  GTK4's own headers rely on C11 typedef-redefinition tolerance
  (`G_DECLARE_*_TYPE` macros); `src/cpu.c`/`src/memory.c` stay plain
  C99-compatible regardless of which standard compiles them.

### Cross-project test harness: `mini6502.py`

`asm/examples/mini6502.py` is a from-scratch 6502 CPU + C64Machine
emulator (CIA keyboard/joystick emulation, `CHROUT`/`CHRIN` trapping,
zero-page KERNAL-poisoning simulation) purpose-built to test-drive this
project's own output — not a general VICE replacement, and not the
same thing as `emu/`'s `c64emu` above. Both `asm/`'s demo tests and
`C/`'s compiler tests run against it. It is not a substitute for
real-hardware/VICE testing — several real bugs in this project's history
were only caught on actual hardware and then back-ported into the
emulator as a new simulated failure mode (e.g. the zero-page KERNAL
poisoning above). If you fix a bug that a real device caught but
`mini6502.py` didn't, consider whether the emulator should be taught to
catch it too.

Note: a second, much smaller and apparently-stale copy of this file
also exists at `C/bin/mini6502.py` (see root `ROADMAP.md`'s "Two
separate `mini6502.py` copies" for why this hasn't been reconciled
yet) — check which one a command is actually resolving to before
trusting its output.

### VICE (manual/real-hardware verification)

`asm/Makefile`'s `disk`/`examples` targets shell out to a local VICE
install (`c1541`, hardcoded path
`/Applications/vice-arm64-gtk3-3.10/bin/c1541`) to build a `.d64` disk
image of every example — used for testing beyond what `mini6502.py`
simulates (real disk I/O timing, raster behavior, etc.). This path is
machine-specific; expect to adjust `VICE_C1541` in `asm/Makefile` on a
different setup.
