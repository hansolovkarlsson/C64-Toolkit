/*
 * Hand-verified checks for ../../src/vic.c, worked out directly
 * against the register semantics and hardware quirks documented in
 * vic.h/vic.c's own header comments - there's no third-party VIC-II
 * test suite the way tests/cpu/ has Klaus Dormann's, so every expected
 * value below is derived by hand, the same way tests/memory/ and
 * tests/cia/ check against their own hand-derived expectations.
 */

#include "../../src/vic.h"
#include <stdio.h>
#include <string.h>

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { fprintf(stderr, "FAIL: %s\n", msg); failures++; } \
} while (0)

enum {
    REG_D011 = 0x11, REG_D012 = 0x12, REG_D016 = 0x16, REG_D018 = 0x18,
    REG_D019 = 0x19, REG_D01A = 0x1A, REG_D020 = 0x20, REG_D021 = 0x21,
    REG_D022 = 0x22, REG_D023 = 0x23, REG_D024 = 0x24,
    REG_D010 = 0x10, REG_D015 = 0x15, REG_D017 = 0x17, REG_D01B = 0x1B,
    REG_D01C = 0x1C, REG_D01D = 0x1D, REG_D01E = 0x1E, REG_D01F = 0x1F,
    REG_D025 = 0x25, REG_D026 = 0x26, REG_D027 = 0x27,
};

/* Every sprite test starts from the same baseline: DEN on, a blank
 * (all char-code-0, all-zero-glyph) text screen at $0400/$0000 so the
 * whole display window reads as background/fgmask=0 unless a test
 * deliberately draws into it, and sprite 0's data pointer set up at
 * $0800 (pointer byte 32 at $07F8, the video matrix's own last-8-bytes
 * convention) so tests only need to fill in $0800's 63 data bytes and
 * position/attribute registers, not repeat this plumbing every time. */
static void sprite_test_baseline(Vic *vic, Memory *mem) {
    vic_init(vic);
    memory_init(mem);
    vic_write(vic, REG_D011, 0x10); /* DEN, everything else off */
    vic_write(vic, REG_D018, 0x10); /* screen ptr -> $0400, char ptr -> $0000 */
    mem->ram[0x07F8] = 32; /* sprite 0's pointer byte: data at bank+32*64 = $0800 */
}

static void test_raster_counter(void) {
    Vic vic;
    vic_init(&vic);

    CHECK(vic_read(&vic, 0x12) == 0, "raster: should start at line 0");

    vic_tick(&vic, 100 * PAL_CYCLES_PER_LINE); /* advance to line 100 */
    CHECK(vic_read(&vic, 0x12) == 100, "raster: $D012 should read the current line's low 8 bits");
    CHECK((vic_read(&vic, REG_D011) & 0x80) == 0, "raster: $D011 bit7 (MSB) should be clear below line 256");

    vic_tick(&vic, (256 - 100) * PAL_CYCLES_PER_LINE); /* advance to line 256 */
    CHECK(vic_read(&vic, 0x12) == 0, "raster: $D012 should read 0 (256 & 0xFF) at line 256");
    CHECK((vic_read(&vic, REG_D011) & 0x80) != 0, "raster: $D011 bit7 should be set at line 256 and above");

    vic_tick(&vic, (PAL_LINES_PER_FRAME - 256) * PAL_CYCLES_PER_LINE); /* wrap back to line 0 */
    CHECK(vic_read(&vic, 0x12) == 0, "raster: should wrap back to line 0 after a full PAL frame");
    CHECK((vic_read(&vic, REG_D011) & 0x80) == 0, "raster: $D011 bit7 should be clear again after wrapping");
}

static void test_raster_irq(void) {
    Vic vic;
    vic_init(&vic);

    vic_write(&vic, REG_D011, 0x00); /* compare MSB = 0 */
    vic_write(&vic, REG_D012, 5);    /* compare = line 5 */
    CHECK(vic_read(&vic, 0x12) == 0, "raster IRQ: writing $D012 must NOT change what reading it returns (still the live line, not the compare value)");

    CHECK(vic_irq_line(&vic) == 0, "raster IRQ: line should be clear before the compare line is even reached");
    for (int line = 0; line < 5; line++) vic_tick(&vic, PAL_CYCLES_PER_LINE);
    CHECK(vic_irq_line(&vic) == 0, "raster IRQ: still masked (enable=0) even though the compare line was just reached - pending, not asserted");
    CHECK((vic_read(&vic, REG_D019) & 0x01) != 0, "raster IRQ: $D019 bit0 should be pending regardless of the mask");
    CHECK((vic_read(&vic, REG_D019) & 0x80) == 0, "raster IRQ: $D019 bit7 should be clear while masked - it reflects pending&enable, not pending alone");

    vic_write(&vic, REG_D01A, 0x01); /* unmask the raster source */
    CHECK(vic_irq_line(&vic) != 0, "raster IRQ: line should assert now that the already-pending source is unmasked");
    CHECK((vic_read(&vic, REG_D019) & 0x80) != 0, "raster IRQ: $D019 bit7 should now be set");

    vic_write(&vic, REG_D019, 0x01); /* write-1-to-clear */
    CHECK(vic_irq_line(&vic) == 0, "raster IRQ: writing 1 to $D019 bit0 should clear pending and de-assert the line");

    for (int line = 0; line < PAL_LINES_PER_FRAME - 5; line++) vic_tick(&vic, PAL_CYCLES_PER_LINE);
    CHECK(vic_irq_line(&vic) == 0, "raster IRQ: should stay clear for the rest of the frame until line 5 comes around again");
    vic_tick(&vic, PAL_CYCLES_PER_LINE); /* wraps past line 311 back to line 0, then up to line 5 over the next 5 calls below */
    for (int line = 0; line < 4; line++) vic_tick(&vic, PAL_CYCLES_PER_LINE);
    CHECK(vic_irq_line(&vic) != 0, "raster IRQ: should fire again once line 5 comes around on the next frame");
}

