#ifndef C64EMU_VIC_H
#define C64EMU_VIC_H

#include <stdint.h>
#include "memory.h"

/* VIC-II, first pass (../ROADMAP.md step 5): a free-running raster
 * line counter, standard 40x25 hi-res text mode, and a solid border/
 * background color. Deliberately NOT implemented yet, each explicitly
 * left for a later pass per the roadmap: raster compare/IRQs (reads
 * of $D011/$D012 already reflect the live raster position - real
 * software can poll it right now - but writes to them don't yet set
 * up a compare target, and $D019/$D01A are passive storage only, no
 * IRQ wiring into the CPU), multicolor/extended-background-color text
 * modes, bitmap modes, sprites, and light pen. "Bad lines" (the
 * cycle-stealing DMA quirk a lot of real software's timing depends
 * on) isn't modeled either - rendering happens once per whole frame,
 * not scanline-by-scanline, so there's no notion of mid-frame raster
 * effects at all yet.
 *
 * PAL timing: 63 cycles/line, 312 lines/frame (see PAL_CYCLES_PER_LINE/
 * PAL_LINES_PER_FRAME) - matches gtk/main.c's own PAL frame-cycle
 * budget.
 */
typedef struct {
    uint8_t regs[64];        /* $D000-$D03F, mirrored across all of $D000-$D3FF - see vic.c for which of these have real behavior vs. passive storage */
    uint8_t color_ram[1024]; /* $D800-$DBFF when I/O is banked in - a real, physically separate 4-bit-wide static RAM chip, always present regardless of VIC bank (unlike screen/char memory, which follow the VIC bank). Low nibble meaningful - see vic_color_ram_read(). */

    uint32_t raster_cycle; /* 0..(PAL_CYCLES_PER_LINE*PAL_LINES_PER_FRAME - 1), advanced by vic_tick() */
} Vic;

#define PAL_CYCLES_PER_LINE 63
#define PAL_LINES_PER_FRAME 312

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
 * Reading $D011/$D012 returns the LIVE raster position (MSB/low byte
 * respectively), not whatever was last written to them - see this
 * file's header comment on why writes to those two don't do anything
 * yet. Every other register just reads back what was last written. */
uint8_t vic_read(Vic *vic, uint8_t reg);
void vic_write(Vic *vic, uint8_t reg, uint8_t v);

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
 * interrupts are even set up - see emu/ROADMAP.md's step 5 entry). */
void vic_tick(Vic *vic, int cycles);

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
