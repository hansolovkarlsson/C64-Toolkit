/*
 * vic.c - see vic.h's header comment for scope (text mode + raster
 * IRQs) and what's deliberately deferred.
 */

#include "vic.h"
#include <string.h>

enum {
    REG_D011 = 0x11, /* control register 1: bit7=raster MSB (read)/compare MSB (write), bit4=DEN, bit3=RSEL (not modeled), bits0-2=YSCROLL (not modeled) */
    REG_D012 = 0x12, /* raster (read: live low 8 bits; write: compare low 8 bits) */
    REG_D016 = 0x16, /* control register 2: bit4=MCM, bit3=CSEL (not modeled), bits0-2=XSCROLL (not modeled) */
    REG_D018 = 0x18, /* memory pointers: bits4-7=screen pointer (1K units), bits1-3=char/bitmap pointer (2K units) */
    REG_D019 = 0x19, /* IRQ status: bits0-3 pending (bit0=raster, bit1=sprite-background collision, bit2=sprite-sprite collision, bit3=light pen - never set, not planned), bit7=read-only summary. Write CLEARS whichever of bits0-3 are 1 in the value written - see vic_write(). */
    REG_D01A = 0x1A, /* IRQ enable: bits0-3, plain read/write */
    REG_D020 = 0x20, /* border color (low nibble) */
    REG_D021 = 0x21, /* background color 0 (low nibble) */
    REG_D022 = 0x22, /* background color 1 (low nibble) - multicolor text mode only */
    REG_D023 = 0x23, /* background color 2 (low nibble) - multicolor text mode only */
    REG_D024 = 0x24, /* background color 3 (low nibble) - extended background color text mode only */
    REG_D010 = 0x10, /* sprite X MSBs: bit n = sprite n's 9th (top) X bit */
    REG_D015 = 0x15, /* sprite enable: bit n = sprite n enabled */
    REG_D017 = 0x17, /* sprite Y expansion (double height): bit n = sprite n */
    REG_D01B = 0x1B, /* sprite data priority: bit n=1 -> sprite n draws behind graphics FOREGROUND pixels (still in front of background pixels) */
    REG_D01C = 0x1C, /* sprite multicolor mode: bit n = sprite n */
    REG_D01D = 0x1D, /* sprite X expansion (double width): bit n = sprite n */
    REG_D01E = 0x1E, /* sprite-sprite collision: bit n set when sprite n's opaque pixels overlapped another sprite's. Reading clears the WHOLE register - see vic_read(). */
    REG_D01F = 0x1F, /* sprite-background collision: bit n set when sprite n's opaque pixels overlapped a graphics FOREGROUND pixel. Reading clears the WHOLE register - see vic_read(). */
    REG_D025 = 0x25, /* sprite multicolor 0 (low nibble) - shared by every multicolor sprite's '01' pixel-pair */
    REG_D026 = 0x26, /* sprite multicolor 1 (low nibble) - shared by every multicolor sprite's '11' pixel-pair */
    REG_D027 = 0x27, /* sprite 0's individual color (low nibble); sprite n's is REG_D027+n, through REG_D02E for sprite 7 */
};

#define VIC_IRQ_RASTER 0x01
#define VIC_IRQ_SPRITE_BG 0x02
#define VIC_IRQ_SPRITE_SPRITE 0x04

/* Widely-cited (c64-wiki/VICE) landmark: sprite registers X=24,Y=50
 * put a sprite's top-left pixel at the top-left corner of this
 * project's own canvas (see vic.h) - i.e. real hardware's raster
 * origin for the visible field, not independently re-derived against
 * real hardware, same spirit as VIC_BORDER_X/Y's own approximation. */
#define VIC_SPRITE_X_OFFSET 24
#define VIC_SPRITE_Y_OFFSET 50
#define VIC_SPRITE_W 24
#define VIC_SPRITE_H 21