static void test_bad_lines(void) {
    Vic vic;
    vic_init(&vic);
    vic_write(&vic, REG_D011, 0x10); /* DEN=1, YSCROLL=0 */

    for (int line = 0; line < 48; line++) vic_tick(&vic, PAL_CYCLES_PER_LINE); /* -> line 48 ($30), the bottom edge of the window; 48 & 7 == 0 == YSCROLL */
    CHECK(vic_take_badline_stall(&vic) == VIC_BADLINE_STALL_CYCLES,
          "bad lines: entering line $30 (48) with matching YSCROLL should stall the CPU");
    CHECK(vic_take_badline_stall(&vic) == 0, "bad lines: the stall should only be reported once, then clear");

    Vic vic_no_den;
    vic_init(&vic_no_den);
    vic_write(&vic_no_den, REG_D011, 0x00); /* DEN=0, YSCROLL=0 - otherwise identical to the case above */
    for (int line = 0; line < 48; line++) vic_tick(&vic_no_den, PAL_CYCLES_PER_LINE);
    CHECK(vic_take_badline_stall(&vic_no_den) == 0, "bad lines: DEN=0 should suppress the stall even with a matching line/YSCROLL");

    Vic vic_outside;
    vic_init(&vic_outside);
    vic_write(&vic_outside, REG_D011, 0x10); /* DEN=1, YSCROLL=0 */
    for (int line = 0; line < 240; line++) vic_tick(&vic_outside, PAL_CYCLES_PER_LINE); /* -> line 240 ($F0): still inside the window */
    CHECK(vic_take_badline_stall(&vic_outside) == VIC_BADLINE_STALL_CYCLES,
          "bad lines: sanity check - line $F0 (240) is still inside the $30-$F7 window and should stall");
    for (int line = 0; line < 8; line++) vic_tick(&vic_outside, PAL_CYCLES_PER_LINE); /* -> line 248 ($F8): matching YSCROLL again, but past the window's top edge */
    CHECK(vic_take_badline_stall(&vic_outside) == 0,
          "bad lines: a matching YSCROLL just outside the $30-$F7 window ($F8/248) should not stall");
}

static void test_multicolor_text_mode(void) {
    Vic vic;
    Memory mem;
    vic_init(&vic);
    memory_init(&mem);

    vic_write(&vic, REG_D011, 0x10); /* DEN */
    vic_write(&vic, REG_D016, 0x10); /* MCM on */
    vic_write(&vic, REG_D018, 0x10); /* screen ptr -> $0400, char ptr -> $0000 (plain RAM) */
    vic_write(&vic, REG_D021, 1);    /* background color 0 = white (index 1) */
    vic_write(&vic, REG_D022, 2);    /* background color 1 = red (index 2) */
    vic_write(&vic, REG_D023, 5);    /* background color 2 = green (index 5) */

    mem.ram[0x0400] = 0;    /* char code 0 */
    mem.ram[0x0000] = 0x1B; /* 00 01 10 11 - one of each pixel-pair value */
    vic_color_ram_write(&vic, 0, 0x0D); /* 1101: bit3 set (multicolor cell), low 3 bits = 5 (green) - the "11" pair's color */

    static uint32_t pixels[VIC_CANVAS_H * VIC_CANVAS_W];
    vic_render_frame(&vic, &mem, 0, pixels, VIC_CANVAS_W);
    uint32_t *row0 = &pixels[VIC_BORDER_Y * VIC_CANVAS_W + VIC_BORDER_X];

    CHECK(row0[0] == 0xFFFFFF && row0[1] == 0xFFFFFF, "multicolor: pixel-pair '00' should render background color 0, 2 pixels wide");
    CHECK(row0[2] == 0x68372B && row0[3] == 0x68372B, "multicolor: pixel-pair '01' should render background color 1, 2 pixels wide");
    CHECK(row0[4] == 0x588D43 && row0[5] == 0x588D43, "multicolor: pixel-pair '10' should render background color 2, 2 pixels wide");
    CHECK(row0[6] == 0x588D43 && row0[7] == 0x588D43, "multicolor: pixel-pair '11' should render color RAM's low 3 bits as the 4th color, 2 pixels wide");
}

