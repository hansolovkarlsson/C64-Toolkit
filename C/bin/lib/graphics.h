/*
 * graphics.h - VIC-II text-screen and hardware-sprite helpers, built
 * entirely on peek()/poke() against the same absolute addresses a
 * hand-written asm program would hit directly (see ../examples/
 * graphics_demo.c, the program this library was extracted from).
 *
 * This is independent of asm/lib/graphics.inc, not a wrapper around
 * it - cc64 has no inline-assembly/foreign-function-call mechanism to
 * JSR into external assembly at all, and asm/lib/'s macros use a
 * raw-register calling convention (plus caller-defined zero-page
 * pointers) that doesn't match cc64's own per-function frame-save/
 * restore convention anyway. See the root ROADMAP.md's "Recently
 * done" for the full reasoning. Where useful, this still reuses the
 * same hardware facts (register addresses, bit layouts) that
 * asm/lib/graphics.inc's own comments document.
 *
 * HEADER-ONLY, like string.h/print.h: every function you #include gets
 * fully compiled into your program whether you call it or not.
 *
 * Assumes the VIC-II's default bank and memory pointers: screen RAM at
 * $0400, character data from the character ROM shadow - i.e. plain
 * text mode with nothing in $D018 touched. A program that reconfigures
 * $D018 itself (bitmap mode, a different VIC bank) should not expect
 * plot_char()/clear_screen() to still target the right addresses.
 */

/* $D020 - the color of the border around the visible screen. 0-15. */
void border_color(int c) {
    poke(53280, c);
}

/* $D021 - the background color showing through everywhere a text cell
 * doesn't set its own foreground pixel. 0-15. */
void background_color(int c) {
    poke(53281, c);
}

/* Writes one screen code + color directly into screen RAM ($0400) and
 * color RAM ($D800) at column x (0-39), row y (0-24) - bypassing
 * KERNAL CHROUT's cursor entirely, so this can place a character
 * anywhere without disturbing where putchar()/puts() would print
 * next. screen_code is a raw C64 screen code, NOT a PETSCII/ASCII
 * byte - putchar() converts letters/digits for you, this doesn't (see
 * the README's "PETSCII and case" section for the distinction; a
 * space is screen code 32 either way, and $A0/160 is the solid
 * reversed-space block graphics_demo.c uses to draw with). No bounds
 * checking - an out-of-range x/y silently pokes the wrong cell (or,
 * past row 24, spills into the sprite-pointer bytes at the very end of
 * color/screen RAM), exactly like every other peek()/poke() call in
 * cc64. */
void plot_char(int x, int y, int screen_code, int color) {
    int offset;
    offset = y * 40 + x;
    poke(1024 + offset, screen_code);
    poke(55296 + offset, color);
}

/* Fills the whole 40x25 visible screen with screen code 32 (space) and
 * color RAM with `color` - the direct-memory equivalent of CLS, except
 * it also sets every cell's color in one pass, and (like plot_char())
 * doesn't move or reset the KERNAL's own cursor position. */
void clear_screen(int color) {
    int i;
    for (i = 0; i < 1000; i++) {
        poke(1024 + i, 32);
        poke(55296 + i, color);
    }
}

/* Turns hardware sprite n (0-7) on or off via $D015 - the master
 * switch each sprite needs regardless of its position/color/pointer
 * being set correctly. */
void sprite_enable(int n, int on) {
    int mask;
    int v;
    mask = 1 << n;
    v = peek(53269);
    if (on) v = v | mask;
    else v = v & ~mask;
    poke(53269, v);
}

/* Sets sprite n's (0-7) position. x is the sprite's full 0-511 range
 * (the visible screen's right edge, ~344, is itself past 255), so this
 * also maintains its bit in $D010 (the shared X-MSB register) - the
 * same reason asm/lib/graphics.inc's sprite0_bounce_step tracks a
 * 16-bit xpos instead of one byte. y never exceeds 255, so it needs no
 * equivalent handling. */
void sprite_pos(int n, int x, int y) {
    int msb;
    poke(53248 + n * 2, x & 255);
    poke(53249 + n * 2, y);
    msb = peek(53264);
    if (x > 255) msb = msb | (1 << n);
    else msb = msb & ~(1 << n);
    poke(53264, msb);
}