/* A commonly used approximation of the VIC-II's real analog NTSC/PAL
 * output (this is "Pepto's palette", widely reused across emulators),
 * not derived from measuring real hardware ourselves. Index = the low
 * nibble of a color register / color RAM nibble. */
static const uint32_t VIC_PALETTE[16] = {
    0x000000, 0xFFFFFF, 0x68372B, 0x70A4B2,
    0x6F3D86, 0x588D43, 0x352879, 0xB8C76F,
    0x6F4F25, 0x433900, 0x9A6759, 0x444444,
    0x6C6C6C, 0x9AD284, 0x6C5EB5, 0x959595,
};

void vic_init(Vic *vic) {
    memset(vic, 0, sizeof(*vic));
}

uint8_t vic_read(Vic *vic, uint8_t reg) {
    reg &= 0x3F;
    if (reg == REG_D011) {
        uint32_t line = vic->raster_cycle / PAL_CYCLES_PER_LINE;
        uint8_t msb = (uint8_t)((line >> 8) & 0x01);
        return (uint8_t)((vic->regs[REG_D011] & 0x7F) | (msb << 7));
    }
    if (reg == REG_D012) {
        uint32_t line = vic->raster_cycle / PAL_CYCLES_PER_LINE;
        return (uint8_t)(line & 0xFF);
    }
    if (reg == REG_D019) {
        uint8_t pending = vic->regs[REG_D019] & 0x0F;
        uint8_t enable = vic->regs[REG_D01A] & 0x0F;
        return (uint8_t)(pending | ((pending & enable) ? 0x80 : 0));
    }
    if (reg == REG_D01E || reg == REG_D01F) {
        /* Real hardware: reading either collision register returns its
         * latched bits AND clears the whole register - unlike $D019,
         * there's no write-1-to-clear here, any read clears it. */
        uint8_t v = vic->regs[reg];
        vic->regs[reg] = 0;
        return v;
    }
    return vic->regs[reg];
}

void vic_write(Vic *vic, uint8_t reg, uint8_t v) {
    reg &= 0x3F;
    if (reg == REG_D011) {
        vic->raster_compare = (uint16_t)((vic->raster_compare & 0x00FF) | ((v & 0x80) ? 0x100 : 0));
        vic->regs[REG_D011] = v; /* other bits (DEN/RSEL/YSCROLL) are plain storage, only bit7 is dual-purpose */
        return;
    }
    if (reg == REG_D012) {
        vic->raster_compare = (uint16_t)((vic->raster_compare & 0x0100) | v);
        vic->regs[REG_D012] = v; /* not read back as such - vic_read() always returns the live raster line here - kept only for consistency/debugging */
        return;
    }
    if (reg == REG_D019) {
        vic->regs[REG_D019] &= (uint8_t)~(v & 0x0F); /* write-1-to-clear, real 6567/6569 behavior - not a plain assignment */
        return;
    }
    vic->regs[reg] = v;
}

int vic_irq_line(const Vic *vic) {
    return (vic->regs[REG_D019] & vic->regs[REG_D01A] & 0x0F) != 0;
}

uint8_t vic_color_ram_read(Vic *vic, uint16_t addr) {
    return (uint8_t)(0xF0 | (vic->color_ram[addr & 0x3FF] & 0x0F));
}

void vic_color_ram_write(Vic *vic, uint16_t addr, uint8_t v) {
    vic->color_ram[addr & 0x3FF] = v & 0x0F;
}

