/*
 * bounce_demo.c - a real, visually-and-audibly-verifiable cc64 program
 * combining lib/graphics.h and lib/sound.h: one hardware sprite bounces
 * around inside the visible screen, playing a short SID "bonk" each
 * time it hits an edge.
 *
 * Deliberately the same shape, bounds, and sound this project's own
 * asm/examples/bounce.asm already uses (a 21-row filled-circle "ball",
 * XMIN/XMAX/YMIN/YMAX = 24/320/50/229, and the exact wall-bounce sound
 * effect pong.asm/bounce.asm both proved out - freq_hi $18, attack 0,
 * decay 6, sustain 0, release 0, noise waveform) - not a coincidence:
 * this is the same well-understood behavior, written from scratch in
 * cc64 against lib/graphics.h/lib/sound.h instead of
 * asm/lib/graphics.inc/sound.inc's macros. See the root ROADMAP.md's
 * "Recently done" for why cc64's library doesn't wrap asm/lib/ - this
 * demo is the first real program putting that library to use together.
 *
 * Two things graphics.h doesn't wrap yet get poked/peeked directly,
 * the same primitive graphics_demo.c used for everything before
 * graphics.h existed: $CC (KERNAL cursor-blink flag - see
 * asm/lib/graphics.inc's DISABLE_CURSOR for why this matters for any
 * program that takes over the screen) and $D012 (raster line, for
 * frame sync - see asm/lib/graphics.inc's wait_frame).
 *
 * Like every game/demo in this project, this loops forever - there's
 * no way to quit back to BASIC. See README.md's "Testing" for why this
 * isn't run through mini6502.py's clean-return check the way the
 * language-feature tests are.
 */

#include <graphics.h>
#include <sound.h>

void main(void) {
    int i;
    int rowval;
    int base;
    int x;
    int y;
    int xdir;
    int ydir;
    int bounced;

    /* $3E00 - NOT the "$0d00 or $0e00" scratch address
     * asm/lib/graphics.inc's own comment recommends. Two constraints,
     * both real:
     *
     * 1. sprite_pointer() (graphics.h) writes a single byte, an offset
     *    in 64-byte units - so the sprite data must fall within the
     *    VIC-II's CURRENT 16K bank ($0000-$3FFF here, the default bank
     *    every demo in this project leaves unconfigured), not just
     *    anywhere in the CPU's full 64K address space. An earlier
     *    version of this program used $6000 - comfortably clear of
     *    everything else, but outside bank 0 entirely, so the
     *    truncated pointer byte silently pointed at the wrong 64-byte
     *    block within bank 0 instead (sprite_ptr = 24576 / 64 = 384,
     *    which doesn't fit in a byte - poke() truncates to 384 & 255).
     * 2. Within that bank, the asm convention's own "$0d00 or $0e00"
     *    assumes a short hand-assembled program - cc64's compiled
     *    output (this program's own runtime library alone) already
     *    runs from $0801 to roughly $3B22, comfortably past $0e00, so
     *    anything placed there overwrites the running program's own
     *    code instead of sitting in free RAM. That's what actually
     *    happened first: an earlier version of this program (still on
     *    $0e00, before the bank-0 issue above was even found) crashed
     *    almost immediately - BRK at $0E14, a poked zero byte read
     *    back as a BRK opcode - traced with a manual step-by-step run
     *    that confirmed it wasn't a library bug.
     *
     * $3E00 clears both: within bank 0, well past this program's own
     * ~$3B22 end, and clear of the character ROM shadow ($1000-$1FFF). */
    base = 15872;

    /* Build the sprite shape at runtime: cc64 has no way to embed
     * initialized constant array data (global array initializers
     * aren't supported - see cc64-reference.md #4.3), so this pokes
     * each byte directly instead, the same way every other hardware
     * fact in this program gets set up. Byte-for-byte the same 21-row
     * filled circle asm/examples/bounce.asm's own sprite_data uses. */
    for (i = 0; i < 21; i++) {
        switch (i) {
            case 0: case 19: rowval = 60; break;
            case 1: case 18: rowval = 126; break;
            case 20: rowval = 0; break;
            default: rowval = 255; break;
        }
        poke(base + i * 3, rowval);
        poke(base + i * 3 + 1, 0);
        poke(base + i * 3 + 2, 0);
    }

    poke(204, 1); /* disable the KERNAL's blinking text cursor */

    border_color(6);      /* blue, matching bounce.asm's own choice */
    background_color(0);
    clear_screen(0);

    sprite_pointer(0, base / 64);
    sprite_color(0, 1); /* white */
    x = 24;
    y = 50;
    xdir = 1;
    ydir = 1;
    sprite_pos(0, x, y);
    sprite_enable(0, 1);

    sid_init();

    while (1) {
        /* Busy-wait for a raster line near the bottom of the visible
         * display, syncing the loop to the screen's ~50Hz refresh rate
         * - the same polling target asm/lib/graphics.inc's wait_frame
         * uses. */
        while (peek(53266) != 251) {
        }

        bounced = 0;

        if (xdir) {
            x = x + 1;
            if (x >= 320) {
                xdir = 0;
                bounced = 1;
            }
        } else {
            x = x - 1;
            if (x <= 24) {
                xdir = 1;
                bounced = 1;
            }
        }

        if (ydir) {
            y = y + 1;
            if (y >= 229) {
                ydir = 0;
                bounced = 1;
            }
        } else {
            y = y - 1;
            if (y <= 50) {
                ydir = 1;
                bounced = 1;
            }
        }

        sprite_pos(0, x, y);

        if (bounced) {
            sid_play(0, 6144, 128, 0, 6, 0, 0);
        }
    }
}
