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

enum { REG_D011 = 0x11, REG_D018 = 0x18, REG_D020 = 0x20, REG_D021 = 0x21 };

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
