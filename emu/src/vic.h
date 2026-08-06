#ifndef C64EMU_VIC_H
#define C64EMU_VIC_H

#include <stdint.h>
#include "memory.h"

/* VIC-II (../ROADMAP.md steps 5-6): a free-running raster line
 * counter, standard 40x25 hi-res text mode, a solid border/background
 * color, raster IRQs, and bad lines (the cycle-stealing DMA quirk a
 * lot of real software's raster-timed effects depend on - see
 * vic_take_badline_stall()'s header comment for exactly what's
 * modeled and what isn't). Rendering still happens once per whole
 * frame, not scanline-by-scanline, so there's no notion of mid-frame
 * raster EFFECTS yet even though the underlying timing (raster IRQs,
 * bad-line CPU stalls) is now real. Deliberately not implemented yet:
 * multicolor/extended-background-color text modes, bitmap modes,
 * sprites, and light pen.
 *
 * PAL timing: 63 cycles/line, 312 lines/frame (see PAL_CYCLES_PER_LINE/
 * PAL_LINES_PER_FRAME) - matches gtk/main.c's own PAL frame-cycle
 * budget.
 */
typedef struct {
    uint8_t regs[64];        /* $D000-$D03F, mirrored across all of $D000-$D3FF - see vic.c for which of these have real behavior vs. passive storage. $D019 (bits0-3 only) IS the live IRQ-pending byte, $D01A (bits0-3 only) IS the live IRQ-enable byte - see vic_write()'s special-case for $D019's write-1-to-clear semantics. */
    uint8_t color_ram[1024]; /* $D800-$DBFF when I/O is banked in - a real, physically separate 4-bit-wide static RAM chip, always present regardless of VIC bank (unlike screen/char memory, which follow the VIC bank). Low nibble meaningful - see vic_color_ram_read(). */

    uint32_t raster_cycle;  /* 0..(PAL_CYCLES_PER_LINE*PAL_LINES_PER_FRAME - 1), advanced by vic_tick() */
    uint16_t raster_compare; /* 0-311: the raster line $D011 bit7 + $D012 last had written to them - vic_tick() sets $D019 bit0 pending the instant the live raster line reaches this value */
    uint32_t badline_stall_cycles; /* >0 right after entering a bad line, until vic_take_badline_stall() consumes it - see that function's header comment */
} Vic;

#define PAL_CYCLES_PER_LINE 63
#define PAL_LINES_PER_FRAME 312

/* The standard, widely-cited figure (c64-wiki, VICE, Christian Bauer's
 * VIC-II article) for how many PHI2 cycles a bad line steals from the
 * CPU - not independently re-derived or cycle-verified against real
 * hardware by this project. */
#define VIC_BADLINE_STALL_CYCLES 40

#define VIC_TEXT_COLS 40
#define VIC_TEXT_ROWS 25
/* Border thickness is a plausible-looking approximation, not real
 * border timing (real border geometry depends on raster position and
 * the RSEL/CSEL bits - not modeled, see this file's header comment). */
#define VIC_BORDER_X 32
#define VIC_BORDER_Y 35
#define VIC_CANVAS_W (VIC_TEXT_COLS * 8 + VIC_BORDER_X * 2)
#define VIC_CANVAS_H (VIC_TEXT_ROWS * 8 + VIC_BORDER_Y * 2)

void vic_init(Vic *vic);

/* addr must already be reduced to a register number (addr & 0x3F) -
 * machine.c owns deciding which physical address range maps here.
 *
 * $D011/$D012 are real VIC-II shared registers: READING them returns
 * the LIVE raster position (MSB/low byte respectively); WRITING them
 * sets `raster_compare` instead (same bit split) - real hardware reuses
 * the same two register addresses for both purposes, it isn't a bug or
 * an approximation. $D019 (bits 0-3 pending, bit7 read-only summary)
 * and $D01A (bits 0-3 enable) are real too - see vic_irq_line().
 * Writing to $D019 CLEARS whichever of bits 0-3 are 1 in the value
 * written (matching real hardware's write-1-to-clear semantics), it
 * does not assign the byte directly.
 *
 * Every other register just reads back what was last written. */
uint8_t vic_read(Vic *vic, uint8_t reg);
void vic_write(Vic *vic, uint8_t reg, uint8_t v);

/* True exactly when this chip is currently requesting an interrupt:
 * $D019 & $D01A & 0x0F (bits 0-3) != 0. Unlike CIA's ICR, reading
 * $D019 does NOT clear pending bits - only an explicit write-1 to
 * $D019 does - so, also unlike cia_irq_line(), this can safely be
 * called every machine_step() without side effects. Wired to the
 * CPU's /IRQ line, the same (shared, wired-OR) line CIA1 uses - see
 * machine_step(). */
