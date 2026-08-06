# `c64emu`

A from-scratch Commodore 64 emulator: a cycle-stepped 6502/6510 CPU
core, memory/bank-switching, CIA/VIC-II/SID chip emulation, and a
GTK4 front end — built the same way `asm/` and `C/` were, without
leaning on an existing emulator core.

**Status:** steps 1-3 of the build order are done - a full legal
6502/6510 instruction set (`src/cpu.c`, verified against Klaus
Dormann's 6502 functional test suite plus a hand-written interrupt/
reset check), the C64's memory map/bank switching (`src/memory.c`,
verified against the full mode table with no cartridge present), and a
minimal GTK4 shell (`gtk/main.c`) that drives the CPU through a real
window/event loop and displays screen RAM as a raw grayscale grid (not
real VIC-II output - that doesn't exist yet). See `tests/cpu/README.md`,
`tests/memory/README.md`, and this file's "Building" section below.
No CIA/VIC-II/SID yet (so no real keyboard input, no real graphics, no
sound). See [`ROADMAP.md`](ROADMAP.md) for what's next.

## Why this exists, and how it relates to `asm/`'s `mini6502.py`

`asm/`'s `mini6502.py` (and `C/bin/mini6502.py` - see the root
[`ROADMAP.md`](../ROADMAP.md)'s "Two separate `mini6502.py` copies"
for why there are two) is a purpose-built *test harness*: it
implements just enough of the 6502 opcode/addressing-mode surface,
plus enough CIA/KERNAL zero-page behavior, to run `c64asm`/`cc64`
output and catch regressions. It was never meant to run arbitrary C64
software, has no VIC-II or SID emulation at all, and has no display -
CHROUT is trapped in software and printed as text.

`c64emu` is a different goal: a real, general-purpose C64 emulator
that can run actual games and demos, with a GUI. The two projects
don't share code - `c64emu`'s CPU core will need the *full* 6502 ISA
(not just what `cc64` happens to emit), plus VIC-II graphics, SID
audio, and CIA input, none of which `mini6502.py` has any of.

## Planned layout

```
emu/
├── src/    - the machine core: CPU, memory/banking, VIC-II, CIA, SID,
│             ROM loading. No GTK dependency - a portable library.
├── gtk/    - the GTK4 front end: window, framebuffer blit, keyboard/
│             joystick input, audio output. Consumes src/ through a
│             small interface (step the machine, read the framebuffer,
│             inject key events) - swapping in a different front end
│             later shouldn't require touching chip code.
├── tests/  - correctness gates, starting with Klaus Dormann's 6502
│             functional test suite for the CPU core.
├── docs/   - reference notes (opcode tables, VIC-II timing quirks,
│             memory map) written up as they're worked out, the same
│             way asm/docs/ grew alongside the assembler.
└── roms/   - NOT checked in - see roms/README.md.
```

## Building

```sh
make            # -> bin/c64emu (requires GTK4 dev libraries, e.g. `brew install gtk4`)
make clean

./bin/c64emu [rom-dir]   # rom-dir defaults to "roms" (see "ROM images" below)
```

The window drives the CPU at ~50 Hz and shows screen RAM
(`$0400`-`$07E7`) as a raw grayscale grid - not real graphics yet, see
`ROADMAP.md`'s step 3 entry for why. It runs fine with no ROMs loaded
(the CPU just executes a harmless BRK loop on zeroed memory), which is
enough to see the window/event loop working before real ROMs are
available.

Each core piece also has its own standalone correctness gate,
independent of the GTK build:

```sh
cd tests/cpu && make fetch && make run-all   # CPU core
cd tests/memory && make run                  # memory map / bank switching
```

See [`ROADMAP.md`](ROADMAP.md) for what's next (CIA 1/2).

## ROM images

This emulator needs real KERNAL/BASIC/character ROM dumps to boot a
C64 environment. They are **not included** in this repository -
Commodore's copyrighted binaries. See [`roms/README.md`](roms/README.md).
