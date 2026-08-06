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
    REG_D022 = 0x22, REG_D023 = 0x23,
};

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