static void test_mcm_bit3_clear_stays_hires(void) {
    Vic vic;
    Memory mem;
    vic_init(&vic);
    memory_init(&mem);

    vic_write(&vic, REG_D011, 0x10); /* DEN */
    vic_write(&vic, REG_D016, 0x10); /* MCM on */
    vic_write(&vic, REG_D018, 0x10); /* screen ptr -> $0400, char ptr -> $0000 (plain RAM) */
    vic_write(&vic, REG_D021, 0);    /* background = black */

    mem.ram[0x0400] = 0;
    /* 10110100 - deliberately NOT a "looks the same either way" pattern:
     * read as hi-res this is fg,bg,fg,fg,bg,fg,bg,bg (8 distinct 1px
     * values); read as multicolor pairs it would be 10,11,01,00 - bg2,
     * fg,bg1,bg0 instead (4 distinct 2px-wide values) - a real test of
     * WHICH path rendered, not just what the pixels happen to be. */
    mem.ram[0x0000] = 0xB4;
    vic_color_ram_write(&vic, 0, 5); /* bit3 CLEAR (5 < 8): must force plain hi-res despite MCM being on globally */

    static uint32_t pixels[VIC_CANVAS_H * VIC_CANVAS_W];
    vic_render_frame(&vic, &mem, 0, pixels, VIC_CANVAS_W);
    uint32_t *row0 = &pixels[VIC_BORDER_Y * VIC_CANVAS_W + VIC_BORDER_X];

    uint32_t fg = 0x588D43, bg = 0x000000;
    uint32_t expected[8] = {fg, bg, fg, fg, bg, fg, bg, bg};
    int ok = 1;
    for (int i = 0; i < 8; i++) {
        if (row0[i] != expected[i]) ok = 0;
    }
    CHECK(ok, "MCM on but color RAM bit3 clear: cell should still render plain hi-res (per-pixel), not multicolor pixel-pairs");
}

static void test_extended_background_color_mode(void) {
    Vic vic;
    Memory mem;
    vic_init(&vic);
    memory_init(&mem);

    vic_write(&vic, REG_D011, 0x50); /* DEN | ECM, MCM/BMM off */
    vic_write(&vic, REG_D018, 0x10); /* screen ptr -> $0400, char ptr -> $0000 (plain RAM) */
    vic_write(&vic, REG_D021, 1);    /* background color 0 = white */
    vic_write(&vic, REG_D022, 2);    /* background color 1 = red */
    vic_write(&vic, REG_D023, 5);    /* background color 2 = green */
    vic_write(&vic, REG_D024, 3);    /* background color 3 = cyan */

    /* Char code's top 2 bits = 01 -> background color 1 (red); low 6
     * bits = 0 -> glyph 0, same pattern used elsewhere in this file. */
    mem.ram[0x0400] = 0x40;
    mem.ram[0x0000] = 0xB4; /* 10110100 */
    vic_color_ram_write(&vic, 0, 1); /* foreground = white - full nibble, ECM never masks color RAM */

    static uint32_t pixels[VIC_CANVAS_H * VIC_CANVAS_W];
    vic_render_frame(&vic, &mem, 0, pixels, VIC_CANVAS_W);
    uint32_t *row0 = &pixels[VIC_BORDER_Y * VIC_CANVAS_W + VIC_BORDER_X];

    uint32_t fg = 0xFFFFFF, bg = 0x68372B; /* palette[1], palette[2] */
    uint32_t expected[8] = {fg, bg, fg, fg, bg, fg, bg, bg};
    int ok = 1;
    for (int i = 0; i < 8; i++) {
        if (row0[i] != expected[i]) ok = 0;
    }
    CHECK(ok, "extended background color mode: char code bits 6-7 should pick $D022 as this cell's background, foreground still the full color RAM nibble");
}

static void test_ecm_masks_character_code(void) {
    Vic vic;
    Memory mem;
    vic_init(&vic);
    memory_init(&mem);

    vic_write(&vic, REG_D011, 0x50); /* DEN | ECM */
    vic_write(&vic, REG_D018, 0x10); /* screen ptr -> $0400, char ptr -> $0000 */
    vic_write(&vic, REG_D024, 3);    /* background color 3 = cyan - selected by top bits = 11 below */

    /* Char code $C5: top bits 11 -> background color 3; low 6 bits
     * (5) should be the ONLY part used to address character memory. */
    mem.ram[0x0400] = 0xC5;
    memset(&mem.ram[0xC5 * 8], 0xFF, 8); /* full (unmasked) char code's glyph slot: solid foreground - must NOT be used */
    memset(&mem.ram[5 * 8], 0x00, 8);    /* masked (char_code & 0x3F) glyph slot: solid background - should be what actually renders */
    vic_color_ram_write(&vic, 0, 1);     /* foreground = white, irrelevant since the glyph is all-background */

    static uint32_t pixels[VIC_CANVAS_H * VIC_CANVAS_W];
    vic_render_frame(&vic, &mem, 0, pixels, VIC_CANVAS_W);
    uint32_t *row0 = &pixels[VIC_BORDER_Y * VIC_CANVAS_W + VIC_BORDER_X];

    int ok = 1;
    for (int i = 0; i < 8; i++) {
        if (row0[i] != 0x70A4B2) ok = 0; /* palette[3], cyan - background color 3 throughout */
    }
    CHECK(ok, "extended background color mode: only the low 6 bits of the character code should address character memory, not the full byte");
}

