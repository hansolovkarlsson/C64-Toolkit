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
4. ~~**CIA 1/2**~~ - done: a chip-generic 6526 model (`src/cia.c`/
   `src/cia.h` - PRA/PRB with real per-bit DDR output-latch-vs-
   external-pin read semantics, 16-bit Timer A/B with continuous/
   one-shot modes and Timer B able to cascade off Timer A's underflows,
   the ICR mask/pending/read-clears interrupt path) plus the
   C64-specific wiring on top of it (`src/machine.c`/`src/machine.h` -
   new, ties CPU+Memory+CIA1+CIA2 together, replacing the ad hoc
   wiring `gtk/main.c` used to do inline): CIA1's keyboard-matrix/
   joystick-2-sharing-PRA/joystick-1-sharing-PRB pin-pulldown model,
   CIA1's interrupt output reaching `cpu.irq_line`, and CIA2's
   reaching `cpu_nmi()` (edge-triggered, matching real wiring - CIA2's
   IRQ output goes to the CPU's /NMI pin, not /IRQ). `gtk/main.c`'s
   keyboard capture (step 3) is now actually wired to something: GDK
   key events map to C64 keyboard-matrix positions and feed
   `machine_set_key()`, so typing in the window reaches BASIC/KERNAL
   once real ROM images are present. TOD clock and the serial data
   register are both modeled as passive storage only (no real
   ticking/shifting) - documented as a known simplification in
   `cia.h`, revisit only if something concrete needs either. See
   `tests/cia/README.md` and `tests/machine/README.md` for the
   regression coverage (there's no third-party CIA test suite the way
   step 1 has Klaus Dormann's, so all of it is hand-derived from the
   published 6526 register semantics).
5. ~~**VIC-II, first pass**~~ - done (`src/vic.c`/`src/vic.h`): a
   free-running raster line counter (63 cycles/line, 312 lines/frame,
   PAL - `vic_tick()`, called from `machine_step()` the same way
   `cia_tick()` is, so `$D011`/`$D012` reflect a real, continuously
   advancing value rather than an inert placeholder), standard 40x25
   hi-res text mode (screen RAM + character ROM/RAM + color RAM,
   rendered through whichever VIC bank CIA2 currently selects - see
   `machine_vic_bank()`), and a solid border/background color
   (`$D020`/`$D021`) plus DEN (`$D011` bit 4) blanking the display to
   the border color when clear. Implements the real hardware quirk
   where the character ROM is only visible to the VIC (never the CPU)
   in banks 0 and 2 specifically, not 1 or 3, when the character
   pointer selects offset `$1000`/`$1800` - see `vic_render_frame()`'s
   header comment. Rendering happens once per whole frame, not
   scanline-by-scanline, so there's no bad-lines modeling yet and no
   mid-frame raster effects are possible - that's genuinely deferred,
   along with raster IRQs (`$D011`/`$D012` writes don't set up a
   compare target yet, `$D019`/`$D01A` are passive storage only),
   multicolor/extended-background-color text modes, bitmap modes,
   sprites, and light pen - all explicitly out of scope for "first
   pass," see `vic.h`'s header comment. Verified against hand-derived
   expectations (`tests/vic/`) AND, for the first time, against real
   third-party C64 system software rather than just this project's own
   test fixtures: `tests/boot/` fetches a real open-source ROM
   replacement (the MEGA65 `open-roms` project - GPL-3.0/LGPL-3.0,
   unencumbered by design, safe to auto-fetch unlike Commodore's own
   ROMs) and checks the whole machine boots it unmodified to a readable
   BASIC `READY.` prompt - a genuine end-to-end integration gate, not
   just another module-level unit test. "Bad lines" (the cycle-stealing
   DMA quirk a lot of real software's timing depends on) remains its
   own explicitly-tracked follow-up, not assumed to fall out of a
   future pass for free.
6. **VIC-II, second pass** - in progress. Everything step 5 explicitly
   deferred:
   - ~~Raster IRQs~~ - done: `$D011`/`$D012` are real shared registers
     now (reading returns the live raster position, same as before;
     writing sets `raster_compare` instead - not a bug, real hardware
     reuses the same two addresses for both). `$D019`'s bits 0-3 are a
     real write-1-to-clear pending byte, bit 7 a read-only summary of
     `pending & $D01A`; `vic_irq_line()` feeds `cpu.irq_line` alongside
     CIA1's (real hardware wired-OR - see `machine_step()`). Caught two
     real bugs via the new tests before this was even committed:
     `machine_init()` wasn't zeroing `Machine`'s embedded `Cpu6502` at
     all (nothing was - `cpu_reset()` only ever touched
     sp/p/pc/nmi_pending), so `cpu.irq_line` started as uninitialized
     stack garbage until the first `machine_step()` call happened to
     overwrite it; and `vic_read()`'s `$D019` bit 7 summary was
     documented but never actually implemented. Both fixed. Still not
     modeled: bad lines and real per-instruction raster-effect timing
     (rendering is still once-per-frame, not scanline-by-scanline, so
     an IRQ handler that pokes `$D020`/`$D021` mid-frame for a raster
     bar won't show up in the picture yet even though the IRQ itself
     now fires at the right line).
   - ~~Bad lines~~ - done: real VIC-II hardware asserts `/RDY` (freezing
     the CPU) whenever the raster line's low 3 bits match YSCROLL
     (`$D011` bits 0-2), the line is within `$30`-`$F7`, and DEN is set
     - `vic_tick()` detects this the instant such a line is entered and
     sets a pending stall of `VIC_BADLINE_STALL_CYCLES` (40, the
     standard widely-cited figure - not independently cycle-verified
     against real hardware). Since `cpu.c` executes whole instructions
     atomically with no way to interrupt one mid-flight, the stall is
     applied as its own dedicated `machine_step()` call right when
     entered - `vic_take_badline_stall()` returns the pending amount
     and clears it, and if nonzero, that step only ticks the CIAs/VIC
     by that many cycles and skips `cpu_step()` entirely, so the CPU
     makes zero progress that call (matching a real `/RDY` stall) - see
     `machine_step()`. Not modeled: the exact real timing of when
     within the line the stall starts (real hardware: partway through,
     around cycle 12-17; here: applied in one lump at the instant of
     line-entry) or DEN's real per-line latch behavior (checked as a
     plain current-value read instead). Verified against hand-derived
     expectations, including that DEN=0 and lines outside the window
     correctly suppress it (`tests/vic/`), and that the CPU's PC/cycle
     count genuinely don't advance during a stall call (`tests/machine/`).
   - ~~Multicolor text mode~~ - done: MCM (`$D016` bit4) turns text mode
     "mixed" per real hardware behavior - color RAM's bit3 becomes a
     PER-CELL mode flag instead of part of the color. A cell with
     bit3=0 still renders plain hi-res, masked to color RAM bits 0-2
     (colors 0-7 only - bit3 no longer means what it does with MCM
     off); a cell with bit3=1 renders as true 4-color multicolor
     instead (background color 0/1/2 - `$D021`/`$D022`/`$D023`,
     the latter two new registers - or color RAM bits 0-2 as the 4th
     color), each of the 4 possible 2-bit pixel-pair values covering 2
     real pixels instead of 1, so multicolor cells render at half the
     horizontal resolution of hi-res ones. Verified against hand-derived
     expectations for all 4 pixel-pair values and for the bit3=0
     fallback genuinely taking the hi-res code path, not just happening
     to produce the same pixels (`tests/vic/`).
   - ~~Bitmap modes~~ - done: BMM (`$D011` bit5) switches from text mode
     to bitmap mode entirely - a different memory layout, not a variant
     of text mode's. Screen RAM holds per-CELL color info directly
     instead of a character code (no character-code indirection, and no
     character ROM involved at all - that's text-mode-only); `$D018`'s
     char/bitmap-pointer field only uses its bit3 in bitmap mode (bits
     1-2 are ignored) to pick the bitmap data's `$0000`/`$2000` offset
     within the VIC bank. Standard bitmap (MCM=0): each cell's own
     screen-RAM byte holds its 2 colors directly (upper nibble for set
     bits, lower for clear) - color RAM isn't used at all. Multicolor
     bitmap (MCM=1): each 2-bit pixel-pair picks one of 4 colors -
     `$D021`, screen RAM's upper nibble, screen RAM's lower nibble, or
     color RAM's low nibble - a different color-source list than
     multicolor TEXT mode's (which uses `$D022`/`$D023`, not screen
     RAM, and is otherwise unrelated even though both are "multicolor").
     Refactored the shared hi-res/multicolor pixel-writing logic (which
     text and bitmap modes both use identically) into one `render_cell()`
     helper in `vic.c`, rather than duplicating it a second time.
     Verified against hand-derived expectations for both bitmap
     sub-modes, using a real per-cell byte pattern for the standard
     case and all 4 pixel-pair values for the multicolor case
     (`tests/vic/`).
   - Extended-background-color text mode, sprites, and light pen - not
     started. Also where real border geometry (currently a fixed,
     plausible-looking approximation - see `vic.h`) and scanline-by-
     scanline rendering (instead of one whole frame per
     `vic_render_frame()` call) would need to land, if either turns out
     to matter for real software being tested against - bad lines now
     stall the CPU correctly, but a raster IRQ handler that pokes
     `$D020`/`$D021` mid-frame for a split-screen effect still won't
     show up in the picture, since rendering itself is still
     once-per-frame.
7. **SID** - the 3-voice synth (square/triangle/sawtooth/noise
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
