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
  including real keyboard/joystick wiring, VIC-II [40x25 hi-res AND
  multicolor text mode, border/background color, raster IRQs, bad
  lines], a GTK4 shell, eventually SID). Does not share code with
  `asm/`'s `mini6502.py` test harness — see "`c64emu` (`emu/`)" below
  for why they're deliberately separate. **Boots real system
  software**: tested against the MEGA65 `open-roms` open-source ROM
  replacement, it runs unmodified to a readable BASIC `READY.` prompt.
  Still no sound, no bitmap modes, no sprites.

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
VIC-II first pass -> VIC-II second pass -> SID).

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

# CIA chip (timers, ports, ICR) and its C64-specific keyboard/
# joystick/IRQ-NMI wiring: hand-written checks against the published
# 6526 register semantics and the standard C64 keyboard matrix
cd emu/tests/cia && make run
cd emu/tests/machine && make run

# VIC-II (raster counter, text rendering, char-ROM bank quirk): hand-written checks
cd emu/tests/vic && make run

# End-to-end: fetches MEGA65's open-roms (GPL-3.0/LGPL-3.0, unencumbered
# by design — NOT Commodore's own copyrighted ROMs, see emu/roms/README.md)
# and checks the whole machine actually boots to a BASIC READY. prompt
cd emu/tests/boot && make fetch && make run
```

All six gates must pass before building on top of the module(s) they
cover — see `emu/tests/cpu/README.md`, `emu/tests/memory/README.md`,
`emu/tests/cia/README.md`, `emu/tests/machine/README.md`,
`emu/tests/vic/README.md`, and `emu/tests/boot/README.md` for what
"pass" looks like and how to re-derive the CPU suite's success address
if a future revision of it moves. `emu/tests/boot/` is the odd one out
— unlike the other five, it isn't a single module's hand-derived unit
tests, it's the only gate that exercises the CPU, memory map, both
CIAs, and VIC-II together against real third-party system software.

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
pass (raster IRQs, bad lines, and multicolor text mode done; extended-
background-color mode, bitmap modes, and sprites not started) -> SID.
PAL timing only; cartridge and 1541 disk-drive emulation are
explicitly out of scope for now.

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
  in whether bit3 is masked away. Deliberately still deferred:
  extended-background-color text mode, bitmap modes, sprites, light
  pen — rendering still happens once per whole frame, not scanline by
  scanline, so a raster IRQ handler that pokes `$D020`/`$D021`
  mid-frame for a split-screen effect still won't show up in the
  picture, even though bad lines now stall the CPU correctly and
  raster IRQs fire at the right line. Verified against hand-derived
  expectations, including that a multicolor cell's fallback to plain
  hi-res (bit3=0) genuinely takes that code path rather than just
  happening to render the same pixels (`tests/vic/`, `tests/machine/`),
  and, together with the CPU/memory/CIA modules it depends on, against
  real system software actually booting (`tests/boot/` — see below).
- **Machine wiring** (`src/machine.c`/`src/machine.h` — replaces the ad
  hoc CPU+Memory wiring `gtk/main.c` used to do inline): ties CPU +
  Memory + CIA1 + CIA2 + VIC-II together. Registers one `IoBus` with
  `Memory` that dispatches `$D000`-`$D3FF` to VIC-II registers,
  `$D800`-`$DBFF` to color RAM, `$DC00`-`$DCFF` to CIA1, and
  `$DD00`-`$DDFF` to CIA2 (SID's `$D400`-`$D7FF` and cartridge I/O
  still fall through to the inert placeholder). `machine_vic_bank()`
  resolves the VIC's current 16K bank from CIA2 PRA bits 0-1 (via
  `cia_read()`, reusing CIA's own DDR/output-latch logic rather than
  duplicating it) for `gtk/main.c` to pass into `vic_render_frame()`.
  Implements the actual C64-specific keyboard-matrix/joystick wiring
  on top of the generic CIA1: `update_keyboard_pins()` computes a
  pin-pulldown model in both directions (PRA driving columns pulls
  down PRB's rows wherever a held key matches, and vice versa, since
  real software occasionally scans in either direction) — joystick 2
  shares PRA's pins 0-4 with keyboard column-select, joystick 1 shares
  PRB's, a real and well-known hardware quirk (see
  `asm/docs/c64-memory-reference.md` §6). `machine_step()` calls
  `cpu_step()` once, ticks both CIAs and the VIC by exactly that many
  cycles (NOT batched per video frame — that would make CIA timers
  grossly imprecise and could miss interrupts), then propagates CIA1's
  interrupt output into `cpu.irq_line` (level-triggered, direct
  assignment since it's currently the only IRQ source — VIC-II raster
  IRQs will need to OR in here once the second pass adds them) and
  CIA2's into `cpu_nmi()` (edge-triggered — CIA2's IRQ output is wired
  to the CPU's /NMI pin on real hardware, not /IRQ, so `machine.c`
  tracks the previous state itself and only calls `cpu_nmi()` on a
  0->1 transition). Verified against hand-derived expectations for
  keyboard scanning (both directions), joystick/keyboard pin-sharing,
  and IRQ/NMI propagation (`tests/machine/`).
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
  calls, which wouldn't keep up at ~50Hz for a canvas this size. Runs
  fine with 0/3 ROMs loaded (the CPU just executes a harmless BRK loop
  on zeroed memory). Keyboard events are real: GDK key events are
  translated through `c64_keymap[]` (GDK keyval -> C64 keyboard-matrix
  PA/PB position, standard published matrix — not exhaustive, see the
  table's own comment for what's missing) into `machine_set_key()`
  calls, so typing in the window reaches BASIC/KERNAL once real ROM
  images are present (also logged to stderr — with no VIC-II sprite/
  cursor feedback loop of its own yet, that's the only confirmation a
  keypress was recognized without ROMs echoing it to the screen).
  Joystick input isn't wired into the GTK shell yet even though
  `machine_set_joystick()` exists. Built with `-std=c11`, not this
  toolkit's usual `-std=c99` — GTK4's own headers rely on C11
  typedef-redefinition tolerance (`G_DECLARE_*_TYPE` macros);
  `src/*.c` itself stays plain C99-compatible regardless of which
  standard compiles it.
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