static void test_ecm_invalid_mode_combinations(void) {
    Vic vic;
    Memory mem;

    vic_init(&vic);
    memory_init(&mem);
    vic_write(&vic, REG_D020, 2); /* border = red */
    vic_write(&vic, REG_D011, 0x50); /* DEN | ECM */
    vic_write(&vic, REG_D016, 0x10); /* MCM on too - invalid combination */
    static uint32_t pixels[VIC_CANVAS_H * VIC_CANVAS_W];
    vic_render_frame(&vic, &mem, 0, pixels, VIC_CANVAS_W);
    uint32_t corner = pixels[0];
    uint32_t center = pixels[(VIC_CANVAS_H / 2) * VIC_CANVAS_W + VIC_CANVAS_W / 2];
    CHECK(corner == 0x68372B, "ECM+MCM invalid mode: border should still show the normal border color");
    CHECK(center == 0x000000, "ECM+MCM invalid mode: the display window should render solid black, real VIC-II 'invalid mode' behavior");

    vic_init(&vic);
    memory_init(&mem);
    vic_write(&vic, REG_D020, 2); /* border = red */
    vic_write(&vic, REG_D011, 0x70); /* DEN | ECM | BMM - also invalid */
    vic_write(&vic, REG_D016, 0x00); /* MCM off */
    vic_render_frame(&vic, &mem, 0, pixels, VIC_CANVAS_W);
    corner = pixels[0];
    center = pixels[(VIC_CANVAS_H / 2) * VIC_CANVAS_W + VIC_CANVAS_W / 2];
    CHECK(corner == 0x68372B, "ECM+BMM invalid mode: border should still show the normal border color");
    CHECK(center == 0x000000, "ECM+BMM invalid mode: the display window should render solid black too");
}

static void test_standard_bitmap_mode(void) {
    Vic vic;
    Memory mem;
    vic_init(&vic);
    memory_init(&mem);

    vic_write(&vic, REG_D011, 0x30); /* DEN | BMM */
    vic_write(&vic, REG_D016, 0x00); /* MCM off - standard (2-color) bitmap mode */
    vic_write(&vic, REG_D018, 0x10); /* screen ptr -> $0400, bitmap bit3=0 -> bitmap at bank+$0000 */

    mem.ram[0x0400] = 0x51; /* this cell's 2 colors: upper nibble=5 (green) for set bits, lower nibble=1 (white) for clear bits */
    /* Same 10110100 pattern used for the hi-res-text/MCM-off test - deliberately not symmetric, so a wrong bit order or wrong color source shows up immediately. */
    mem.ram[0x0000] = 0xB4;

    static uint32_t pixels[VIC_CANVAS_H * VIC_CANVAS_W];
    vic_render_frame(&vic, &mem, 0, pixels, VIC_CANVAS_W);
    uint32_t *row0 = &pixels[VIC_BORDER_Y * VIC_CANVAS_W + VIC_BORDER_X];

    uint32_t fg = 0x588D43, bg = 0xFFFFFF; /* palette[5], palette[1] */
    uint32_t expected[8] = {fg, bg, fg, fg, bg, fg, bg, bg};
    int ok = 1;
    for (int i = 0; i < 8; i++) {
        if (row0[i] != expected[i]) ok = 0;
    }
    CHECK(ok, "standard bitmap mode: pixels should come from the bitmap byte, colors from screen RAM's nibbles (upper=set bits, lower=clear bits), not color RAM");
}

static void test_multicolor_bitmap_mode(void) {
    Vic vic;
    Memory mem;
    vic_init(&vic);
    memory_init(&mem);

    vic_write(&vic, REG_D011, 0x30); /* DEN | BMM */
    vic_write(&vic, REG_D016, 0x10); /* MCM on - multicolor bitmap mode */
    vic_write(&vic, REG_D018, 0x10); /* screen ptr -> $0400, bitmap bit3=0 -> bitmap at bank+$0000 */
    vic_write(&vic, REG_D021, 0);    /* background color 0 = black */

    mem.ram[0x0400] = 0x51; /* screen RAM nibbles: upper=5 (green), lower=1 (white) - the '01'/'10' pixel-pair colors here, NOT a 2-color bitmap byte */
    mem.ram[0x0000] = 0x1B; /* 00 01 10 11 - one of each pixel-pair value */
    vic_color_ram_write(&vic, 0, 3); /* color RAM = cyan (index 3) - the '11' pixel-pair color */

    static uint32_t pixels[VIC_CANVAS_H * VIC_CANVAS_W];
    vic_render_frame(&vic, &mem, 0, pixels, VIC_CANVAS_W);
    uint32_t *row0 = &pixels[VIC_BORDER_Y * VIC_CANVAS_W + VIC_BORDER_X];

    CHECK(row0[0] == 0x000000 && row0[1] == 0x000000, "multicolor bitmap: pixel-pair '00' should render background color 0 ($D021), 2 pixels wide");
    CHECK(row0[2] == 0x588D43 && row0[3] == 0x588D43, "multicolor bitmap: pixel-pair '01' should render screen RAM's upper nibble, 2 pixels wide");
    CHECK(row0[4] == 0xFFFFFF && row0[5] == 0xFFFFFF, "multicolor bitmap: pixel-pair '10' should render screen RAM's lower nibble, 2 pixels wide");
    CHECK(row0[6] == 0x70A4B2 && row0[7] == 0x70A4B2, "multicolor bitmap: pixel-pair '11' should render color RAM's low nibble, 2 pixels wide");
}

