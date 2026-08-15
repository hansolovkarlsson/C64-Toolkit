/*
 * bounce_demo.c - a real, visually-and-audibly-verifiable cc64 program
 * combining lib/graphics.h and lib/sound.h: one hardware sprite bounces
 * around inside the visible screen, playing a short SID "bonk" each
 * time it hits an edge.
 *
 * Deliberately the same shape and sound this project's own
 * asm/examples/bounce.asm already uses (a 21-row filled-circle "ball",
 * and the exact wall-bounce sound effect pong.asm/bounce.asm both
 * proved out - freq_hi $18, attack 0, decay 6, sustain 0, release 0,
 * noise waveform) - not a coincidence: this is the same well-
 * understood behavior, written from scratch in cc64 against
 * lib/graphics.h/lib/sound.h instead of asm/lib/graphics.inc/
 * sound.inc's macros. See the root ROADMAP.md's "Recently done" for
 * why cc64's library doesn't wrap asm/lib/ - this demo is the first
 * real program putting that library to use together.
 *
 * The bounce BOUNDS are NOT bounce.asm's own XMIN/XMAX/YMIN/YMAX
 * (24/320/50/229) - those are the well-known real-hardware landmark
 * for sprite X/Y=24/50 landing at the PLAYFIELD's own top-left corner,
 * correct for real hardware/VICE, but c64emu's own VIC-II model uses a
 * DIFFERENT convention: VIC_SPRITE_X_OFFSET/Y_OFFSET (emu/src/vic.c)
 * map X/Y=24/50 to the CANVAS's top-left corner instead - i.e. the
 * outer edge of the rendered border, not the playfield's inner edge -
 * confirmed deliberate and tested (emu/tests/vic/test_vic.c's own
 * sprite-position test comments this exact mapping). Using
 * bounce.asm's real-hardware bounds under c64emu genuinely puts the
 * sprite in the border, off the true playfield, by exactly the border
 * width (VIC_BORDER_X/Y = 32/35 canvas pixels) - caught by actually
 * running this program in the GTK window and watching it, not by
 * either of this program's own automated checks (mini6502.py has no
 * real rendering; the c64emu spot-check before this fix only checked
 * "does it crash", not "does it look right"). The bounds below add
 * that border-width offset back in, so the sprite bounces flush
 * against the true rendered playfield edges under c64emu specifically:
 * XMIN/YMIN = 24+32/50+35 = 56/85 (playfield's own top-left, in c64emu's
 * canvas-relative sprite coordinates), XMAX/YMAX = 352/264 (playfield's
 * bottom-right minus the sprite's own 24x21 size, so its far edge lands
 * exactly on the playfield boundary rather than running past it).
 *
 * Two things graphics.h doesn't wrap yet get poked/peeked directly,
 * the same primitive graphics_demo.c used for everything before
 * graphics.h existed: $CC (KERNAL cursor-blink flag - see
 * asm/lib/graphics.inc's DISABLE_CURSOR for why this matters for any
 * program that takes over the screen) and $D012 (raster line, for
 * frame sync).
 *
 * Frame sync does NOT use asm/lib/graphics.inc's own wait_frame
 * technique (busy-wait for an EXACT raster line) - that assumes a
 * tight few-cycle-per-iteration native loop, reliably landing within a
 * single raster line's ~63-cycle-wide window every revolution. cc64's
 * compiled equivalent of `peek(x) != N` is far more expensive (a
 * generic runtime comparison routine, several JSRs deep - see this
 * program's own .lst if curious), so an exact-match busy-wait
 * frequently overshoots line 251 entirely and only catches it every
 * several real frames instead of every one - caught by actually
 * watching this run: it was extremely slow and jerky, not a steady
 * ~50Hz bounce. Fixed with an edge-triggered THRESHOLD check instead
 * (see frame_fired below), whose correctness doesn't depend on how
 * expensive a single check is - only exact-match busy-waits do.
 *
 * Like every game/demo in this project, this loops forever - there's
 * no way to quit back to BASIC. See README.md's "Testing" for why this
 * isn't run through mini6502.py's clean-return check the way the
 * language-feature tests are.
 *
 * Both fixes above were verified with real cycle-accurate timing (a
 * throwaway harness driving machine_step() directly and dumping a
 * rendered frame + register state at chosen cycle counts, the same
 * technique emu/tests/boot/'s own end-to-end gate uses) rather than
 * mini6502.py's plain instruction stepping, since both bugs were
 * specifically about real timing/rendering that a non-cycle-accurate
 * check can't see: confirmed the sprite renders inside the playfield,
 * bounces exactly at y=85 (reversing direction) with the SID gate
 * register firing at that same moment, and moves at ~1 pixel per
 * ~19656 cycles (one real PAL frame) - matching the intended pacing,
 * not the several-real-frames-per-pixel crawl from before the fix.
 *
 * One more thing this surfaced, not yet addressed: clear_screen()
 * alone (lib/graphics.h) takes roughly 800,000+ cycles here - about 16
 * real seconds - before the ball first appears, since each of its
 * 2000 poke() calls carries real per-call overhead under cc64's
 * current codegen (no optimization pass exists yet - see
 * C/ROADMAP.md's "Tooling ideas"). Separate from the two bugs above
 * (which were about ongoing behavior, not one-time startup cost); left
 * alone here since fixing it would mean changing shared, already-
 * tested library code, not just this program.
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
    int raster;
    int frame_fired;

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
    x = 56;
    y = 85;
    xdir = 1;
    ydir = 1;
    sprite_pos(0, x, y);
    sprite_enable(0, 1);

    sid_init();

    /* frame_fired latches so the bounce-step body below runs exactly
     * once per raster revolution: it fires the instant raster crosses
     * INTO the >=251 zone, then stays latched (no-op) for the rest of
     * that ~61-line-wide zone, and only resets once raster has dropped
     * back below 251 again (i.e. wrapped around for the next frame).
     * See this file's own header comment for why this replaces a
     * simple exact-match busy-wait. */
    frame_fired = 0;
    while (1) {
        raster = peek(53266);
        if (raster >= 251) {
            if (!frame_fired) {
                frame_fired = 1;

                bounced = 0;

                if (xdir) {
                    x = x + 1;
                    if (x >= 352) {
                        xdir = 0;
                        bounced = 1;
                    }
                } else {
                    x = x - 1;
                    if (x <= 56) {
                        xdir = 1;
                        bounced = 1;
                    }
                }

                if (ydir) {
                    y = y + 1;
                    if (y >= 264) {
                        ydir = 0;
                        bounced = 1;
                    }
                } else {
                    y = y - 1;
                    if (y <= 85) {
                        ydir = 1;
                        bounced = 1;
                    }
                }

                sprite_pos(0, x, y);

                if (bounced) {
                    sid_play(0, 6144, 128, 0, 6, 0, 0);
                }
            }
        } else {
            frame_fired = 0;
        }
    }
}
