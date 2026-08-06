/*
 * machine.c - see machine.h's header comment. The keyboard/joystick
 * pin-pulldown model implemented here (update_keyboard_pins) mirrors
 * real C64 wiring: CIA1 PRA drives keyboard columns (and doubles as
 * joystick port 2), CIA1 PRB reads keyboard rows (and doubles as
 * joystick port 1) - a column/row pin only reads back low if it's
 * currently configured as an input AND something external is pulling
 * it low (a held key at that matrix position with its column/row
 * currently driven low by the other port, or a held joystick
 * direction/fire). Computed in both directions (PRA as input reading
 * columns pulled low by PRB-driven rows, and PRB as input reading rows
 * pulled low by PRA-driven columns) since real software occasionally
 * scans in either direction.
 */

#include "machine.h"
#include <stdio.h>
#include <string.h>

static void update_keyboard_pins(Machine *m) {
    uint8_t col_drive = (uint8_t)((m->cia1.pra & m->cia1.ddra) | (uint8_t)~m->cia1.ddra);
    uint8_t row_drive = (uint8_t)((m->cia1.prb & m->cia1.ddrb) | (uint8_t)~m->cia1.ddrb);

    uint8_t row_pulldown = 0;
    for (int c = 0; c < 8; c++) {
        if (!(col_drive & (1u << c))) row_pulldown |= m->key_matrix[c];
    }

    uint8_t col_pulldown = 0;
    for (int r = 0; r < 8; r++) {
        if (!(row_drive & (1u << r))) {
            for (int c = 0; c < 8; c++) {
                if (m->key_matrix[c] & (1u << r)) col_pulldown |= (uint8_t)(1u << c);
            }
        }
    }

    m->cia1.portb_in = (uint8_t)(~row_pulldown) & m->joystick1;
    m->cia1.porta_in = (uint8_t)(~col_pulldown) & m->joystick2;
}

static uint8_t io_read(void *ctx, uint16_t addr) {
    Machine *m = ctx;
    if (addr >= 0xD000 && addr <= 0xD3FF) {
        return vic_read(&m->vic, (uint8_t)addr);
    }
    if (addr >= 0xD800 && addr <= 0xDBFF) {
        return vic_color_ram_read(&m->vic, addr);
    }
    if (addr >= 0xDC00 && addr <= 0xDCFF) {
        update_keyboard_pins(m);
        return cia_read(&m->cia1, (uint8_t)addr);
    }
    if (addr >= 0xDD00 && addr <= 0xDDFF) {
        return cia_read(&m->cia2, (uint8_t)addr);
    }
    if (addr >= 0xD400 && addr <= 0xD7FF) {
        return sid_read(&m->sid, (uint8_t)addr);
    }
    return 0xFF; /* cartridge I/O - not implemented */
}

static void io_write(void *ctx, uint16_t addr, uint8_t v) {
    Machine *m = ctx;
    if (addr >= 0xD000 && addr <= 0xD3FF) {
        vic_write(&m->vic, (uint8_t)addr, v);
        return;
    }
    if (addr >= 0xD800 && addr <= 0xDBFF) {
        vic_color_ram_write(&m->vic, addr, v);
        return;
    }
    if (addr >= 0xDC00 && addr <= 0xDCFF) {
        cia_write(&m->cia1, (uint8_t)addr, v);
        update_keyboard_pins(m); /* an output latch may have just changed - keep pins consistent for an immediate follow-up read */
        return;
    }
    if (addr >= 0xDD00 && addr <= 0xDDFF) {
        cia_write(&m->cia2, (uint8_t)addr, v);
        return;
    }
    if (addr >= 0xD400 && addr <= 0xD7FF) {
        sid_write(&m->sid, (uint8_t)addr, v);
        return;
    }
    /* cartridge I/O - not implemented, write dropped */
}

void machine_init(Machine *m) {
    /* Covers m->cpu, which nothing else here zeroes explicitly -
     * cpu_reset() (called separately, after this) only sets
     * sp/p/pc/nmi_pending, leaving a/x/y/cycles/irq_line as whatever
     * this memory happened to hold otherwise. The sub-inits below are
     * redundant with this for mem/cia1/cia2/vic (each already zeroes
     * itself) but kept for clarity and so they still work correctly if
     * this line is ever removed. */
    memset(m, 0, sizeof(*m));
    memory_init(&m->mem);
    cia_init(&m->cia1);
    cia_init(&m->cia2);
    vic_init(&m->vic);
    sid_init(&m->sid);
    memset(m->key_matrix, 0, sizeof(m->key_matrix));
    m->joystick1 = 0xFF;
    m->joystick2 = 0xFF;
    /* CIA2's PRA/PRB inputs are the serial bus and user port, neither
     * of which is modeled - nothing external ever pulls them low. */
    m->cia2.porta_in = 0xFF;
    m->cia2.portb_in = 0xFF;
    m->cia2_nmi_prev = 0;

    m->mem.io.read = io_read;
    m->mem.io.write = io_write;
    m->mem.io.ctx = m;

    m->cpu.bus = memory_cpu_bus(&m->mem);
}