void vic_tick(Vic *vic, int cycles) {
    /* `cycles` is always one CPU instruction's worth here (2-7) or one
     * bad-line stall's worth (VIC_BADLINE_STALL_CYCLES) - both well
     * under a full line (63), so at most one line boundary is ever
     * crossed per call. That's what makes this simple old/new
     * comparison correct without needing to walk every line
     * individually - see vic.h's header comment for both. */
    uint32_t old_line = vic->raster_cycle / PAL_CYCLES_PER_LINE;
    vic->raster_cycle = (uint32_t)((vic->raster_cycle + (uint32_t)cycles) % (PAL_CYCLES_PER_LINE * PAL_LINES_PER_FRAME));
    uint32_t new_line = vic->raster_cycle / PAL_CYCLES_PER_LINE;

    if (new_line == old_line) return;

    if (new_line == vic->raster_compare) {
        vic->regs[REG_D019] |= VIC_IRQ_RASTER;
    }

    int den = (vic->regs[REG_D011] & 0x10) != 0;
    int yscroll = vic->regs[REG_D011] & 0x07;
    if (den && new_line >= 0x30 && new_line <= 0xF7 && (int)(new_line & 0x07) == yscroll) {
        vic->badline_stall_cycles = VIC_BADLINE_STALL_CYCLES;
    }
}

uint32_t vic_take_badline_stall(Vic *vic) {
    uint32_t stall = vic->badline_stall_cycles;
    vic->badline_stall_cycles = 0;
    return stall;
}

static void fill_rect(uint32_t *pixels, int stride, int x0, int y0, int w, int h, uint32_t color) {
    for (int y = y0; y < y0 + h; y++) {
        uint32_t *row = pixels + (size_t)y * (size_t)stride;
        for (int x = x0; x < x0 + w; x++) row[x] = color;
    }
}

/* Renders one 8x8 cell's 8 pixel-data bytes (`glyph`, 8 contiguous
 * bytes - a character ROM/RAM glyph in text mode, a bitmap-memory cell
 * in bitmap mode, both addressed identically per-cell either way) at
 * (px0,py0). Shared between text and bitmap modes below since both use
 * the exact same two pixel encodings, just with different color
 * sources: plain hi-res (1 bit -> 1 pixel, `fg`/`bg`) or multicolor
 * (2-bit pairs -> 1 of `mc_colors`, each pair covering 2 real pixels -
 * multicolor halves horizontal resolution either way).
 *
 * Also stamps a 1-byte-per-pixel "graphics foreground" mask into
 * `fgmask` (same px0/py0 offsets as `pixels` since both use the same
 * canvas coordinate system, but its OWN stride - VIC_CANVAS_W, a
 * compile-time constant, NOT `pixels`'s `stride`, which can carry
 * cairo's alignment padding - see vic_render_frame()) for sprite
 * priority/collision to read later: hi-res pixel value 1, or any
 * non-zero multicolor pixel-pair value, counts as foreground - this is
 * the same real VIC-II concept sprite priority ($D01B) and sprite-
 * background collision ($D01F) are defined against, not something
 * invented for this emulator. */
static void render_cell(uint32_t *pixels, int stride, uint8_t *fgmask, int px0, int py0, const uint8_t *glyph,
                         int multicolor, const uint32_t mc_colors[4], uint32_t fg, uint32_t bg) {
    for (int line = 0; line < 8; line++) {
        uint8_t bits = glyph[line];
        uint32_t *prow = pixels + (size_t)(py0 + line) * (size_t)stride + px0;
        uint8_t *mrow = fgmask + (size_t)(py0 + line) * (size_t)VIC_CANVAS_W + px0;
        if (multicolor) {
            for (int pair = 0; pair < 4; pair++) {
                uint8_t val = (bits >> (6 - pair * 2)) & 0x03;
                uint32_t c = mc_colors[val];
                prow[pair * 2] = c;
                prow[pair * 2 + 1] = c;
                mrow[pair * 2] = mrow[pair * 2 + 1] = (uint8_t)(val != 0);
            }
        } else {
            for (int px = 0; px < 8; px++) {
                int set = (bits & (0x80 >> px)) != 0;
                prow[px] = set ? fg : bg;
                mrow[px] = (uint8_t)set;
            }
        }
    }
}

static int popcount8(uint8_t v) {
    int c = 0;
    while (v) { v &= (uint8_t)(v - 1); c++; }
    return c;
}

/* Sprite geometry read out of `vic`'s registers for sprite `n`, shared
 * between the collision pass and the draw pass below so both agree on
 * exactly the same footprint. */