static void test_sprite_hires_position_and_shape(void) {
    Vic vic;
    Memory mem;
    sprite_test_baseline(&vic, &mem);

    /* Row 0: 0xFF, 0x00, 0xFF - opaque, transparent, opaque, 8 pixels each. */
    mem.ram[0x0800] = 0xFF;
    mem.ram[0x0801] = 0x00;
    mem.ram[0x0802] = 0xFF;

    vic_write(&vic, 0x00, 24); /* sprite 0 X low byte = 24 -> canvas x=0 (VIC_SPRITE_X_OFFSET) */
    vic_write(&vic, 0x01, 50); /* sprite 0 Y = 50 -> canvas y=0 (VIC_SPRITE_Y_OFFSET) */
    vic_write(&vic, REG_D015, 0x01); /* enable sprite 0 */
    vic_write(&vic, REG_D027, 2);    /* sprite 0 color = red */

    static uint32_t pixels[VIC_CANVAS_H * VIC_CANVAS_W];
    vic_render_frame(&vic, &mem, 0, pixels, VIC_CANVAS_W);
    uint32_t *row0 = &pixels[0];

    uint32_t red = 0x68372B, black = 0x000000;
    for (int i = 0; i < 8; i++) CHECK(row0[i] == red, "sprite hi-res: first byte's 8 bits should render opaque in the sprite's own color");
    for (int i = 8; i < 16; i++) CHECK(row0[i] == black, "sprite hi-res: second (all-clear) byte should be transparent, showing the border color underneath");
    for (int i = 16; i < 24; i++) CHECK(row0[i] == red, "sprite hi-res: third byte should render opaque again");
    CHECK(pixels[24] == black, "sprite hi-res: pixel 24 is past the 24-pixel-wide sprite and should show the plain border color");
}

static void test_sprite_disabled_does_not_render(void) {
    Vic vic;
    Memory mem;
    sprite_test_baseline(&vic, &mem);

    mem.ram[0x0800] = 0xFF; /* fully opaque row, same position as the enabled test above */
    vic_write(&vic, 0x00, 24);
    vic_write(&vic, 0x01, 50);
    vic_write(&vic, REG_D027, 2);
    /* $D015 left at 0 - sprite 0 never enabled. */

    static uint32_t pixels[VIC_CANVAS_H * VIC_CANVAS_W];
    vic_render_frame(&vic, &mem, 0, pixels, VIC_CANVAS_W);
    CHECK(pixels[0] == 0x000000, "disabled sprite: should not render at all, even with valid position/pointer/data");
}

static void test_sprite_multicolor(void) {
    Vic vic;
    Memory mem;
    sprite_test_baseline(&vic, &mem);

    /* Same 00/01/10/11 repeating pattern used for multicolor text/bitmap tests. */
    mem.ram[0x0800] = 0x1B;
    mem.ram[0x0801] = 0x1B;
    mem.ram[0x0802] = 0x1B;

    vic_write(&vic, 0x00, 24);
    vic_write(&vic, 0x01, 50);
    vic_write(&vic, REG_D015, 0x01);
    vic_write(&vic, REG_D01C, 0x01); /* multicolor sprite 0 */
    vic_write(&vic, REG_D025, 1);    /* shared multicolor 0 = white - the '01' pair color */
    vic_write(&vic, REG_D026, 5);    /* shared multicolor 1 = green - the '11' pair color */
    vic_write(&vic, REG_D027, 3);    /* sprite 0's own color = cyan - the '10' pair color */

    static uint32_t pixels[VIC_CANVAS_H * VIC_CANVAS_W];
    vic_render_frame(&vic, &mem, 0, pixels, VIC_CANVAS_W);
    uint32_t *row0 = &pixels[0];

    CHECK(row0[0] == 0x000000 && row0[1] == 0x000000, "sprite multicolor: pixel-pair '00' should stay transparent (border shows through), 2 pixels wide");
    CHECK(row0[2] == 0xFFFFFF && row0[3] == 0xFFFFFF, "sprite multicolor: pixel-pair '01' should render shared multicolor 0 ($D025), 2 pixels wide");
    CHECK(row0[4] == 0x70A4B2 && row0[5] == 0x70A4B2, "sprite multicolor: pixel-pair '10' should render the sprite's OWN color, 2 pixels wide");
    CHECK(row0[6] == 0x588D43 && row0[7] == 0x588D43, "sprite multicolor: pixel-pair '11' should render shared multicolor 1 ($D026), 2 pixels wide");
}

