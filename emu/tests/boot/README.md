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