/* Sprite n's (0-7) own color, $D027-$D02E. For a multicolor sprite
 * (see sprite_multicolor() below), this is only the "11"-bit-pair
 * color - the other two colors are shared across every multicolor
 * sprite, set once via sprite_shared_colors(). */
void sprite_color(int n, int c) {
    poke(53287 + n, c);
}

/* Points sprite n (0-7) at its 63-byte shape data: `block` is a sprite
 * data block index (data address / 64), NOT a raw address - matching
 * how the VIC-II's own sprite-pointer bytes work (see
 * asm/lib/graphics.inc's SPRITE_INIT for the same convention). The
 * pointer bytes themselves live in the LAST 8 bytes of the current
 * video matrix, $07F8-$07FF for the default screen at $0400 - a real
 * hardware convention, not a cc64-specific choice.
 *
 * Two placement constraints on the sprite data itself, both real
 * hardware facts, not cc64-specific: it must avoid $1000-$1FFF/
 * $9000-$9FFF within the current VIC bank (the character ROM shadow -
 * the same quirk asm/lib/graphics.inc's own header comment documents),
 * and - easy to miss, since `block` is silently truncated to a byte
 * like any other poke() argument - it must fall within the VIC-II's
 * CURRENT 16K bank (the default, $0000-$3FFF, unless something has
 * reconfigured CIA2 PRA), not just anywhere in the CPU's full 64K
 * address space: a data address at or past $4000 divides out to a
 * block index over 255, which doesn't fit in one byte and silently
 * wraps to the wrong block instead of erroring. Caught for real
 * writing examples/bounce_demo.c: an address comfortably clear of
 * everything else ($6000) still produced a wrapped, wrong pointer for
 * exactly this reason. Within bank 0, remember that a real cc64
 * program's own compiled code/runtime library can easily run well
 * past the "$0d00 or $0e00" scratch address asm/lib/graphics.inc's
 * comment suggests for a short hand-assembled program - check where
 * your own program's code actually ends (its `.lst` output, or the
 * `.prg` file's size added to $0801) before picking an address. */
void sprite_pointer(int n, int block) {
    poke(2040 + n, block);
}

/* Turns sprite n's (0-7) multicolor mode on or off via $D01C. A
 * multicolor sprite trades horizontal resolution (each pixel pair
 * covers 2 real pixels) for a 4th visible color: transparent, the two
 * shared colors set by sprite_shared_colors(), and this sprite's own
 * color (sprite_color()). */
void sprite_multicolor(int n, int on) {
    int mask;
    int v;
    mask = 1 << n;
    v = peek(53276);
    if (on) v = v | mask;
    else v = v & ~mask;
    poke(53276, v);
}

/* Sets the two colors shared by every multicolor sprite (see
 * sprite_multicolor() above) - $D025 ("01" pixel pairs) and $D026
 * ("11" pairs). Unlike sprite_color(), this isn't per-sprite: real
 * VIC-II hardware only has one pair of these registers for all 8
 * sprites together. */
void sprite_shared_colors(int c1, int c3) {
    poke(53285, c1);
    poke(53286, c3);
}

/* Doubles sprite n's (0-7) width and/or height by drawing each source
 * pixel twice, via $D01D (X) and $D017 (Y). */
void sprite_expand(int n, int x_on, int y_on) {
    int mask;
    int v;
    mask = 1 << n;
    v = peek(53277);
    if (x_on) v = v | mask;
    else v = v & ~mask;
    poke(53277, v);
    v = peek(53271);
    if (y_on) v = v | mask;
    else v = v & ~mask;
    poke(53271, v);
}

/* Sets sprite n's (0-7) draw priority relative to text/bitmap graphics
 * via $D01B: `behind` nonzero hides this sprite behind any foreground
 * graphics pixel (real hardware's own definition of "foreground" -
 * see c64-memory-reference.md), zero (the power-on default) draws it
 * in front. Priority between two sprites is always just draw order
 * (sprite 0 highest, real hardware behavior) and isn't configurable at
 * all. */
void sprite_priority(int n, int behind) {
    int mask;
    int v;
    mask = 1 << n;
    v = peek(53275);
    if (behind) v = v | mask;
    else v = v & ~mask;
    poke(53275, v);
}
