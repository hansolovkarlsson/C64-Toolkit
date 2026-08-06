# `c64emu`

A from-scratch Commodore 64 emulator: a cycle-stepped 6502/6510 CPU
core, memory/bank-switching, CIA/VIC-II/SID chip emulation, and a
GTK4 front end — built the same way `asm/` and `C/` were, without
leaning on an existing emulator core.

**Status:** steps 1-6 are done (step 6, VIC-II second pass, done except
light pen - not planned, see `ROADMAP.md`'s "Not yet scheduled") and
step 7 (SID) is in progress - a full legal 6502/6510 instruction set (`src/cpu.c`,
verified against Klaus Dormann's 6502 functional test suite plus a
hand-written interrupt/reset check), the C64's memory map/bank
switching (`src/memory.c`, verified against the full mode table with
no cartridge present), a GTK4 shell (`gtk/main.c`), both CIAs
(`src/cia.c` for the chip, `src/machine.c` for the C64-specific
keyboard-matrix/joystick/IRQ-NMI wiring on top of it - real keyboard
input reaches the machine, typing in the window feeds CIA1's keyboard
matrix), and VIC-II (`src/vic.c` - real 40x25 hi-res, multicolor, AND
extended-background-color text mode, PLUS standard AND multicolor
bitmap mode, PLUS all 8 hardware sprites [position/enable/expansion/
multicolor, sprite-to-graphics and sprite-to-sprite priority, sprite-
sprite and sprite-background collision detection], through screen
RAM/character ROM/color RAM and whichever bank CIA2 selects, a solid
border/background color [plus three extra background-color registers
used by multicolor and extended-background-color mode], real raster
IRQs [CIA1 and VIC-II share the CPU's /IRQ line, matching real
hardware], and bad lines [the cycle-stealing DMA quirk - the CPU
genuinely stalls for `VIC_BADLINE_STALL_CYCLES` when one is entered]),
and SID (`src/sid.c` - the 3-voice synth: all 4 waveforms [triangle/
sawtooth/pulse/noise], hard sync, ring modulation, ADSR envelopes using
the real chip's published rate-period and exponential decay/release
tables, wired into the address map at `$D400`-`$D7FF`; no analog filter
modeling yet, and no audio OUTPUT wired up anywhere yet either - the
chip model runs and is fully tested, but nothing pulls samples out of
it into an actual sound API, so the emulator is still silent).
**This is enough to actually boot**: `tests/boot/` is an automated
end-to-end gate that fetches a real open-source ROM replacement (the
MEGA65 `open-roms` project) and checks that the whole machine runs it
unmodified all the way to a readable BASIC `READY.` prompt - the one
test that exercises the CPU, memory map, both CIAs, and VIC-II
together, not just each module in isolation (SID isn't part of this
gate - nothing in BASIC's own boot path touches SID registers). See
`tests/cpu/README.md`, `tests/memory/README.md`, `tests/cia/README.md`,
`tests/machine/README.md`, `tests/vic/README.md`, `tests/sid/README.md`,
`tests/boot/README.md`, and this file's "Building" section below. No
joystick wiring in the GTK shell yet even though
`machine_set_joystick()` exists. See
[`ROADMAP.md`](ROADMAP.md) for what's next.

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

The window drives the machine at ~50 Hz (PAL frame rate) and shows
real VIC-II output - see `vic.h`'s header comment for exactly what's
modeled (40x25 hi-res, multicolor, and extended-background-color text
mode, standard and multicolor bitmap mode, all 8 hardware sprites,
border/background color, DEN blanking, raster IRQs, bad lines) - light
pen is not planned, see `ROADMAP.md`. It runs fine with no ROMs loaded (the CPU just
executes a harmless BRK loop on zeroed memory, and the screen stays
whatever color `$D021`/`$D020` default to), which is enough to see the
window/event loop working before real ROMs are available - but with
real `kernal.rom`/`basic.rom`/`chargen.rom` in `roms/` (see "ROM
images" below), it now actually boots to a readable BASIC prompt.

Each core piece also has its own standalone correctness gate,
independent of the GTK build:

```sh
cd tests/cpu && make fetch && make run-all   # CPU core
cd tests/memory && make run                  # memory map / bank switching
cd tests/cia && make run                     # CIA chip (timers, ports, ICR)
cd tests/machine && make run                 # CIA <-> C64 wiring (keyboard, joystick, IRQ/NMI)
cd tests/vic && make run                     # VIC-II (raster counter, text rendering, char ROM bank quirk)
cd tests/sid && make run                     # SID (waveforms, ADSR envelopes, noise LFSR, hard sync)
cd tests/boot && make fetch && make run      # end-to-end: real ROMs, boots to READY.
```

`tests/boot/` is the one gate that isn't a single module's own
hand-derived unit tests - it fetches [MEGA65's `open-roms`
project](https://github.com/MEGA65/open-roms) (GPL-3.0/LGPL-3.0, an
unencumbered KERNAL/BASIC/character ROM replacement, safe to fetch
unlike Commodore's own ROMs - see `tests/boot/README.md`) and checks
that the whole machine actually boots to a `READY.` prompt, catching
integration bugs no single module's tests could.

See [`ROADMAP.md`](ROADMAP.md) for what's next (SID audio output).

## ROM images

This emulator needs real KERNAL/BASIC/character ROM dumps to boot a
C64 environment. They are **not included** in this repository -
Commodore's copyrighted binaries. See [`roms/README.md`](roms/README.md).