typedef struct {
    int enabled, x, y, w, h, xexp, yexp, mc;
} SpriteGeom;

static SpriteGeom sprite_geom(const Vic *vic, int n) {
    SpriteGeom g;
    g.enabled = (vic->regs[REG_D015] >> n) & 1;
    int x_msb = (vic->regs[REG_D010] >> n) & 1;
    g.x = ((x_msb << 8) | vic->regs[0x00 + n * 2]) - VIC_SPRITE_X_OFFSET;
    g.y = vic->regs[0x01 + n * 2] - VIC_SPRITE_Y_OFFSET;
    g.xexp = (vic->regs[REG_D01D] >> n) & 1;
    g.yexp = (vic->regs[REG_D017] >> n) & 1;
    g.mc = (vic->regs[REG_D01C] >> n) & 1;
    g.w = VIC_SPRITE_W << g.xexp;
    g.h = VIC_SPRITE_H << g.yexp;
    return g;
}

/* Opaque/color for sprite `n` at footprint-relative (rel_x,rel_y) - the
 * caller (both the collision and draw passes below) is responsible for
 * bounds-checking against `g.w`/`g.h` first. Sprite data is 3
 * contiguous bytes (24 bits) per source row, 21 source rows, pointed
 * to by the last 8 bytes of the current video matrix (`screen_base +
 * 0x3F8 + n`, a real hardware convention, not incidental) times 64 -
 * see vic_render_frame()'s header comment. */
static int sprite_pixel(const Vic *vic, const Memory *mem, uint16_t bank_base, uint16_t screen_base,
                         int n, const SpriteGeom *g, int rel_x, int rel_y, uint32_t *out_color) {
    int src_x = rel_x >> g->xexp;   /* 0-23 */
    int src_row = rel_y >> g->yexp; /* 0-20 */

    uint8_t pointer_byte = mem->ram[(uint16_t)(screen_base + 0x3F8 + n)];
    uint16_t data_addr = (uint16_t)(bank_base + pointer_byte * 64);
    const uint8_t *row = &mem->ram[(uint16_t)(data_addr + src_row * 3)];
    uint32_t bits24 = ((uint32_t)row[0] << 16) | ((uint32_t)row[1] << 8) | row[2];

    if (g->mc) {
        int pair = src_x / 2; /* 0-11 */
        uint8_t val = (uint8_t)((bits24 >> (22 - pair * 2)) & 0x03);
        if (val == 0) return 0; /* '00' is always transparent, even in multicolor - real hardware */
        static const uint8_t which_reg[4] = {0, REG_D025, REG_D027, REG_D026}; /* index 2 (REG_D027) gets +n below */
        uint8_t reg = (val == 2) ? (uint8_t)(REG_D027 + n) : which_reg[val];
        *out_color = VIC_PALETTE[vic->regs[reg] & 0x0F];
        return 1;
    }
    int bit = (bits24 >> (23 - src_x)) & 1;
    if (!bit) return 0;
    *out_color = VIC_PALETTE[vic->regs[REG_D027 + n] & 0x0F];
    return 1;
}

/* Scratch buffers reused across frames rather than allocated fresh
 * each time (same spirit as the `static uint32_t pixels[...]` buffers
 * this project's own tests use) - `fgmask` uses the SAME dimensions
 * and (VIC_CANVAS_W) stride as `pixels` so render_cell()'s px0/py0
 * offsets apply unchanged to both; the border area of `fgmask` is
 * never written by render_cell() (it only ever touches the display
 * window), so it reads back 0 - i.e. "background" - matching real
 * hardware, where the border isn't graphics foreground data either. */
static uint8_t fgmask[VIC_CANVAS_H * VIC_CANVAS_W];
static uint8_t sprite_mask[VIC_CANVAS_H * VIC_CANVAS_W]; /* bit n = sprite n opaque here, ALL 8 sprites regardless of priority - collision detection doesn't care what's drawn on top */