static void test_sprite_x_and_y_expand(void) {
    Vic vic;
    Memory mem;
    sprite_test_baseline(&vic, &mem);

    mem.ram[0x0800] = 0x80; /* source row 0: only the first pixel opaque */
    mem.ram[0x0801] = 0x00;
    mem.ram[0x0802] = 0x00;
    mem.ram[0x0803] = 0x00; /* source row 1: fully transparent */
    mem.ram[0x0804] = 0x00;
    mem.ram[0x0805] = 0x00;

    vic_write(&vic, 0x00, 24);
    vic_write(&vic, 0x01, 50);
    vic_write(&vic, REG_D015, 0x01);
    vic_write(&vic, REG_D01D, 0x01); /* X-expand sprite 0 */
    vic_write(&vic, REG_D017, 0x01); /* Y-expand sprite 0 */
    vic_write(&vic, REG_D027, 2);    /* red */

    static uint32_t pixels[VIC_CANVAS_H * VIC_CANVAS_W];
    vic_render_frame(&vic, &mem, 0, pixels, VIC_CANVAS_W);

    uint32_t red = 0x68372B, black = 0x000000;
    CHECK(pixels[0] == red && pixels[1] == red, "sprite X-expand: the one opaque source pixel should render 2 real pixels wide");
    CHECK(pixels[2] == black, "sprite X-expand: past the doubled pixel should be transparent again");
    CHECK(pixels[VIC_CANVAS_W] == red && pixels[VIC_CANVAS_W + 1] == red,
          "sprite Y-expand: canvas row 1 should repeat source row 0's data, not move on to source row 1");
    CHECK(pixels[2 * VIC_CANVAS_W] == black,
          "sprite Y-expand: canvas row 2 should show source row 1 (all transparent), proving the Y mapping actually advances");
}

static void test_sprite_priority_vs_graphics(void) {
    Vic vic;
    Memory mem;
    sprite_test_baseline(&vic, &mem);

    /* Cell (0,0) (canvas x32-39) renders solid foreground; cells (0,1)
     * and (0,2) (x40-55) stay background - so a 24px-wide sprite
     * starting at x32 spans one foreground cell and two background
     * ones, letting a single render prove both halves of $D01B's
     * per-pixel behavior at once. */
    mem.ram[0x0400] = 0; /* cell (0,0): char code 0 */
    for (int i = 0; i < 8; i++) mem.ram[0x0000 + i] = 0xFF; /* glyph 0: solid */
    vic_color_ram_write(&vic, 0, 1); /* white foreground */
    /* cells (0,1)/(0,2) already char code 0 too, but a DIFFERENT glyph
     * (code 1) kept all-zero (background) - point them at glyph 1. */
    mem.ram[0x0401] = 1;
    mem.ram[0x0402] = 1;

    mem.ram[0x0800] = 0xFF; mem.ram[0x0801] = 0xFF; mem.ram[0x0802] = 0xFF; /* fully opaque sprite row */
    vic_write(&vic, 0x00, 24 + 32); /* canvas x = 32, the start of cell (0,0) */
    vic_write(&vic, 0x01, 50 + 35); /* canvas y = 35, cell row 0 */
    vic_write(&vic, REG_D015, 0x01);
    vic_write(&vic, REG_D027, 2); /* sprite color = red */

    static uint32_t pixels[VIC_CANVAS_H * VIC_CANVAS_W];
    uint32_t white = 0xFFFFFF, red = 0x68372B;

    vic_write(&vic, REG_D01B, 0x00); /* priority: always in front */
    vic_render_frame(&vic, &mem, 0, pixels, VIC_CANVAS_W);
    uint32_t *row = &pixels[VIC_BORDER_Y * VIC_CANVAS_W + VIC_BORDER_X];
    for (int i = 0; i < 24; i++) CHECK(row[i] == red, "sprite priority (front): should cover graphics foreground AND background alike");

    vic_write(&vic, REG_D01B, 0x01); /* priority: behind graphics foreground */
    vic_render_frame(&vic, &mem, 0, pixels, VIC_CANVAS_W);
    row = &pixels[VIC_BORDER_Y * VIC_CANVAS_W + VIC_BORDER_X];
    for (int i = 0; i < 8; i++) CHECK(row[i] == white, "sprite priority (behind): graphics foreground pixels should win over the sprite here");
    for (int i = 8; i < 24; i++) CHECK(row[i] == red, "sprite priority (behind): graphics BACKGROUND pixels should still lose to the sprite");
}