int vic_irq_line(const Vic *vic);

/* addr must already be reduced to 0-1023. SIMPLIFICATION: the real
 * chip is 4 bits wide - only the low nibble is meaningful storage: a
 * write only stores bits 0-3, and a read ORs the stored nibble with
 * 0xF0 (matching memory.c's own "an unconnected input bit reads as 1"
 * convention for the 6510 I/O port, rather than modeling the real
 * chip's actual floating-bus noise on the unused upper nibble). */
uint8_t vic_color_ram_read(Vic *vic, uint16_t addr);
void vic_color_ram_write(Vic *vic, uint16_t addr, uint8_t v);

/* Advances the free-running raster counter by `cycles` PHI2 ticks -
 * call once per machine_step(), the same pattern as cia_tick(), so
 * $D011/$D012 reflect a real, continuously-changing value instead of
 * an inert placeholder (this is specifically what real KERNAL/BASIC
 * boot code polls to detect the passage of time before CIA/VIC
 * interrupts are even set up - see emu/ROADMAP.md's step 5 entry).
 * `cycles` must be small relative to PAL_CYCLES_PER_LINE (in practice
 * always one CPU instruction's worth, 2-7, or one bad-line stall's
 * worth, VIC_BADLINE_STALL_CYCLES - both comfortably under 63) - the
 * raster-IRQ and bad-line detection below only checks for crossing
 * ONE line boundary per call, not several. */
void vic_tick(Vic *vic, int cycles);

/* Returns the pending bad-line stall amount (0 if none) and clears it
 * - call this FIRST, before cpu_step(), once per machine_step(). If it
 * returns nonzero, skip calling cpu_step() entirely for that
 * machine_step() call and tick the CIAs/VIC by the returned amount
 * instead - the CPU makes zero progress, simulating a real bad line's
 * /RDY stall (the VIC-II halting the CPU to fetch a whole line's
 * worth of screen/color RAM data at once).
 *
 * Set internally by vic_tick() the instant a bad line is entered: real
 * VIC-II hardware asserts /RDY whenever the raster line's low 3 bits
 * match YSCROLL ($D011 bits 0-2), the line is within $30-$F7, and DEN
 * is set - this is genuine VIC-II chip behavior, not a C64-specific
 * wiring quirk (contrast with machine.c's keyboard-matrix handling),
 * which is why it lives here rather than in machine.c.
 *
 * NOT modeled: the exact real timing of when within the line the
 * stall starts (real hardware: partway through, around cycle 12-17)
 * or DEN's real per-line latch behavior (real hardware samples DEN at
 * a specific cycle each line, not continuously) - this checks DEN's
 * current value at the instant the line is detected as entered
 * instead. The whole stall is applied in one lump at that instant,
 * delaying whatever instruction would have executed next by up to
 * VIC_BADLINE_STALL_CYCLES rather than truly interrupting the CPU
 * mid-instruction - cpu.c executes whole instructions atomically and
 * has no mechanism to pause mid-flight, so this is the closest
 * approximation available without rearchitecting it into a true
 * cycle-stepped core. */
uint32_t vic_take_badline_stall(Vic *vic);

/* Renders one whole frame into `pixels` (0x00RRGGBB per pixel,
 * VIC_CANVAS_W x VIC_CANVAS_H, top-left origin, row stride given in
 * PIXELS by `stride` so a caller can wrap a cairo (or similar) surface
 * with its own alignment padding without a reformatting pass).
 *
 * `mem` is read directly (mem->ram / mem->char_rom), NOT through
 * memory_read() - the VIC has its own, separate view of memory that
 * ignores the CPU's $A000-$DFFF ROM bank-switching entirely (it never
 * sees BASIC/KERNAL ROM or I/O, only plain RAM - see memory.c). The
 * one exception is the character ROM special case: real C64 wiring
 * makes the character ROM visible to the VIC (never the CPU) whenever
 * its selected character memory pointer lands on offset $1000-$1FFF
 * *within* VIC bank 0 or bank 2 specifically - not banks 1 or 3, since
 * the ROM chip's own select line is physically wired to just those two
 * banks' address decoding. Implemented exactly, not approximated.
 *
 * `bank` (0-3) is the VIC's currently selected 16K RAM bank - resolved
 * from CIA2 PRA bits 0-1 by the caller (machine.c), not read from CIA2
 * here, so this file stays free of any C64-specific-wiring knowledge
 * beyond the VIC chip itself (mirrors how cia.c doesn't know about the
 * keyboard matrix either - that's machine.c's job in both cases). */
void vic_render_frame(Vic *vic, const Memory *mem, uint8_t bank, uint32_t *pixels, int stride);

#endif
