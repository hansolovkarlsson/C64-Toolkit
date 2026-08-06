/*
 * test_boot.c - an end-to-end boot test: loads real, open-source
 * KERNAL/BASIC/character ROMs (see README.md for where they come from
 * and why they're safe to fetch, unlike Commodore's own copyrighted
 * ROMs - ../../roms/README.md) and runs the whole machine until
 * "READY." appears in screen RAM, the same thing a real C64 prints
 * once BASIC has finished initializing and is waiting for input.
 *
 * This is the project's end-to-end correctness gate: the CPU, memory
 * map, both CIAs, and VIC-II all working together against real,
 * unmodified third-party system software - not just each module's own
 * hand-derived unit tests (../cpu/, ../memory/, ../cia/, ../machine/,
 * ../vic/), which can't catch a bug that only shows up when every
 * piece interacts (this is exactly how the VIC-II raster counter's
 * absence was first noticed - see emu/ROADMAP.md's step 5 entry).
 */

#include "../../src/machine.h"
#include <stdio.h>

/* "READY." in C64 screen codes, NOT PETSCII/ASCII (screen RAM stores
 * screen codes - see c64-memory-reference.md if this ever needs
 * extending): R=$12 E=$05 A=$01 D=$04 Y=$19 .=$2E */
static const uint8_t READY_SCREEN_CODES[] = {0x12, 0x05, 0x01, 0x04, 0x19, 0x2E};

static int screen_has_ready(Machine *m) {
    uint8_t buf[1000];
    for (int i = 0; i < 1000; i++) buf[i] = memory_read(&m->mem, (uint16_t)(0x0400 + i));
    for (int i = 0; i <= 1000 - 6; i++) {
        int match = 1;
        for (int j = 0; j < 6; j++) {
            if (buf[i + j] != READY_SCREEN_CODES[j]) { match = 0; break; }
        }
        if (match) return 1;
    }
    return 0;
}

#define CHECK_INTERVAL_CYCLES 10000
/* READY. appears around cycle 200,000 against the MEGA65 open-roms
 * build this was verified with (see README.md) - this budget gives a
 * comfortable 10x margin for a slower boot path or a different ROM
 * revision before calling it a real failure. */
#define MAX_CYCLES 2000000

int main(void) {
    Machine m;
    machine_init(&m);

    int loaded = machine_load_roms(&m, "roms");
    if (loaded != 3) {
        fprintf(stderr, "FAIL: expected 3/3 ROMs to load from roms/ (got %d) - run 'make fetch' first (see README.md)\n", loaded);
        return 2;
    }
    machine_reset(&m);

    long total = 0;
    while (total < MAX_CYCLES) {
        long batch = 0;
        while (batch < CHECK_INTERVAL_CYCLES && total < MAX_CYCLES) {
            int c = machine_step(&m);
            batch += c;
            total += c;
        }
        if (screen_has_ready(&m)) {
            printf("PASS: booted to READY. after %ld cycles\n", total);
            return 0;
        }
    }

    fprintf(stderr, "FAIL: never reached READY. within %d cycles (PC=$%04X, A=$%02X, P=$%02X)\n",
            MAX_CYCLES, m.cpu.pc, m.cpu.a, m.cpu.p);
    return 1;
}
