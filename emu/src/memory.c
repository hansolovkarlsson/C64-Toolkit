/*
 * memory.c - the C64's 64K address space, plus the 6510's built-in
 * bank switching: $A000-$BFFF (BASIC ROM), $D000-$DFFF (character ROM
 * or I/O), and $E000-$FFFF (KERNAL ROM) can each show either ROM/I-O
 * or plain RAM, controlled by the LORAM/HIRAM/CHAREN bits of the I/O
 * port at $0001 (with its data-direction register at $0000). Assumes
 * no cartridge is plugged in (EXROM/GAME both held high, the
 * no-cartridge state) - the full mode table has 32 entries once
 * cartridge lines are involved; this only implements the 8 relevant
 * to a stock machine. See the "Mode Table" at
 * https://www.c64-wiki.com/wiki/Bank_Switching for the complete table
 * this was cross-checked against.
 *
 * WRITE UNDER ROM: a real 6502 write instruction still lands in RAM
 * even when ROM is what's currently visible for READS at that same
 * address - the ROM chip has no write pin, so only the RAM chip
 * responds. This is why memory_write() below never special-cases
 * $A000-$BFFF/$E000-$FFFF/$D000-$DFFF-as-char-ROM at all: it just
 * always writes mem->ram[addr], and the bank-switch logic only
 * affects memory_read(). The one real exception is $D000-$DFFF when
 * I/O is banked in - a write there is claimed by the I/O device
 * itself (its chip-select excludes the RAM chip for that cycle), not
 * silently absorbed into RAM underneath. See
 * https://www.c64-wiki.com/wiki/Memory_Map's "ROM vs RAM" section.
 *
 * SIMPLIFICATION: a real floating (DDR-configured-as-input) port bit
 * has real analog behavior (it retains whatever it was last driven to
 * for a while, then decays) that this doesn't model - an input bit
 * here just always reads as 1. Practically irrelevant: essentially no
 * real software ever reconfigures LORAM/HIRAM/CHAREN as inputs in the
 * first place. Worth revisiting only if something concrete turns out
 * to need it - see ../ROADMAP.md's "cycle-exact fidelity" note.
 */

#include "memory.h"
#include <stdio.h>
#include <string.h>

void memory_init(Memory *mem) {
    memset(mem, 0, sizeof(*mem));
    mem->port_ddr = 0x2F;  /* real C64 power-on default */
    mem->port_data = 0x37; /* LORAM=HIRAM=CHAREN=1 - the normal configuration */
}

static int load_one(const char *dir, const char *name, uint8_t *buf, size_t size) {
    char path[1024];
    snprintf(path, sizeof(path), "%s/%s", dir, name);
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    size_t n = fread(buf, 1, size, f);
    fclose(f);
    return n == size;
}

int memory_load_roms(Memory *mem, const char *dir) {
    mem->has_kernal = load_one(dir, "kernal.rom", mem->kernal_rom, sizeof(mem->kernal_rom));
    mem->has_basic = load_one(dir, "basic.rom", mem->basic_rom, sizeof(mem->basic_rom));
    mem->has_char = load_one(dir, "chargen.rom", mem->char_rom, sizeof(mem->char_rom));
    return mem->has_kernal + mem->has_basic + mem->has_char;
}

/* Per-bit: if configured as output (ddr=1), the effective value is
 * whatever was last written; if input (ddr=0), it reads as 1 (see
 * this file's header comment on why that simplification is fine). */
static uint8_t effective_port(const Memory *mem) {
    return (uint8_t)((mem->port_data & mem->port_ddr) | (uint8_t)(~mem->port_ddr));
}

/* $D000-$DFFF's mode, per the mode table this file's header cites:
 * RAM whenever LORAM and HIRAM are BOTH clear (CHAREN doesn't matter
 * at all in that case); otherwise CHAREN alone picks I/O vs char ROM. */
enum { D000_RAM, D000_IO, D000_CHARROM };
static int d000_mode(int loram, int hiram, int charen) {
    if (!loram && !hiram) return D000_RAM;
    return charen ? D000_IO : D000_CHARROM;
}

uint8_t memory_read(Memory *mem, uint16_t addr) {
    if (addr == 0x0000) return mem->port_ddr;
    if (addr == 0x0001) return effective_port(mem);

    uint8_t port = effective_port(mem);
    int loram = (port & 0x01) != 0;
    int hiram = (port & 0x02) != 0;
    int charen = (port & 0x04) != 0;

    /* BASIC ROM needs BOTH LORAM and HIRAM set - LORAM alone is NOT
     * enough (a common misreading of the mode table; verified against
     * it directly, see this file's header comment). */
    if (addr >= 0xA000 && addr <= 0xBFFF && loram && hiram)
        return mem->basic_rom[addr - 0xA000];

    if (addr >= 0xE000 && hiram)
        return mem->kernal_rom[addr - 0xE000];

    if (addr >= 0xD000 && addr <= 0xDFFF) {
        switch (d000_mode(loram, hiram, charen)) {
            case D000_CHARROM: return mem->char_rom[addr - 0xD000];
            case D000_IO:
                return mem->io.read ? mem->io.read(mem->io.ctx, addr) : 0xFF;
            default: break; /* D000_RAM falls through to the plain RAM read below */
        }
    }

    return mem->ram[addr];
}

void memory_write(Memory *mem, uint16_t addr, uint8_t v) {
    if (addr == 0x0000) { mem->port_ddr = v; return; }
    if (addr == 0x0001) { mem->port_data = v; return; }

    if (addr >= 0xD000 && addr <= 0xDFFF) {
        uint8_t port = effective_port(mem);
        int loram = (port & 0x01) != 0;
        int hiram = (port & 0x02) != 0;
        int charen = (port & 0x04) != 0;
        if (d000_mode(loram, hiram, charen) == D000_IO) {
            if (mem->io.write) mem->io.write(mem->io.ctx, addr, v);
            return; /* I/O claims the write - RAM underneath is untouched, unlike the ROM regions below */
        }
    }

    /* Every other address, including $A000-$BFFF/$E000-$FFFF/
     * $D000-$DFFF-as-char-ROM even while ROM is visible for reads -
     * "write under ROM," see this file's header comment. */
    mem->ram[addr] = v;
}

static uint8_t cpu_bus_read(void *ctx, uint16_t addr) { return memory_read((Memory *)ctx, addr); }
static void cpu_bus_write(void *ctx, uint16_t addr, uint8_t v) { memory_write((Memory *)ctx, addr, v); }

CpuBus memory_cpu_bus(Memory *mem) {
    CpuBus bus;
    bus.read = cpu_bus_read;
    bus.write = cpu_bus_write;
    bus.ctx = mem;
    return bus;
}
