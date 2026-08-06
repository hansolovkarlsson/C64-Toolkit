/*
 * vic.c - see vic.h's header comment for scope (text mode + raster
 * IRQs) and what's deliberately deferred.
 */

#include "vic.h"
#include <string.h>

enum {
    REG_D011 = 0x11, /* control register 1: bit7=raster MSB (read)/compare MSB (write), bit4=DEN, bit3=RSEL (not modeled), bits0-2=YSCROLL (not modeled) */
    REG_D012 = 0x12, /* raster (read: live low 8 bits; write: compare low 8 bits) */
    REG_D016 = 0x16, /* control register 2: MCM/CSEL/XSCROLL - stored only, not modeled */
    REG_D018 = 0x18, /* memory pointers: bits4-7=screen pointer (1K units), bits1-3=char/bitmap pointer (2K units) */
    REG_D019 = 0x19, /* IRQ status: bits0-3 pending (bit0=raster, bits1-3=sprite collisions/light pen - never set, not modeled), bit7=read-only summary. Write CLEARS whichever of bits0-3 are 1 in the value written - see vic_write(). */
    REG_D01A = 0x1A, /* IRQ enable: bits0-3, plain read/write */
    REG_D020 = 0x20, /* border color (low nibble) */
    REG_D021 = 0x21, /* background color 0 (low nibble) */
};

#define VIC_IRQ_RASTER 0x01

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
    /* `cycles` is always one CPU instruction's worth here (2-7, see
     * machine_step()) - always less than a full line (63), so at most
     * one line boundary is ever crossed per call. That's what makes
     * this simple old/new comparison correct without needing to walk
     * every line individually. */
    uint32_t old_line = vic->raster_cycle / PAL_CYCLES_PER_LINE;
    vic->raster_cycle = (uint32_t)((vic->raster_cycle + (uint32_t)cycles) % (PAL_CYCLES_PER_LINE * PAL_LINES_PER_FRAME));
    uint32_t new_line = vic->raster_cycle / PAL_CYCLES_PER_LINE;

    if (new_line != old_line && new_line == vic->raster_compare) {
        vic->regs[REG_D019] |= VIC_IRQ_RASTER;
    }
}

static void fill_rect(uint32_t *pixels, int stride, int x0, int y0, int w, int h, uint32_t color) {
    for (int y = y0; y < y0 + h; y++) {
        uint32_t *row = pixels + (size_t)y * (size_t)stride;
        for (int x = x0; x < x0 + w; x++) row[x] = color;
    }
}

void vic_render_frame(Vic *vic, const Memory *mem, uint8_t bank, uint32_t *pixels, int stride) {
    uint32_t border_color = VIC_PALETTE[vic->regs[REG_D020] & 0x0F];
    uint32_t bg_color = VIC_PALETTE[vic->regs[REG_D021] & 0x0F];

    fill_rect(pixels, stride, 0, 0, VIC_CANVAS_W, VIC_CANVAS_H, border_color);

    int den = (vic->regs[REG_D011] & 0x10) != 0;
    if (!den) {
        fill_rect(pixels, stride, VIC_BORDER_X, VIC_BORDER_Y, VIC_TEXT_COLS * 8, VIC_TEXT_ROWS * 8, border_color);
        return;
    }

    uint16_t bank_base = (uint16_t)(bank * 0x4000);
    uint8_t mem_ptrs = vic->regs[REG_D018];
    uint16_t screen_base = (uint16_t)(bank_base + ((mem_ptrs >> 4) & 0x0F) * 0x400);
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
            uint32_t fg_color = VIC_PALETTE[vic->color_ram[cell] & 0x0F];

            for (int line = 0; line < 8; line++) {
                uint8_t bits = char_rom_visible
                    ? mem->char_rom[(uint16_t)(char_rom_offset + char_code * 8 + line)]
                    : mem->ram[(uint16_t)(char_base + char_code * 8 + line)];

                int py = VIC_BORDER_Y + row * 8 + line;
                uint32_t *prow = pixels + (size_t)py * (size_t)stride + VIC_BORDER_X + col * 8;
                for (int px = 0; px < 8; px++) {
                    prow[px] = (bits & (0x80 >> px)) ? fg_color : bg_color;
                }
            }
        }
    }
}