int machine_load_roms(Machine *m, const char *dir) {
    return memory_load_roms(&m->mem, dir);
}

uint16_t machine_load_prg(Machine *m, const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;

    uint8_t header[2];
    if (fread(header, 1, 2, f) != 2) {
        fclose(f);
        return 0;
    }
    uint16_t load_addr = (uint16_t)(header[0] | (header[1] << 8));

    uint16_t addr = load_addr;
    int byte;
    while ((byte = fgetc(f)) != EOF) {
        memory_write(&m->mem, addr, (uint8_t)byte);
        addr++; /* wraps $FFFF -> $0000, matching real address-space wraparound - a file long enough to hit it would corrupt itself the same way on real hardware */
    }
    fclose(f);
    return load_addr;
}

uint16_t machine_find_sys_target(const Machine *m, uint16_t load_addr) {
    if (load_addr != 0x0801) return 0;

    /* Stub layout after the load address: [next-line ptr, 2 bytes]
     * [line number, 2 bytes][$9E "SYS" token][ASCII digits...][$00] -
     * see machine_find_sys_target()'s own doc comment in machine.h. */
    uint16_t idx = (uint16_t)(load_addr + 4);
    if (m->mem.ram[idx] != 0x9E) return 0;
    idx++;

    uint16_t target = 0;
    int saw_digit = 0;
    while (m->mem.ram[idx] != 0x00) {
        uint8_t c = m->mem.ram[idx];
        if (c < '0' || c > '9') return 0;
        target = (uint16_t)(target * 10 + (c - '0'));
        saw_digit = 1;
        idx++;
    }
    return saw_digit ? target : 0;
}

void machine_reset(Machine *m) {
    cpu_reset(&m->cpu);
}

static void update_interrupt_lines(Machine *m) {
    /* CIA1 and VIC-II raster IRQs share the CPU's /IRQ line on real
     * hardware (wired-OR) - level-triggered, so this can just be
     * recomputed fresh every call rather than tracked incrementally. */
    m->cpu.irq_line = cia_irq_line(&m->cia1) || vic_irq_line(&m->vic);

    /* CIA2 -> /NMI (edge-triggered, per cpu_nmi()'s own contract). */
    int nmi_now = cia_irq_line(&m->cia2);
    if (nmi_now && !m->cia2_nmi_prev) cpu_nmi(&m->cpu);
    m->cia2_nmi_prev = nmi_now;
}

int machine_step(Machine *m) {
    /* A bad line freezes the CPU (real /RDY behavior - see
     * vic_take_badline_stall()'s header comment) - if one is pending,
     * this call makes zero CPU progress: only the CIAs/VIC tick. */
    uint32_t stall = vic_take_badline_stall(&m->vic);
    if (stall > 0) {
        cia_tick(&m->cia1, (int)stall);
        cia_tick(&m->cia2, (int)stall);
        vic_tick(&m->vic, (int)stall);
        sid_tick(&m->sid, (int)stall);
        update_interrupt_lines(m);
        return (int)stall;
    }

    int cycles = cpu_step(&m->cpu);
    cia_tick(&m->cia1, cycles);
    cia_tick(&m->cia2, cycles);
    vic_tick(&m->vic, cycles);
    sid_tick(&m->sid, cycles);
    update_interrupt_lines(m);
    return cycles;
}

void machine_set_key(Machine *m, int pa_bit, int pb_bit, int pressed) {
    if (pa_bit < 0 || pa_bit > 7 || pb_bit < 0 || pb_bit > 7) return;
    uint8_t bit = (uint8_t)(1u << pb_bit);
    if (pressed) m->key_matrix[pa_bit] |= bit;
    else m->key_matrix[pa_bit] &= (uint8_t)~bit;
}

uint8_t machine_vic_bank(Machine *m) {
    uint8_t pra = cia_read(&m->cia2, 0x00);
    return (uint8_t)((~pra) & 0x03);
}

void machine_set_joystick(Machine *m, int port, uint8_t bits) {
    uint8_t v = (uint8_t)(bits | 0xE0); /* bits 5-7 always 1 - see machine.h */
    if (port == 1) m->joystick1 = v;
    else if (port == 2) m->joystick2 = v;
}