static void test_sprite_vs_sprite_priority(void) {
    Vic vic;
    Memory mem;
    sprite_test_baseline(&vic, &mem);

    mem.ram[0x0800] = 0xFF; mem.ram[0x0801] = 0xFF; mem.ram[0x0802] = 0xFF; /* sprite 0's data */
    mem.ram[0x07F9] = 33; /* sprite 1's pointer -> data at $0840 */
    mem.ram[0x0840] = 0xFF; mem.ram[0x0841] = 0xFF; mem.ram[0x0842] = 0xFF; /* sprite 1's data, identical footprint */

    vic_write(&vic, 0x00, 24); vic_write(&vic, 0x01, 50); /* sprite 0 at canvas (0,0) */
    vic_write(&vic, 0x02, 24); vic_write(&vic, 0x03, 50); /* sprite 1 at the SAME position */
    vic_write(&vic, REG_D015, 0x03); /* enable both */
    vic_write(&vic, REG_D027, 2);     /* sprite 0 = red */
    vic_write(&vic, REG_D027 + 1, 5); /* sprite 1 = green */

    static uint32_t pixels[VIC_CANVAS_H * VIC_CANVAS_W];
    vic_render_frame(&vic, &mem, 0, pixels, VIC_CANVAS_W);
    CHECK(pixels[0] == 0x68372B, "sprite-vs-sprite priority: lower-numbered sprite 0 should win over sprite 1 wherever both are opaque");
}

static void test_sprite_sprite_collision(void) {
    Vic vic;
    Memory mem;
    sprite_test_baseline(&vic, &mem);

    mem.ram[0x0800] = 0xFF; mem.ram[0x0801] = 0x00; mem.ram[0x0802] = 0x00;
    mem.ram[0x07F9] = 33;
    mem.ram[0x0840] = 0xFF; mem.ram[0x0841] = 0x00; mem.ram[0x0842] = 0x00; /* sprite 1: same first byte, so pixels 0-7 overlap sprite 0's */

    vic_write(&vic, 0x00, 24); vic_write(&vic, 0x01, 50);
    vic_write(&vic, 0x02, 24); vic_write(&vic, 0x03, 50); /* fully overlapping */
    vic_write(&vic, REG_D015, 0x03);
    vic_write(&vic, REG_D027, 2);
    vic_write(&vic, REG_D027 + 1, 5);
    vic_write(&vic, REG_D01A, 0x04); /* unmask sprite-sprite collision IRQ, so we can check it fires too */

    static uint32_t pixels[VIC_CANVAS_H * VIC_CANVAS_W];
    vic_render_frame(&vic, &mem, 0, pixels, VIC_CANVAS_W);

    CHECK(vic_irq_line(&vic) != 0, "sprite-sprite collision: should have raised an IRQ (unmasked, $D019 bit2)");
    uint8_t collided = vic_read(&vic, REG_D01E);
    CHECK(collided == 0x03, "sprite-sprite collision: $D01E should have both sprites' bits set");
    CHECK(vic_read(&vic, REG_D01E) == 0, "sprite-sprite collision: reading $D01E should clear it");
    CHECK(vic_irq_line(&vic) != 0, "sprite-sprite collision: reading $D01E must NOT itself clear $D019's pending bit - only an explicit write-1 to $D019 does");
    vic_write(&vic, REG_D019, 0x04);
    CHECK(vic_irq_line(&vic) == 0, "sprite-sprite collision: write-1 to $D019 bit2 should finally drop the IRQ");
}

static void test_sprite_background_collision(void) {
    Vic vic;
    Memory mem;
    sprite_test_baseline(&vic, &mem);

    mem.ram[0x0400] = 0;
    for (int i = 0; i < 8; i++) mem.ram[0x0000 + i] = 0xFF; /* cell (0,0): solid foreground */
    vic_color_ram_write(&vic, 0, 1);

    mem.ram[0x0800] = 0xFF; mem.ram[0x0801] = 0x00; mem.ram[0x0802] = 0x00;
    vic_write(&vic, 0x00, 24 + 32); /* land sprite 0 right on cell (0,0) */
    vic_write(&vic, 0x01, 50 + 35);
    vic_write(&vic, REG_D015, 0x01);
    vic_write(&vic, REG_D027, 2);
    vic_write(&vic, REG_D01B, 0x00); /* priority irrelevant to collision detection - front, so it's also visibly there */

    static uint32_t pixels[VIC_CANVAS_H * VIC_CANVAS_W];
    vic_render_frame(&vic, &mem, 0, pixels, VIC_CANVAS_W);

    uint8_t collided = vic_read(&vic, REG_D01F);
    CHECK(collided == 0x01, "sprite-background collision: $D01F should have sprite 0's bit set after overlapping a graphics foreground pixel");
    CHECK(vic_read(&vic, REG_D01F) == 0, "sprite-background collision: reading $D01F should clear it");
}

static void test_den_blanks_to_border(void) {
    Vic vic;
    Memory mem;
    vic_init(&vic);
    memory_init(&mem);

    vic_write(&vic, REG_D020, 2); /* border = red */
    vic_write(&vic, REG_D021, 0); /* background = black */
    /* $D011 left at 0 (DEN clear) - vic_init()'s default. */

    static uint32_t pixels[VIC_CANVAS_H * VIC_CANVAS_W];
    vic_render_frame(&vic, &mem, 0, pixels, VIC_CANVAS_W);

    uint32_t border_rgb = pixels[0]; /* top-left corner: always border */
    uint32_t center_rgb = pixels[(VIC_CANVAS_H / 2) * VIC_CANVAS_W + VIC_CANVAS_W / 2];
    CHECK(center_rgb == border_rgb, "DEN clear: the whole interior should show the border color, not background/text");
}

