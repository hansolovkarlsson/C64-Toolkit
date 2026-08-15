# End-to-end boot test

`test_boot.c` is this project's only end-to-end correctness gate: it
loads real KERNAL/BASIC/character ROMs, runs the whole `Machine`
(CPU + memory map + both CIAs + VIC-II, all together) from reset, and
checks that "READY." actually appears in screen RAM - the same thing a
real C64 prints once BASIC has finished initializing and is waiting
for input. Every other test in `../` (`cpu/`, `memory/`, `cia/`,
`machine/`, `vic/`) checks one module in isolation against hand-derived
expectations; this is the one check that every module is wired
together *correctly*, against real, unmodified third-party system
software rather than a synthetic fixture.

## Getting the ROM images

Uses [MEGA65's `open-roms` project](https://github.com/MEGA65/open-roms)
(GPL-3.0/LGPL-3.0) - a from-scratch KERNAL/BASIC/character ROM
replacement built specifically to be free of Commodore's copyright,
unlike the real ROMs `../../roms/README.md` covers (that file's "don't
ask an AI assistant to fetch these" warning is about Commodore's own
binaries specifically - it doesn't apply to `open-roms`, which exists
for exactly this reason). Not vendored into this repo, same reasoning
as `../cpu/`'s Klaus Dormann suite:

```sh
make fetch   # downloads kernal.rom/basic.rom/chargen.rom into roms/ (this directory only, not ../../roms/)
make run     # builds test_boot and runs it against them
```

## What "pass" looks like

```
PASS: booted to READY. after 200000 cycles
```

`READY.` typically appears within the first ~200,000 cycles (~0.2
seconds of simulated C64 time) against the `open-roms` build this was
last verified with. The test budgets up to 2,000,000 cycles before
calling it a real failure - a comfortable margin, not a tight bound.

On failure:

```
FAIL: never reached READY. within 2000000 cycles (PC=$XXXX, A=$XX, P=$XX)
```

That's a real regression somewhere in the CPU/memory/CIA/VIC-II
integration - worth checking each module's own test suite first
(`../cpu/`, `../memory/`, `../cia/`, `../machine/`, `../vic/`) to
narrow down which one broke, since this test alone won't say which.

## Known limitations: `open-roms` BASIC completeness

This gate only checks that BASIC/KERNAL initialize far enough to print
`READY.` - it says nothing about how complete `open-roms`'s BASIC
interpreter actually is. `open-roms` is a from-scratch, still-in-
development reimplementation (see its own repo for current status),
not a drop-in, feature-complete match for real Commodore BASIC 2.0.

**Confirmed missing: `CHR$()`.** Typing `10 PRINT CHR$(65)` then `RUN`
produces:

```
?NOT IMPLEMENTED ERROR IN 10
```

not a syntax error - the `CHR$` token is recognized by the tokenizer,
but the routine behind it isn't implemented in this build (`GENERIC
BUILD RELEASE DEV.210823.FC.1`, the version this project currently
fetches). Other BASIC commands may have similar gaps; this hasn't been
exhaustively tested, only surfaced when a real program needed `CHR$`.

**How this was verified** (worth reusing for testing any other
suspected gap): a throwaway harness booted to `READY.`, poked a real
tokenized BASIC program directly into `$0801` (skipping `LOAD`, so it
also fixed up `VARTAB`/`ARYTAB`/`STREND` - `$2D`-`$32` - to point past
the program, the same bookkeeping a real `LOAD` would do), then
**typed `RUN` through the real keyboard matrix** via
`machine_set_key()` - the same mechanism `gtk/main.c` uses for actual
keystrokes, not a shortcut that calls into BASIC internals directly.
That matters: it means whatever result came back is exactly what a
real keypress would have produced, not an artifact of some ROM-
internal shortcut. Not a permanent test (nothing to assert against
generically - what's "missing" depends entirely on what a given
program needs), so not checked in; recreate a similar harness (see
`../../src/machine.h`'s `machine_set_key()` and the C64 keyboard
matrix table in `gtk/main.c`'s `c64_keymap[]`) if a future program hits
another suspected gap and needs it confirmed the same rigorous way.

**Why not just use Commodore's real ROMs instead?** See
`../../roms/README.md` - this project (and an AI assistant working in
it) won't fetch those, copyright, regardless of where they'd be
stored locally. If you want to compare against real ROM behavior,
supply your own (legally dumped from hardware you own) into
`../../roms/` yourself - already gitignored, already documented there.
