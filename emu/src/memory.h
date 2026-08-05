#ifndef C64EMU_MEMORY_H
#define C64EMU_MEMORY_H

#include <stdint.h>
#include "cpu.h"

/* VIC-II/SID/CIA/color RAM will each claim their own sub-range of
 * $D000-$DFFF once they exist (steps 4-6 of ../ROADMAP.md) by
 * registering an IoBus here. Until then, io.read/io.write are NULL
 * and memory_read()/memory_write() fall back to treating $D000-$DFFF
 * as inert whenever I/O is banked in (reads return $FF, writes are
 * dropped) - a placeholder, not real hardware behavior. */
typedef struct {
    uint8_t (*read)(void *ctx, uint16_t addr);
    void (*write)(void *ctx, uint16_t addr, uint8_t value);
    void *ctx;
} IoBus;

typedef struct {
    uint8_t ram[65536];        /* the real, physical, always-present RAM - see memory.c's header comment on "write under ROM" */
    uint8_t basic_rom[8192];   /* $A000-$BFFF when banked in */
    uint8_t kernal_rom[8192];  /* $E000-$FFFF when banked in */
    uint8_t char_rom[4096];    /* $D000-$DFFF when banked in (and I/O isn't) */
    int has_basic, has_kernal, has_char; /* whether memory_load_roms() actually found each file */
    uint8_t port_ddr;  /* $0000 - 6510 I/O port data direction register (1 bit = output) */
    uint8_t port_data; /* $0001 - 6510 I/O port data register */
    IoBus io;
} Memory;

/* Zeros all RAM/ROM and sets port_ddr/port_data to the real C64's
 * power-on defaults ($2F/$37 - LORAM=HIRAM=CHAREN=1, the normal
 * BASIC+KERNAL+I/O configuration). */
void memory_init(Memory *mem);

/* Loads <dir>/kernal.rom, <dir>/basic.rom, <dir>/chargen.rom (see
 * ../roms/README.md for expected sizes) into the matching buffers
 * above. A missing file is not fatal - has_basic/has_kernal/has_char
 * record what actually loaded, so a caller (or test) can decide
 * whether to proceed without it. Returns how many of the 3 loaded
 * successfully (0-3). */
int memory_load_roms(Memory *mem, const char *dir);

/* Wires mem's read/write into the shape cpu.c expects. */
CpuBus memory_cpu_bus(Memory *mem);

/* Exposed directly (not just through the CpuBus wrapper) since a GTK
 * front end or test harness will want to peek/poke memory without
 * going through the CPU - e.g. to inspect screen RAM for a frame, or
 * to set up a test fixture. */
uint8_t memory_read(Memory *mem, uint16_t addr);
void memory_write(Memory *mem, uint16_t addr, uint8_t value);

#endif
