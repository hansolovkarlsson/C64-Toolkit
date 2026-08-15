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
  (cycle-stepped 6502/6510 CPU, memory/bank-switching, both CIAs
  including real keyboard/joystick wiring, VIC-II [40x25 hi-res,
  multicolor, AND extended-background-color text mode, standard AND
  multicolor bitmap mode, all 8 hardware sprites, border/background
  color, raster IRQs, bad lines], SID [3-voice synth: all 4 waveforms,
  ADSR envelopes, hard sync, ring modulation, played back for real
  through SDL2 - no analog filter modeling yet], a GTK4 shell). Does
  not share code with `asm/`'s `mini6502.py` test harness — see
  "`c64emu` (`emu/`)" below for why they're deliberately separate.
  **Boots real system software**: tested against the MEGA65 `open-roms`
  open-source ROM replacement, it runs unmodified to a readable BASIC
  `READY.` prompt. Has sound now. `open-roms` itself is a from-scratch,
  still-in-development BASIC/KERNAL reimplementation, not a guaranteed
  match for every real Commodore BASIC 2.0 command — confirmed missing:
  `CHR$()` (`?NOT IMPLEMENTED ERROR`, not a syntax error — the token is
  recognized, the routine behind it isn't). See `emu/tests/boot/README.md`'s
  "Known limitations" for how this was verified (typed through the real
  keyboard matrix, not a shortcut) and `emu/roms/README.md` for why this
  project can't just switch to Commodore's own real ROMs to sidestep it.
  Also separately confirmed booting real Commodore ROMs correctly too
  (a project maintainer's own, from hardware they own — see
  `emu/tests/boot/README.md`'s "Verified against real Commodore ROMs
  too"), where `CHR$()` works fine — real KERNAL just takes noticeably
  longer to boot than `open-roms`'s shortcut (~2.14M cycles vs. ~200K).

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
next-language-features order for `cc64` and the decision that its
future standard library stays independent of `asm/lib/` rather than
wrapping it (root `ROADMAP.md`'s "Recently done" has the full
reasoning), and `emu/ROADMAP.md` has `c64emu`'s staged build order
(CPU -> memory -> GTK shell -> CIA -> VIC-II first pass -> VIC-II
second pass -> SID).

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
    python3 bin/mini6502.py tests/$f.prg tests/$f.lst
done
```

Each `tests/*.c` file targets one compiler area — `features.c` (arithmetic/
bitwise/comparison/control-flow), `pointers.c`, `recursion.c` (including an
80-deep recursion stress case), `include.c` (`#include` + stdlib), `structs.c`,
`forward.c` (declaration-order independence).

### `emu/` — the emulator

```sh
cd emu
make                       # -> bin/c64emu (needs GTK4 AND SDL2 dev libs, e.g. `brew install gtk4 sdl2`)
make clean

./bin/c64emu [rom-dir] [--prg PATH]     # rom-dir defaults to "roms"; runs fine with 0/3 ROMs loaded; --prg auto-runs a c64asm-built .prg once boot reaches READY.; --help/-h for the full option reference
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

# CIA chip (timers, ports, ICR) and its C64-specific keyboard/
# joystick/IRQ-NMI wiring: hand-written checks against the published
# 6526 register semantics and the standard C64 keyboard matrix
cd emu/tests/cia && make run
cd emu/tests/machine && make run

# VIC-II (raster counter, text rendering, char-ROM bank quirk): hand-written checks
cd emu/tests/vic && make run

# SID (waveform generators, ADSR envelopes, noise LFSR, hard sync): hand-written checks
cd emu/tests/sid && make run

# End-to-end: fetches MEGA65's open-roms (GPL-3.0/LGPL-3.0, unencumbered
# by design — NOT Commodore's own copyrighted ROMs, see emu/roms/README.md)
# and checks the whole machine actually boots to a BASIC READY. prompt
cd emu/tests/boot && make fetch && make run

# --prg SYS-injection return trampoline (a program that legitimately
# RTS's back after finishing didn't used to work — see emu/src/machine.h's
# machine_push_prg_return_trampoline() doc comment): hand-written
# regression check, no real ROMs needed
cd emu/tests/prg_inject && make run
```

All eight gates must pass before building on top of the module(s) they
cover — see `emu/tests/cpu/README.md`, `emu/tests/memory/README.md`,
`emu/tests/cia/README.md`, `emu/tests/machine/README.md`,
`emu/tests/vic/README.md`, `emu/tests/sid/README.md`,
`emu/tests/boot/README.md`, and `emu/tests/prg_inject/README.md` for
what "pass" looks like and how to re-derive the CPU suite's success
address if a future revision of it moves. `emu/tests/boot/` is the odd
one out among the other seven — it isn't a single module's hand-derived
unit tests, it's the only gate that exercises the CPU, memory map, both
CIAs, and VIC-II together against real third-party system software
(SID isn't part of that gate — nothing observable in screen RAM depends
on audio, and BASIC's own boot path never touches SID registers).

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
memory/bank-switching -> GTK4 shell -> CIA 1/2 -> VIC-II first pass
(all five done, and as of VIC-II this is now enough to actually boot —
tested against the MEGA65 `open-roms` open-source ROM replacement, it
runs unmodified to a readable BASIC `READY.` prompt) -> VIC-II second
pass done (raster IRQs, bad lines, multicolor text mode, both bitmap
sub-modes, extended-background-color mode, and sprites - light pen not
planned, see `emu/ROADMAP.md`'s "Not yet scheduled") -> SID (chip core,
address-map wiring, and real SDL2 audio output all done - waveforms,
ADSR envelopes, hard sync, ring modulation; exact analog filter
modeling not started). PAL timing only; cartridge and 1541 disk-drive
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
  `machine.c` registers a real `IoBus` here that dispatches VIC-II/
  color RAM/CIA1/CIA2 to their own models (see below) — SID and
  cartridge I/O still fall through to the original inert placeholder
  (reads `$FF`, writes dropped).
- **ROM images** (`roms/`) are not checked in — Commodore's copyrighted
  binaries. See `emu/roms/README.md`.
- **CIA chip** (`src/cia.c`/`src/cia.h`): a chip-generic MOS 6526
  model, no C64-specific wiring assumptions. PRA/PRB read semantics
  are per-bit: an output-configured (DDR=1) bit reads back its output
  latch, an input-configured (DDR=0) bit reads `porta_in`/`portb_in` —
  external pin state the *caller* must set before reading (see
  `machine.c` below; that's the seam that lets one chip-generic model
  serve both CIA1's keyboard/joystick wiring and CIA2's, which has
  none of that). Timer A/B are 16-bit down-counters with continuous
  and one-shot modes, and Timer B can cascade off Timer A's underflows
  (its INMODE). ICR: writing with bit7 set/clear sets/clears mask bits
  0-4; reading returns which sources are pending (regardless of mask)
  OR'd with bit7 iff `pending & mask != 0`, and always clears all
  pending bits — which is also what de-asserts `cia_irq_line()`, real
  6526 behavior, not optional. TOD clock and the serial data register
  are passive storage only (no real ticking/shifting) — a documented
  simplification, not needed for anything on the current roadmap.
  Verified against hand-derived expectations from the published 6526
  register semantics (`tests/cia/`) — there's no third-party CIA test
  suite the way the CPU core has Klaus Dormann's.
- **VIC-II** (`src/vic.c`/`src/vic.h`, steps 5-6): a free-running
  raster line counter (63 cycles/line, 312 lines/frame, PAL —
  `vic_tick()`, called from `machine_step()` the same way
  `cia_tick()` is, so `$D011`/`$D012` reflect a real, continuously
  advancing value — this specifically is what real KERNAL/BASIC boot
  code polls to detect the passage of time before CIA/VIC interrupts
  are even set up, and what unblocked booting to `READY.` at all),
  standard 40x25 hi-res text mode, and a solid border/background color
  (`$D020`/`$D021`) with DEN (`$D011` bit 4) blanking the whole
  display to the border color when clear. Reads screen/character
  memory directly out of `Memory`'s `ram`/`char_rom` fields — NOT
  through `memory_read()`, since the VIC has its own view of memory
  that never sees the CPU's ROM bank-switching, only plain RAM (plus
  one exception: implements the real hardware quirk where the
  character ROM is visible to the VIC — never the CPU — specifically
  in banks 0 and 2, not 1 or 3, when the character pointer selects
  offset `$1000`/`$1800`, since the ROM chip's select line is
  physically wired to just those two banks' decoding). Color RAM
  (`$D800`-`$DBFF`, a real physically separate 4-bit-wide chip, low
  nibble meaningful) lives in `Vic` since only rendering ever reads it.
  **Raster IRQs** (step 6): `$D011`/`$D012` are real shared registers —
  reading still returns the live raster position, but writing sets
  `raster_compare` instead (real hardware reuses the same two
  addresses for both, not a bug). `$D019` bits 0-3 are a real
  write-1-to-clear pending byte (`vic_write()` special-cases this —
  it's not a plain assignment), bit 7 a read-only summary of
  `pending & $D01A`; `vic_irq_line()` feeds `cpu.irq_line` alongside
  `cia_irq_line(&cia1)` (real hardware wired-OR, see `machine_step()`).
  Writing the new tests for this caught two real, pre-existing bugs:
  `machine_init()` wasn't zeroing `Machine`'s embedded `Cpu6502` at
  all — `cpu_reset()` only ever touched sp/p/pc/nmi_pending — so
  `cpu.irq_line` started as uninitialized stack garbage until the
  first `machine_step()` call happened to overwrite it (now fixed with
  a `memset(m, 0, sizeof(*m))` at the top of `machine_init()`); and
  `vic_read()`'s `$D019` bit 7 summary was documented in a comment but
  never actually implemented (fell through to plain passive storage).
  **Bad lines**: real VIC-II hardware asserts `/RDY` (freezing the CPU)
  whenever the raster line's low 3 bits match YSCROLL (`$D011` bits
  0-2), the line is within `$30`-`$F7`, and DEN is set — `vic_tick()`
  detects this on line-entry and sets a pending stall of
  `VIC_BADLINE_STALL_CYCLES` (40, the standard cited figure, not
  independently cycle-verified). Since `cpu.c` executes whole
  instructions atomically with no way to interrupt one mid-flight, the
  stall is its own dedicated `machine_step()` call:
  `vic_take_badline_stall()` returns the pending amount and clears it;
  if nonzero, that step ticks only the CIAs/VIC by that many cycles and
  skips `cpu_step()` entirely, so the CPU makes zero progress that call
  — verified directly (`tests/machine/`: PC and `cpu.cycles` genuinely
  don't advance during a stall call). **Multicolor text mode**: MCM
  (`$D016` bit4) makes text mode "mixed" — real hardware behavior, not
  a bug: color RAM bit3 becomes a PER-CELL mode flag instead of part of
  the color. bit3=0 still renders that cell plain hi-res, masked to
  color RAM bits 0-2 (colors 0-7 only); bit3=1 renders it as true
  4-color multicolor instead (background color 0/1/2 — `$D021`/
  `$D022`/`$D023`, the latter two new — or color RAM bits 0-2 as the
  4th color), each 2-bit pixel-pair covering 2 real pixels, so
  multicolor cells render at half the horizontal resolution of hi-res
  ones. One expression (`mcm ? (color_val & 0x07) : color_val`) covers
  the foreground color for all three cases (MCM off, MCM on with
  bit3=0, and the "11" pair color when bit3=1) since they only differ
  in whether bit3 is masked away. **Bitmap modes**: BMM (`$D011` bit5)
  switches from text mode to bitmap mode entirely — a different memory
  layout, not a text-mode variant. Screen RAM holds per-CELL color info
  directly instead of a character code (no character-code indirection,
  and no character ROM involved — that's text-mode-only); `$D018`'s
  char/bitmap-pointer field only uses bit3 in bitmap mode (bits1-2 are
  ignored) to pick the bitmap data's `$0000`/`$2000` offset within the
  VIC bank. Standard bitmap (MCM=0): each cell's own screen-RAM byte
  holds its 2 colors directly (upper nibble for set bits, lower for
  clear) — color RAM isn't used at all. Multicolor bitmap (MCM=1): each
  2-bit pixel-pair picks one of 4 colors — `$D021`, screen RAM's upper
  nibble, screen RAM's lower nibble, or color RAM's low nibble — a
  different color-source list than multicolor TEXT mode's (which uses
  `$D022`/`$D023`, not screen RAM). The shared hi-res/multicolor pixel-
  writing logic (identical between text and bitmap modes) is factored
  into one `render_cell()` helper rather than duplicated. **Extended
  background color mode**: ECM (`$D011` bit6) is text-mode only — real
  hardware treats ECM combined with MCM and/or BMM as an "invalid mode"
  that renders the whole display window (border unaffected) solid
  black instead of any meaningful pixel data, checked first in
  `vic_render_frame()` before either mode's own path runs. With ECM set
  alone, the character code's top 2 bits pick one of 4 background
  colors (`$D021`-`$D024`, the last one new) for that cell instead of
  contributing to which glyph is shown, and only the low 6 bits of the
  code address character memory — so only 64 of the normal 256
  characters are reachable, real hardware behavior. Foreground still
  comes from the full 4-bit color RAM value, same as plain hi-res text.
  **Sprites**: all 8 hardware sprites, composited on top of whatever
  text/bitmap rendering produced, in two passes at the end of
  `vic_render_frame()` (see the algorithm comment directly above that
  code in `vic.c`). Position (`$D000`-`$D00F` X/Y pairs plus `$D010`'s
  X MSBs), enable (`$D015`), Y/X expansion (`$D017`/`$D01D` — each
  source pixel drawn 2x), and per-sprite multicolor (`$D01C`) are all
  real. A sprite is 24x21 pixels unexpanded: 3 contiguous data bytes
  (24 bits) per row, 21 rows, pointed to by the LAST 8 bytes of the
  current video matrix (`screen_base+0x3F8+n`, a real hardware
  convention reused here, not incidental) times 64. Hi-res sprite
  pixels are opaque (that sprite's own color, `$D027`-`$D02E`) or
  transparent; multicolor sprite pixels use a DIFFERENT palette shape
  than multicolor text/bitmap modes — '01'/'11' are the two SHARED
  multicolor registers (`$D025`/`$D026`, same for every multicolor
  sprite), '10' is that sprite's own color, '00' always transparent.
  Sprite-to-graphics priority (`$D01B`) reuses a new per-pixel
  "graphics foreground" mask — `render_cell()` now stamps this
  alongside color at essentially no extra cost, since the same
  hi-res-bit/multicolor-pair-nonzero test it already computes IS the
  real VIC-II definition of "foreground" — so a sprite can be hidden
  specifically behind graphics foreground pixels while staying in
  front of the background, not a text-only fg/bg model repurposed.
  Sprite-to-sprite priority is just draw order (sprite 0 highest,
  drawn last, so it wins ties). Sprite-sprite and sprite-background
  collision (`$D01E`/`$D01F`) are detected once per frame in the same
  pass — not continuously, since rendering itself isn't scanline-by-
  scanline yet — OR'd into their registers rather than overwritten
  (only an explicit READ clears either one, a real hardware detail
  different from `$D019`'s write-1-to-clear), and feed `$D019` bits
  1/2 the same way raster IRQs already fed bit 0. Light pen is not
  planned (see `emu/ROADMAP.md`'s "Not yet scheduled"). Rendering still
  happens once per whole frame, not scanline by scanline, so a raster
  IRQ handler that pokes `$D020`/`$D021` mid-frame for a split-screen
  effect still won't show up in the picture, even though bad lines now
  stall the CPU correctly and raster IRQs fire at the right line.
  Verified against hand-derived
  expectations, including that a multicolor cell's fallback to plain
  hi-res (bit3=0) genuinely takes that code path rather than just
  happening to render the same pixels, both bitmap sub-modes, ECM's
  background-color selection and character-code masking, both ECM
  invalid-mode combinations, sprite shape/position, disabled sprites
  genuinely not rendering, multicolor sprites' distinct palette shape,
  X/Y expansion actually doubling source pixels, both directions of
  `$D01B` priority within a single render, sprite-vs-sprite draw
  order, and both collision registers including their read-clears-
  but-`$D019`-needs-its-own-clear distinction (`tests/vic/`,
  `tests/machine/`), and, together with the CPU/memory/CIA modules it
  depends on, against real system software actually booting
  (`tests/boot/` — see below).
- **SID** (`src/sid.c`/`src/sid.h`, step 7, in progress): the 3-voice
  synth chip, modeled as a pure chip core with no notion of sample
  rates or an OS audio API — `sid_tick()` advances internal state by
  real SID clock cycles (called from `machine_step()` the same way
  `cia_tick()`/`vic_tick()` are — SID shares the CPU's PHI2 clock, no
  rate conversion needed here) and `sid_output()` pulls the current
  instantaneous mixed sample; a caller wanting actual playback would
  tick cycle-for-cycle alongside the CPU and sample at whatever cadence
  produces its target rate — that wiring (and the audio backend it
  needs) doesn't exist yet, the same gap `vic_render_frame()` had
  before `gtk/main.c`'s blit loop existed. Each voice: a 24-bit
  phase-accumulator oscillator (sawtooth = its top 12 bits directly;
  triangle = the same but XOR-inverted once the accumulator's MSB is
  set, producing the up/down ramp; pulse = a 12-bit comparison against
  the pulse-width register; noise = a 23-bit Fibonacci LFSR, taps at
  bits 22/17 feeding bit0, clocked on the SAME accumulator's bit19
  rising edge — which is why noise pitch is controllable via the
  frequency register just like the others), hard sync and ring
  modulation against a fixed "previous voice in the ring" (voice
  1<-3, 2<-1, 3<-2, real hardware wiring), and an independent ADSR
  envelope generator using the real chip's published rate-period table
  (attack is linear) and its exponential decay/release approximation
  (decrements only on a 1-in-N rate-period match, where N depends on
  the CURRENT envelope value — steep at high values, crawling at low
  ones, an approximation of analog RC discharge). GATE edges are
  detected and acted on immediately in `sid_write()`, not polled during
  `sid_tick()` — real hardware behavior, and it's also why re-triggering
  attack mid-release resumes from wherever the envelope already sits
  rather than resetting to 0. Two deliberate simplifications, both
  flagged in `sid.h`'s header comment: the analog filter
  (`$D415`-`$D418`'s cutoff/resonance/routing/mode bits) is stored as
  plain register state but never applied to the output; a voice with
  more than one waveform selected at once uses the common bitwise-AND
  software approximation real combined waveforms don't exactly follow
  (they're an idiosyncratic per-chip analog quirk). `$D41B`/`$D41C`
  (OSC3/ENV3) are real, live outputs — voice 3's current waveform/
  envelope value, respectively, which is what lets real software (like
  BASIC's classic RND-via-SID trick) read pseudo-randomness or use
  voice 3 purely as an unheard oscillator via `$D418` bit7 ("voice 3
  off", which mutes it from the mix without disabling it). `$D419`/
  `$D41A` (POTX/POTY) read as 0xFF — no paddle hardware modeled.
  Verified against hand-derived expectations for each waveform's exact
  shape (including a combined-waveform AND check and a noise LFSR shift
  computed by hand from its documented reset seed), hard sync forcing
  an accumulator to 0 at a precisely hand-computed cycle, linear attack
  timing, decay correctly stopping at the sustain level rather than
  continuing to 0, the exponential decay/release slowdown (demonstrated
  qualitatively — the same fixed cycle budget produces far fewer steps
  from a low starting envelope value than a high one), and gate-edge
  handling (`tests/sid/`). Not yet covered by `tests/boot/` — nothing
  in BASIC's own boot path touches SID registers, so a `tests/boot/`
  failure still means "check CPU/memory/CIA/VIC-II", not SID.
- **Machine wiring** (`src/machine.c`/`src/machine.h` — replaces the ad
  hoc CPU+Memory wiring `gtk/main.c` used to do inline): ties CPU +
  Memory + CIA1 + CIA2 + VIC-II + SID together. Registers one `IoBus`
  with `Memory` that dispatches `$D000`-`$D3FF` to VIC-II registers,
  `$D400`-`$D7FF` to SID, `$D800`-`$DBFF` to color RAM, `$DC00`-`$DCFF`
  to CIA1, and `$DD00`-`$DDFF` to CIA2 (cartridge I/O still falls
  through to the inert placeholder). `machine_vic_bank()` resolves the
  VIC's current 16K bank from CIA2 PRA bits 0-1 (via `cia_read()`,
  reusing CIA's own DDR/output-latch logic rather than duplicating it)
  for `gtk/main.c` to pass into `vic_render_frame()`. Implements the
  actual C64-specific keyboard-matrix/joystick wiring on top of the
  generic CIA1: `update_keyboard_pins()` computes a pin-pulldown model
  in both directions (PRA driving columns pulls down PRB's rows
  wherever a held key matches, and vice versa, since real software
  occasionally scans in either direction) — joystick 2 shares PRA's
  pins 0-4 with keyboard column-select, joystick 1 shares PRB's, a real
  and well-known hardware quirk (see `asm/docs/c64-memory-reference.md`
  §6). `machine_step()` calls `cpu_step()` once (or skips it entirely
  on a bad-line stall — see `vic_take_badline_stall()`), ticks both
  CIAs, the VIC, and SID by exactly that many cycles (NOT batched per
  video frame — that would make CIA timers grossly imprecise and could
  miss interrupts; SID keeps running through a CPU stall too, since
  /RDY only pauses the CPU's own fetch/execute, not the rest of the
  bus), then propagates CIA1's AND VIC-II's interrupt outputs (wired-OR,
  real hardware) into `cpu.irq_line`, and CIA2's into `cpu_nmi()`
  (edge-triggered — CIA2's IRQ output is wired to the CPU's /NMI pin on
  real hardware, not /IRQ, so `machine.c` tracks the previous state
  itself and only calls `cpu_nmi()` on a 0->1 transition; SID has no
  interrupt output at all). Verified against hand-derived expectations
  for keyboard scanning (both directions), joystick/keyboard
  pin-sharing, and IRQ/NMI propagation (`tests/machine/`). SID audio
  OUTPUT is wired up in `gtk/main.c`, not here — see that bullet below.
- **GTK4 shell** (`gtk/main.c`): drives the whole `Machine` via a
  `g_timeout_add` loop at ~50 Hz (PAL frame rate; `CYCLES_PER_FRAME`
  — literally `PAL_CYCLES_PER_LINE * PAL_LINES_PER_FRAME` from `vic.h`
  — worth of `machine_step()` calls per tick, not raster-synchronized,
  since `vic_render_frame()` draws a whole frame at once rather than
  scanline by scanline). Renders by calling `vic_render_frame()` into
  a `uint32_t` pixel buffer sized to cairo's own required stride (via
  `cairo_format_stride_for_width()`, NOT assumed to be
  `VIC_CANVAS_W * 4` — that can have alignment padding), then blits it
  in one call with `cairo_image_surface_create_for_data()` +
  `cairo_paint()` rather than per-pixel `cairo_rectangle()`/`cairo_fill()`
  calls, which wouldn't keep up at ~50Hz for a canvas this size. The
  window opens at `WINDOW_ZOOM_DEFAULT` (2x) the VIC's native
  ~403x284 canvas — too small to read comfortably at 1x on a modern
  display — and stays freely resizable from there: the drawing area is
  set to `hexpand`/`vexpand`, and `draw_screen()` scales the native-
  resolution render (nearest-neighbor filtering, so pixel art stays
  crisp rather than blurring) up to whatever size the widget's draw
  callback reports each frame, uniformly and letterboxed to preserve
  the C64's aspect ratio — so dragging the window to a new size is the
  zoom control, not a fixed multiplier or a separate menu action. Runs
  fine with 0/3 ROMs loaded (the CPU just executes a harmless BRK loop
  on zeroed memory). Keyboard events are real: GDK key events are
  translated through `c64_keymap[]` (GDK keyval -> C64 keyboard-matrix
  PA/PB position, standard published matrix — not exhaustive, see the
  table's own comment for what's missing) into `machine_set_key()`
  calls, so typing in the window reaches BASIC/KERNAL once real ROM
  images are present, echoed to the real screen the same as on real
  hardware — no separate debug logging needed anymore now that VIC-II
  actually renders the result.
  **Joystick**: any SDL_GameController-recognized pad (Xbox
  controllers work via SDL2's built-in mappings, no extra config)
  drives `machine_set_joystick()`'s port 2 — the port every
  joystick-aware `asm/examples/` demo reads (see `pong.asm`'s own
  header comment). `poll_joystick()` in `gtk/main.c` runs once per
  `tick()`, pumping SDL's event queue for `SDL_CONTROLLERDEVICEADDED`/
  `REMOVED` (SDL's only mechanism for hotplug detection — there's no
  polling-only check) and reading whichever controller is currently
  open: D-pad buttons OR the left stick past a deadzone
  (`JOYSTICK_AXIS_THRESHOLD`) for direction — since either a hat-style
  D-pad or an analog stick can be what a given pad reports — and
  button A for fire, the conventional single-fire-button mapping every
  C64 joystick game expects. Only one controller is tracked at a time
  (the first one found); a second connecting later is ignored, not a
  crash. A disconnect resets joystick 2 to all-released rather than
  leaving it stuck in its last-read state. No controller connected is
  never fatal — `poll_joystick()` is a no-op and the emulator runs on
  keyboard input alone, same graceful-degradation spirit as audio
  failing to open. Built with `-std=c11`, not this
  toolkit's usual `-std=c99` — GTK4's own headers rely on C11
  typedef-redefinition tolerance (`G_DECLARE_*_TYPE` macros);
  `src/*.c` itself stays plain C99-compatible regardless of which
  standard compiles it. **Audio**: real SID output, played back through
  SDL2 (`SDL_Init(SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER)` +
  `SDL_OpenAudioDevice()` + a
  callback — a new dependency alongside GTK4's own, `brew install
  gtk4 sdl2`). `sid_tick()`/`sid_output()` are only ever called from
  the GTK main thread, inside `tick()` — SDL's audio callback runs on
  its OWN dedicated real-time thread, so touching `Sid` directly from
  it would be a genuine data race, not just a style concern; the two
  are bridged through a small ring buffer instead, guarded by
  `SDL_LockAudioDevice()`/`SDL_UnlockAudioDevice()` (SDL's own
  documented mechanism for exactly this split — locking pauses callback
  invocation, so the main thread can safely touch shared state and the
  callback needs no locking of its own). `tick()` decides how many of
  SID's real clock cycles (`SID_CLOCK_HZ`, the SAME fixed nominal clock
  CPU/VIC/CIA are all implicitly paced against, NOT measured real
  wall-clock time — see this bullet's second/third real bugs below for
  why) are owed per 44.1kHz sample, interleaved with the cycle-stepping
  loop itself rather than pulled in a burst afterward (equally
  load-bearing — see the same bugs below), accumulating fractionally
  since that ratio isn't a whole number, and pushes finished samples
  into the ring; the callback
  only ever reads it, zero-filling on underrun rather than repeating
  stale samples. `sid_output()`'s raw output is deliberately DC-biased,
  not centered on 0 (see `sid.h`) — fed straight through as-is, NOT
  shifted, so silence is always exactly 0, matching the underrun
  filler exactly. **Real bug, found from an actual user report**: an
  earlier version DID shift each sample by -16384 to look like
  conventional bipolar PCM, which meant silence became a nonzero
  constant that didn't match the underrun filler's plain 0 — every
  routine ring-buffer underrun (GTK's timer and SDL's real-time audio
  thread never stay perfectly in lockstep) then produced an audible
  click, jumping between the two. `sid_output()` itself was traced by
  hand and confirmed rock-steady at exactly 0 through boot and idle
  with zero fluctuation, ruling out a chip-logic bug and pointing
  straight at this mismatch instead — "random low notes repeated"
  turned out to be a train of silence-to-silence level jumps, not real
  chip output. Fixed by dropping the shift. Audio failing to open is
  never fatal, same graceful-degradation spirit as running with 0/3
  ROMs loaded. Checked by actually running the built binary with real
  ROMs loaded and confirming it starts, opens the audio device without
  error, and shuts down cleanly — not covered by an automated test the
  way the chip core is (`tests/sid/`), since there's no meaningful way
  to unit-test "did a real ring buffer correctly hand off to a real
  audio thread" without an actual running audio backend — exactly why
  this bug slipped through until someone actually listened to it.
  **A second real bug, caught while testing `asm/examples/` against the
  emulator**: short sound effects (a bounce/paddle-hit blip) sounded
  fine, but `pong.asm`'s much longer "miss" sound effect (several
  seconds of ADSR decay, vs. the blips' fraction of a second) came out
  "broken up." Temporarily instrumenting the ring buffer directly
  showed dozens of real underrun events every single second,
  continuously, for the whole session — not specific to the miss sound
  at all, just something only a sound long enough to last through
  several of those gaps could reveal; the short blips never lasted long
  enough for a human ear to register the underlying stutter. Root
  cause: `tick()`'s audio pacing generated exactly one frame's worth of
  samples per `g_timeout_add()` call, assuming each call corresponded
  to exactly `FRAME_MS` (20ms) of real time — `g_timeout_add()` never
  actually guarantees that, and the real average interval running even
  slightly longer (ordinary GTK/OS scheduling overhead, nothing exotic)
  was a continuous ~5% sample production deficit against SDL's own
  hardware-clocked 44.1kHz consumption — a systematic rate mismatch, not
  occasional jitter a bigger ring buffer could have absorbed (a
  systematic deficit drains any fixed-size buffer eventually). First
  attempted fix: pace sample generation against actual elapsed
  wall-clock time instead (`g_get_monotonic_time()`) — underrun events
  genuinely dropped to zero. **A third real bug, introduced by that
  very fix and caught immediately by ear**: every sound started coming
  out as the same low, warbling tone. Root cause: `sid_output()` only
  ever reads current instantaneous state, it doesn't advance anything —
  the real-time fix had moved sample generation to run in a burst AFTER
  the whole frame's cycle-stepping loop instead of interleaved with it,
  so every sample in a frame read back the same frozen oscillator
  state, downsampling every waveform to ~50Hz and aliasing anything
  above ~25Hz into low beat-frequency warbling. Restoring the
  interleaving alone still wasn't enough — pacing the interleaved
  accumulator against real time still measurably shifted pitch
  (confirmed by capturing real output and counting zero-crossings: a
  tone that should measure ~720Hz measured ~520-550Hz instead), because
  it decoupled audio from the SAME fixed nominal clock CPU/VIC/CIA are
  all implicitly paced against regardless of real-time drift. Final
  fix: keep sample generation interleaved with cycle-stepping (required
  for a real waveform) AND paced against the fixed nominal clock
  (`SID_CLOCK_HZ`, required for correct pitch, consistent with the rest
  of the machine) — verified both ways this time, with captured output
  confirming stable ~720Hz across a multi-second capture. The underrun
  problem itself is only partially resolved as a result: measured
  directly, enlarging the ring buffer from 8192 to 65536 samples
  changed nothing about the underrun rate (a persistent deficit drains
  any size buffer to the same near-empty equilibrium), so
  `AUDIO_RING_SAMPLES` landed at a modest 16384 and a low, roughly 4-5%
  residual underrun rate remains — now occasional brief silence gaps
  rather than either of the two more severe bugs above. Eliminating it
  outright would mean making the whole emulated machine's timing
  genuinely real-time-locked, not just audio — a bigger change, not
  undertaken without weighing it first. **A fourth real bug**, caught
  testing `asm/examples/bounce.asm`: that residual underrun rate is
  only inaudible while the signal is ALSO near silence — `bounce.asm`'s
  bounce-sound one-shot decays fast (well under 150ms) and spends most
  of its life at real amplitude, so an underrun landing in that window
  used to force an abrupt jump down to `audio_callback()`'s old hard-0
  fill and back up once real samples resumed, a genuine discontinuity
  heard as a short "scratch" at the tail of the bleep. Confirmed via a
  headless capture reproducing `tick()`'s exact pacing against real
  jittered timing: every amplitude jump bigger than a real waveform
  step coincided with an underrun-filled sample. Fixed by holding the
  last real sample on underrun instead of snapping to 0 (a `static`
  local inside `audio_callback()`, audio-thread-only state that needs
  no cross-thread synchronization) — re-running the same capture at the
  same underrun rate produced zero discontinuities, and true silence is
  unaffected since it already decays to a stored value of exactly 0.
- **End-to-end boot test** (`tests/boot/`): the odd one out among
  `emu/`'s test suites — every other one (`tests/cpu/`, `tests/memory/`,
  `tests/cia/`, `tests/machine/`, `tests/vic/`) checks a single module
  in isolation against hand-derived expectations; this fetches a real,
  unencumbered open-source ROM replacement (MEGA65's `open-roms`,
  GPL-3.0/LGPL-3.0 — safe to auto-fetch, unlike Commodore's own ROMs,
  see `emu/roms/README.md`) and runs the whole `Machine` from reset
  until "READY." literally appears in screen RAM (in C64 screen codes,
  not ASCII/PETSCII), the same signal a real C64 gives once BASIC has
  finished initializing. Catches integration bugs no module's own unit
  tests could — this is exactly how the VIC-II raster counter's absence
  was first noticed, by tracing a real boot getting stuck polling
  `$D012` (see `emu/ROADMAP.md`'s step 5 entry) — so treat a `tests/boot/`
  failure as "something in the CPU/memory/CIA/VIC-II integration broke,
  go check each module's own suite to narrow it down," not as its own
  root cause.
- **Running `asm/`'s example programs** (`gtk/main.c`'s `--prg PATH`
  flag, `machine_load_prg()`/`machine_find_sys_target()` in
  `machine.c`): loads a c64asm-built `.prg` straight into RAM and jumps
  the CPU to its BASIC stub's `SYS` target once boot reaches a real
  READY. prompt — the same shortcut `asm/examples/mini6502.py`'s own
  loader takes to run c64asm's output without a real 1541 disk drive
  (not implemented), except this waits for READY. first rather than
  injecting immediately after reset, since real hardware can only
  LOAD+RUN once BASIC/KERNAL's own startup (IRQ vectors, CIA timer
  setup) has actually run. Verified by spot-checking `asm/examples/`
  demos this way and dumping `vic_render_frame()`'s output to an image
  rather than relying on a live window (no screen-recording access in
  this development sandbox): `pong.prg` (paddle/ball/net sprites and
  score text, all correct — the first real, substantial program to
  exercise this session's sprite work, not just this project's own
  hand-derived unit tests), `bounce.prg`, `lander.prg` (bitmap-mode
  terrain), `sprites.prg`, and `music_demo.prg` — all ran correctly
  using real KERNAL routines like `CHROUT` (unlike `mini6502.py`, which
  traps those instead of executing real ROM code). Not a permanent
  automated suite — `asm/examples/test_*.py`'s 15 scripts each drive
  real win-conditions against `mini6502.py` and would need redoing
  against this emulator's own `machine_set_key()`/
  `machine_set_joystick()` API for equivalent coverage; this was a
  one-off manual verification pass, see `emu/ROADMAP.md`. Also used to
  run `cc64` (`C/`) output for the first time — every `asm/examples/`
  demo tested this way loops forever, so a program that legitimately
  `RTS`s back to BASIC after finishing (the normal case for a `cc64`
  program, unlike a game) had never been exercised through this path
  before, and it caught a real bug in `try_inject_prg()` itself:
  jumping straight to the `SYS` target via `cpu.pc = sys_target` never
  pushed a return address the way a genuine `JSR` (what BASIC's own
  `SYS` execution does on real hardware) would have, so that RTS
  popped whatever garbage was on the hardware stack instead — reliably
  reproducible with a trivial hand-assembled `.asm` program too,
  confirming it had nothing to do with `cc64` itself. Fixed by
  `machine_push_prg_return_trampoline()` (`emu/src/machine.c`/`.h` —
  moved there from `gtk/main.c` once a regression test needed to call
  it without linking the GTK/SDL shell, see `emu/tests/prg_inject/`):
  push a real two-byte return address before jumping in, pointing at a
  tiny `JMP`-to-self trampoline poked into `$033C` (the classic C64
  datassette buffer, safe unused RAM) rather than a real BASIC ROM
  address — landing back inside BASIC's actual interpreter would need a
  specific ROM entry point that varies by KERNAL build, so a harmless
  infinite loop is the ROM-independent choice instead, matching what a
  `--prg` demo that never returns was already effectively doing. See
  `emu/ROADMAP.md`'s "Running `cc64`'s output" for the full bisection.

### Cross-project test harness: `mini6502.py`

`asm/examples/mini6502.py` is a from-scratch 6502 CPU + C64Machine
emulator (CIA keyboard/joystick emulation, `CHROUT`/`CHRIN` trapping,
zero-page KERNAL-poisoning simulation) purpose-built to test-drive this
project's own output — not a general VICE replacement, and not the
same thing as `emu/`'s `c64emu` above. It has no CLI entry point of its
own (pure library, imported by `asm/examples/test_*.py` and by
`C/bin/mini6502.py` — see below); running it directly (`python3
asm/examples/mini6502.py ...`) silently does nothing, a real trap that
looks like a passing test since it produces no error either. It is not
a substitute for real-hardware/VICE testing — several real bugs in this
project's history were only caught on actual hardware and then
back-ported into the emulator as a new simulated failure mode (e.g. the
zero-page KERNAL poisoning above). If you fix a bug that a real device
caught but `mini6502.py` didn't, consider whether the emulator should
be taught to catch it too.

`C/bin/mini6502.py` is the CLI entry point `C/`'s own compiler tests
actually invoke (`python3 bin/mini6502.py program.prg program.lst`,
run from `C/`) — a thin wrapper that imports
`asm/examples/mini6502.py`'s `C64Machine` rather than its own separate
CPU implementation (it used to be one — a real, independently-
maintained ~350-line duplicate, formerly the subject of root
`ROADMAP.md`'s "Two separate `mini6502.py` copies" open question, now
resolved). `program.lst` is accepted but no longer read — the entry
point comes from `find_sys_target()` parsing the `.prg`'s own BASIC
stub directly, the same way `asm/`'s own test scripts find it.

### VICE (manual/real-hardware verification)

`asm/Makefile`'s `disk`/`examples` targets shell out to a local VICE
install (`c1541`, hardcoded path
`/Applications/vice-arm64-gtk3-3.10/bin/c1541`) to build a `.d64` disk
image of every example — used for testing beyond what `mini6502.py`
simulates (real disk I/O timing, raster behavior, etc.). This path is
machine-specific; expect to adjust `VICE_C1541` in `asm/Makefile` on a
different setup.
