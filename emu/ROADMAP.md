# `c64emu` roadmap

Nothing here is implemented yet - this is the staged build order,
easiest/best-understood pieces first, each one a correctness
foundation for the next. See `README.md`'s "Planned layout" for where
each piece will live.

## Build order

1. ~~**6502/6510 CPU core**~~ - done (`src/cpu.c`/`src/cpu.h`): the
   full legal instruction set, addressing modes, and per-instruction
   cycle counts (including the conditional +1 for page-crossing on
   indexed reads, and the branch-taken/+1-page-crossed cases), talking
   to memory only through a `CpuBus` read/write vtable so it has no
   idea yet whether it's driving a flat test-harness RAM array or the
   real bank-switched map (step 2, below) - that decoupling is what
   let it be verified in isolation. Decimal-mode ADC/SBC follow the
   documented NMOS quirk where N/V/Z are derived from different
   (partially uncorrected) intermediate values than the accumulator
   result itself, not just "do BCD arithmetic and set flags from that"
   - see `src/cpu.c`'s own header comment and `op_adc()`/`op_sbc()`.
   Passes Klaus Dormann's 6502 functional test suite (every legal
   opcode/addressing-mode combination) on the first full run, plus a
   hand-written interrupt/reset check (`tests/cpu/test_interrupts.c`)
   for cpu_reset()/BRK/IRQ/NMI/RTI, since Dormann's suite deliberately
   never triggers a real interrupt mid-test and so can't exercise that
   path at all. See `tests/cpu/README.md` for how to run both.
2. ~~**Memory map + bank switching**~~ - done (`src/memory.c`/
   `src/memory.h`): the 64K RAM array, ROM loading (`roms/README.md`),
   and the 6510 I/O port at `$00`/`$01` (LORAM/HIRAM/CHAREN) deciding
   whether BASIC ROM/KERNAL ROM/character ROM/I/O or plain RAM is
   visible at $A000-$BFFF/$D000-$DFFF/$E000-$FFFF - cross-checked
   against the full bank-switching mode table (only the 8 rows
   reachable with no cartridge present; cartridge support isn't
   planned yet). Two things worth knowing if this is ever touched
   again: BASIC ROM needs BOTH LORAM and HIRAM set, not LORAM alone
   (an easy detail to get wrong from memory rather than the actual
   table); and a write always lands in RAM even when ROM is what's
   visible for reads at that same address ("write under ROM") -
   `memory_write()` doesn't special-case the ROM regions at all
   because of this, it only special-cases $D000-$DFFF when I/O
   (rather than RAM or character ROM) is what's currently banked in
   there. VIC-II/SID/CIA don't exist yet, so $D000-$DFFF's I/O mode is
   currently just an inert placeholder (`IoBus`, unregistered) - see
   `src/memory.h`. See `tests/memory/README.md` for the regression
   coverage.
3. ~~**Minimal GTK4 shell**~~ - done (`gtk/main.c`, built via the new
   top-level `Makefile` -> `bin/c64emu`): a window driving the CPU
   through a `g_timeout_add` loop at ~50 Hz (PAL frame rate,
   `CYCLES_PER_FRAME` cycles of `cpu_step()` per tick - not
   cycle-exact, there's no raster to synchronize against yet). It
   shows screen RAM (`$0400`-`$07E7`, 1000 bytes) as a raw 40x25 grid
   of grayscale cells, one byte value per cell, deliberately NOT real
   VIC-II text-mode decoding (step 5 below) - the point was proving
   the core can be driven and displayed from a real GUI event loop,
   not producing a real picture yet. Missing ROMs aren't fatal:
   `memory_load_roms()` reports how many loaded (0-3) and the CPU just
   runs a harmless BRK loop on all-zero memory if none did, which is
   enough to exercise the display/event loop on its own. Keyboard
   press/release events are captured via `GtkEventControllerKey` and
   logged to stdout, not wired to anything yet - there's no keyboard
   matrix to feed until CIA (step 4) exists.
4. **CIA 1/2** - timers, keyboard matrix scanning, joystick, TOD clock.
   Needed before VIC-II mainly because BASIC/KERNAL boot-up already
   depends on CIA timer behavior.
5. **VIC-II, first pass** - text mode and a solid border color first;
   bitmap modes, sprites, and raster interrupts after that. "Bad
   lines" (the cycle-stealing DMA quirk a lot of real software's
   timing depends on) is its own explicitly-tracked sub-step, not
   assumed to fall out of the rest for free.
6. **SID** - the 3-voice synth (square/triangle/sawtooth/noise
   generators, ADSR envelopes) without exact analog filter modeling
   at first; revisit filter accuracy later if it matters for a
   specific piece of software being tested against.

## Not yet scheduled

- **1541 disk drive emulation** - the 1541 has its own 6502 core plus
  a serial IEC bus protocol, so true hardware emulation is close to a
  second emulator. Loading a `.prg` by injecting it directly into
  memory (the same shortcut `mini6502.py` already takes) covers the
  common case without this.
- **Cycle-exact VIC-II/SID fidelity** - "good enough for most
  games/demos" (step 5/6 above) versus genuinely cycle-exact,
  demoscene-compatible timing is a large gap; not worth chasing until
  there's a concrete piece of software that needs it.
- **NTSC timing** - PAL timing only at first (matches this toolkit's
  existing PETSCII/hardware assumptions elsewhere).
- A disassembler/debugger view in the GTK front end (register/memory
  inspection, breakpoints, step execution) - useful once there's
  real software to debug against, not before.

## Open questions

- Where do `roms/`'s expected ROM images come from for someone
  building this fresh? `roms/README.md` covers the legal answer
  (dump your own); worth revisiting once step 2 needs concrete
  filenames/sizes to load.