void vic_render_frame(Vic *vic, const Memory *mem, uint8_t bank, uint32_t *pixels, int stride) {
    uint32_t border_color = VIC_PALETTE[vic->regs[REG_D020] & 0x0F];
    uint32_t bg_color = VIC_PALETTE[vic->regs[REG_D021] & 0x0F];
    uint32_t bg1_color = VIC_PALETTE[vic->regs[REG_D022] & 0x0F];
    uint32_t bg2_color = VIC_PALETTE[vic->regs[REG_D023] & 0x0F];
    uint32_t bg3_color = VIC_PALETTE[vic->regs[REG_D024] & 0x0F];
    int mcm = (vic->regs[REG_D016] & 0x10) != 0;
    int bmm = (vic->regs[REG_D011] & 0x20) != 0;
    int ecm = (vic->regs[REG_D011] & 0x40) != 0;

    fill_rect(pixels, stride, 0, 0, VIC_CANVAS_W, VIC_CANVAS_H, border_color);

    int den = (vic->regs[REG_D011] & 0x10) != 0;
    if (!den) {
        fill_rect(pixels, stride, VIC_BORDER_X, VIC_BORDER_Y, VIC_TEXT_COLS * 8, VIC_TEXT_ROWS * 8, border_color);
        return;
    }

    if (ecm && (mcm || bmm)) {
        /* Real VIC-II "invalid mode" combinations - ECM only ever
         * combines meaningfully with plain text mode; pairing it with
         * MCM and/or BMM is documented real hardware behavior that
         * renders the whole display window solid black (border is
         * unaffected) rather than any meaningful pixel data. */
        fill_rect(pixels, stride, VIC_BORDER_X, VIC_BORDER_Y, VIC_TEXT_COLS * 8, VIC_TEXT_ROWS * 8, VIC_PALETTE[0]);
        return;
    }

    uint16_t bank_base = (uint16_t)(bank * 0x4000);
    uint8_t mem_ptrs = vic->regs[REG_D018];
    uint16_t screen_base = (uint16_t)(bank_base + ((mem_ptrs >> 4) & 0x0F) * 0x400);

    memset(fgmask, 0, sizeof(fgmask));

    if (bmm) {
        /* Bitmap mode ($D011 bit5): $D018's bit3 alone selects the
         * bitmap's base offset within the VIC bank (bits1-2 of that
         * same field are ignored here - they only mean "character
         * pointer" in text mode). Screen RAM still uses the normal
         * screen pointer, but holds per-CELL color info instead of
         * character codes: standard bitmap mode uses its upper/lower
         * nibbles directly as that cell's two colors; multicolor
         * bitmap mode uses them as 2 of its 4 pixel-pair colors. No
         * character ROM involved in bitmap mode at all - the bitmap
         * data is addressed directly by cell position, there's no
         * character-code indirection to substitute ROM for. */
        uint16_t bitmap_base = (uint16_t)(bank_base + ((mem_ptrs & 0x08) ? 0x2000 : 0x0000));

        for (int row = 0; row < VIC_TEXT_ROWS; row++) {
            for (int col = 0; col < VIC_TEXT_COLS; col++) {
                uint16_t cell = (uint16_t)(row * VIC_TEXT_COLS + col);
                uint8_t screen_byte = mem->ram[(uint16_t)(screen_base + cell)];
                uint32_t fg_color = VIC_PALETTE[(screen_byte >> 4) & 0x0F];
                uint32_t cell_bg_color = VIC_PALETTE[screen_byte & 0x0F];
                /* Real hardware pixel-pair order for multicolor bitmap
                 * mode - NOT the same source list as multicolor text
                 * mode's (which uses $D022/$D023, not screen RAM). */
                uint32_t mc_colors[4] = {bg_color, fg_color, cell_bg_color, VIC_PALETTE[vic->color_ram[cell] & 0x0F]};

                const uint8_t *bmp = &mem->ram[(uint16_t)(bitmap_base + cell * 8)];
                render_cell(pixels, stride, fgmask, VIC_BORDER_X + col * 8, VIC_BORDER_Y + row * 8,
                            bmp, mcm, mc_colors, fg_color, cell_bg_color);
            }
        }
    } else {
        uint8_t char_ptr_bits = (uint8_t)((mem_ptrs >> 1) & 0x07);
        uint16_t char_base = (uint16_t)(bank_base + char_ptr_bits * 0x800);

        /* Real hardware quirk: the character ROM is only ever visible to
         * the VIC (never the CPU) when its selected char pointer lands on
         * offset $1000/$1800 within VIC bank 0 or bank 2 specifically -
         * not banks 1 or 3, since the ROM chip's own select line is
         * physically wired to just those two banks' address decoding. */
        int char_rom_visible = (bank == 0 || bank == 2) && (char_ptr_bits == 2 || char_ptr_bits == 3);
        uint16_t char_rom_offset = (uint16_t)((char_ptr_bits - 2) * 0x800); /* only meaningful when char_rom_visible */

        for (int row = 0; row < VIC_TEXT_ROWS; row++) {
            for (int col = 0; col < VIC_TEXT_COLS; col++) {
                uint16_t cell = (uint16_t)(row * VIC_TEXT_COLS + col);
                uint8_t char_code = mem->ram[(uint16_t)(screen_base + cell)];
                uint8_t color_val = vic->color_ram[cell] & 0x0F;

                /* Real hardware "mixed mode": with MCM off, every cell is
                 * plain hi-res using the full 4-bit color RAM value. With
                 * MCM on, color RAM bit3 is repurposed as a per-CELL mode
                 * flag instead of part of the color - bit3=0 still renders
                 * that one cell in plain hi-res (masked to 3 bits, colors
                 * 0-7 only); bit3=1 renders it as true 4-color multicolor
                 * instead, at half horizontal resolution. Both cases use
                 * the same masked 3-bit value as this cell's foreground
                 * (also multicolor's "11" pixel-pair color), which is why
                 * one expression covers all three. ecm implies mcm is off
                 * here (the ecm+mcm combination was already handled as an
                 * invalid mode above), so cell_multicolor is always 0 and
                 * fg_color always the full nibble when ecm is set. */
                int cell_multicolor = mcm && (color_val & 0x08);
                uint32_t fg_color = VIC_PALETTE[mcm ? (color_val & 0x07) : color_val];
                uint32_t mc_colors[4] = {bg_color, bg1_color, bg2_color, fg_color};

                /* Extended background color mode ($D011 bit6): the
                 * character code's top 2 bits pick one of 4 background
                 * colors ($D021-$D024) instead of contributing to which
                 * glyph is shown - only the low 6 bits address character
                 * memory, so only 64 distinct characters are reachable.
                 * Real hardware behavior, not a bug. */
                uint32_t cell_bg_color = bg_color;
                uint8_t glyph_code = char_code;
                if (ecm) {
                    uint32_t ecm_bg_colors[4] = {bg_color, bg1_color, bg2_color, bg3_color};
                    cell_bg_color = ecm_bg_colors[(char_code >> 6) & 0x03];
                    glyph_code = char_code & 0x3F;
                }

                const uint8_t *glyph = char_rom_visible
                    ? &mem->char_rom[(uint16_t)(char_rom_offset + glyph_code * 8)]
                    : &mem->ram[(uint16_t)(char_base + glyph_code * 8)];
                render_cell(pixels, stride, fgmask, VIC_BORDER_X + col * 8, VIC_BORDER_Y + row * 8,
                            glyph, cell_multicolor, mc_colors, fg_color, cell_bg_color);
            }
        }
    }

    /* Sprites (../ROADMAP.md step 6): 8 independent hardware sprites,
     * each up to 24x21 pixels (2x in either/both dimensions via
     * $D01D/$D017), positioned by $D000-$D00F (X/Y pairs) plus $D010's
     * X MSBs, drawn on top of the text/bitmap graphics above and
     * composited here in two passes:
     *
     * PASS 1 (collision): every enabled sprite's opaque footprint is
     * OR'd into `sprite_mask` (bit n = sprite n), regardless of
     * priority - collision detection doesn't care what ends up
     * visually on top. Any pixel with 2+ bits set is a sprite-sprite
     * collision (all bits at that pixel go into $D01E); any pixel
     * that's also set in `fgmask` is a sprite-background collision
     * (into $D01F) - "background" here means real VIC-II's actual
     * definition, graphics FOREGROUND pixels specifically (see
     * render_cell()'s comment), not the border or a cell's background
     * color, which is why the border area (always 0 in `fgmask`)
     * never triggers one. Both registers OR new hits in rather than
     * overwrite, since only an explicit read clears them (vic_read()),
     * and this whole detection pass only runs once per frame rather
     * than continuously.
     *
     * PASS 2 (draw): sprites are drawn in priority order, lowest
     * priority first (sprite 7) up to highest last (sprite 0 - real
     * hardware: lower sprite number wins), so a higher-priority
     * sprite's opaque pixels simply overwrite a lower-priority
     * sprite's at the same spot. Each sprite's own $D01B bit picks
     * whether it's always on top (0) or hidden specifically where
     * `fgmask` is set (1, "behind graphics foreground, in front of
     * background") - real hardware sprite-to-graphics priority, not
     * sprite-to-sprite (that's just draw order).
     *
     * Both passes share sprite_geom()/sprite_pixel() so they always
     * agree on a sprite's footprint and pixel data. */
    memset(sprite_mask, 0, sizeof(sprite_mask));
    for (int n = 0; n < 8; n++) {
        SpriteGeom g = sprite_geom(vic, n);
        if (!g.enabled) continue;
        for (int ry = 0; ry < g.h; ry++) {
            int py = g.y + ry;
            if (py < 0 || py >= VIC_CANVAS_H) continue;
            for (int rx = 0; rx < g.w; rx++) {
                int px = g.x + rx;
                if (px < 0 || px >= VIC_CANVAS_W) continue;
                uint32_t color;
                if (sprite_pixel(vic, mem, bank_base, screen_base, n, &g, rx, ry, &color)) {
                    sprite_mask[py * VIC_CANVAS_W + px] |= (uint8_t)(1u << n);
                }
            }
        }
    }
    uint8_t sprite_collision_bits = 0, bg_collision_bits = 0;
    for (int i = 0; i < VIC_CANVAS_H * VIC_CANVAS_W; i++) {
        uint8_t m = sprite_mask[i];
        if (m == 0) continue;
        if (popcount8(m) >= 2) sprite_collision_bits |= m;
        if (fgmask[i]) bg_collision_bits |= m;
    }
    if (sprite_collision_bits) {
        vic->regs[REG_D01E] |= sprite_collision_bits;
        vic->regs[REG_D019] |= VIC_IRQ_SPRITE_SPRITE;
    }
    if (bg_collision_bits) {
        vic->regs[REG_D01F] |= bg_collision_bits;
        vic->regs[REG_D019] |= VIC_IRQ_SPRITE_BG;
    }

    for (int n = 7; n >= 0; n--) {
        SpriteGeom g = sprite_geom(vic, n);
        if (!g.enabled) continue;
        int behind = (vic->regs[REG_D01B] >> n) & 1;
        for (int ry = 0; ry < g.h; ry++) {
            int py = g.y + ry;
            if (py < 0 || py >= VIC_CANVAS_H) continue;
            for (int rx = 0; rx < g.w; rx++) {
                int px = g.x + rx;
                if (px < 0 || px >= VIC_CANVAS_W) continue;
                uint32_t color;
                if (!sprite_pixel(vic, mem, bank_base, screen_base, n, &g, rx, ry, &color)) continue;
                if (behind && fgmask[py * VIC_CANVAS_W + px]) continue;
                pixels[(size_t)py * (size_t)stride + px] = color;
            }
        }
    }
}
