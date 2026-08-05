# `c64emu` roadmap

Nothing here is implemented yet - this is the staged build order,
easiest/best-understood pieces first, each one a correctness
foundation for the next. See `README.md`'s "Planned layout" for where
each piece will live.

## Build order

1. **6502/6510 CPU core** - the full instruction set (not just the
   subset `cc64` emits - see `README.md`'s "Why this exists"), with a
   per-instruction cycle-count table from the start rather than bolted
   on later, since VIC-II/CIA timing later on depends on the CPU
   already being cycle-stepped, not just instruction-stepped.
   **Correctness gate:** Klaus Dormann's 6502 functional test suite
   (a well-known, widely-used validation ROM that exercises every
   legal opcode/addressing-mode combination and traps on failure) -
   nothing else starts until this passes.
2. **Memory map + bank switching** - the 64K address space, and the
   6510 I/O port at `$01` that controls whether BASIC ROM/KERNAL ROM/
   character ROM or plain RAM is visible in their overlapping address
   ranges.
3. **Minimal GTK4 shell** - a window showing the raw framebuffer and
   nothing else yet (no VIC-II text/graphics modes rendered - just
   proving the CPU+memory core can be driven and displayed from a real
   GUI loop before chip emulation adds more moving parts). Keyboard
   input wiring starts here too, ahead of CIA, so there's something to
   type into once BASIC/KERNAL ROMs are loadable.
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