static void test_text_rendering_from_ram(void) {
    Vic vic;
    Memory mem;
    vic_init(&vic);
    memory_init(&mem);

    vic_write(&vic, REG_D011, 0x10); /* DEN set, everything else default */
    vic_write(&vic, REG_D020, 2);    /* border = red (index 2) */
    vic_write(&vic, REG_D021, 0);    /* background = black (index 0) */
    /* screen ptr = 1 (-> $0400), char ptr bits = 0 (-> $0000, plain RAM, not the char ROM range). */
    vic_write(&vic, REG_D018, 0x10);

    mem.ram[0x0400] = 0; /* char code 0 at the first screen cell */
    for (int i = 0; i < 8; i++) mem.ram[0x0000 + i] = 0xFF; /* solid 8x8 glyph */
    vic_color_ram_write(&vic, 0, 1); /* foreground = white (index 1) */

    static uint32_t pixels[VIC_CANVAS_H * VIC_CANVAS_W];
    vic_render_frame(&vic, &mem, 0, pixels, VIC_CANVAS_W);

    uint32_t fg_pixel = pixels[VIC_BORDER_Y * VIC_CANVAS_W + VIC_BORDER_X];
    CHECK(fg_pixel == 0xFFFFFF, "text rendering: a solid glyph byte should paint the foreground color from color RAM");

    uint32_t corner = pixels[0];
    CHECK(corner == 0x68372B, "text rendering: the border area should still show the border color, untouched by the glyph");
}

static void test_char_rom_visibility_quirk(void) {
    Vic vic;
    Memory mem;
    vic_init(&vic);
    memory_init(&mem);

    vic_write(&vic, REG_D011, 0x10);
    vic_write(&vic, REG_D021, 0);
    /* screen ptr = 1 (-> $0400), char ptr bits = 2 (-> offset $1000 - the char-ROM-eligible range). */
    vic_write(&vic, REG_D018, 0x14);
    mem.ram[0x0400] = 0;

    memset(mem.char_rom, 0xAA, 8);           /* char ROM: alternating bits */
    memset(&mem.ram[0x1000], 0x00, 8);       /* RAM at the same offset: all clear - must NOT be what renders in bank 0 */
    vic_color_ram_write(&vic, 0, 1);

    static uint32_t pixels[VIC_CANVAS_H * VIC_CANVAS_W];
    vic_render_frame(&vic, &mem, 0, pixels, VIC_CANVAS_W); /* bank 0: char ROM should be visible here */
    uint32_t *row0 = &pixels[VIC_BORDER_Y * VIC_CANVAS_W + VIC_BORDER_X];
    CHECK(row0[0] == 0xFFFFFF && row0[1] == 0x000000,
          "char ROM visibility: bank 0 with char ptr=2 should render from char ROM (0xAA = fg,bg,fg,bg...), not RAM");

    memset(&mem.ram[0x4000 + 0x1000], 0x0F, 8); /* bank 1's RAM at the same relative offset - a distinct pattern */
    vic_render_frame(&vic, &mem, 1, pixels, VIC_CANVAS_W); /* bank 1: char ROM is NOT wired here on real hardware */
    row0 = &pixels[VIC_BORDER_Y * VIC_CANVAS_W + VIC_BORDER_X];
    CHECK(row0[0] == 0x000000 && row0[4] == 0xFFFFFF,
          "char ROM visibility: bank 1 with the same char ptr should render from RAM instead (0x0F), not char ROM");
}

static void test_color_ram_nibble_masking(void) {
    Vic vic;
    vic_init(&vic);

    vic_color_ram_write(&vic, 5, 0xC3);
    CHECK(vic_color_ram_read(&vic, 5) == 0xF3, "color RAM: only the low nibble should be stored, and reads should show the upper nibble as all 1s");
}

int main(void) {
    test_raster_counter();
    test_raster_irq();
    test_bad_lines();
    test_multicolor_text_mode();
    test_mcm_bit3_clear_stays_hires();
    test_extended_background_color_mode();
    test_ecm_masks_character_code();
    test_ecm_invalid_mode_combinations();
    test_standard_bitmap_mode();
    test_multicolor_bitmap_mode();
    test_sprite_hires_position_and_shape();
    test_sprite_disabled_does_not_render();
    test_sprite_multicolor();
    test_sprite_x_and_y_expand();
    test_sprite_priority_vs_graphics();
    test_sprite_vs_sprite_priority();
    test_sprite_sprite_collision();
    test_sprite_background_collision();
    test_den_blanks_to_border();
    test_text_rendering_from_ram();
    test_char_rom_visibility_quirk();
    test_color_ram_nibble_masking();

    if (failures == 0) {
        printf("PASS: all VIC-II checks passed\n");
        return 0;
    }
    fprintf(stderr, "FAIL: %d check(s) failed\n", failures);
    return 1;
}
