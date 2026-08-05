/*
 * Runs Klaus Dormann's 6502 functional test suite (see README.md in
 * this directory for where to get it - not vendored into this repo)
 * against cpu.c, using a flat 64K RAM array as the bus - no
 * bank-switching, no ROM overlay, since the test image is a single
 * self-contained 64K memory dump that expects plain RAM everywhere.
 *
 * The test is entered directly at $0400 (not through the reset
 * vector - see the suite's own source comments: a real RESET during
 * the test is itself something the suite traps as a bug). Every trap
 * in the suite, including the final "all tests passed" state, is a
 * `JMP *` (jump to its own address) - so completion is detected by
 * noticing the PC stopped advancing, and success is specifically
 * $3469 (this build's address for the passed-trap - see this
 * directory's README.md for how that was found, and how to re-derive
 * it if a different suite revision ever changes it).
 */

#include "../../src/cpu.h"
#include <stdio.h>
#include <string.h>

static uint8_t ram[65536];

static uint8_t bus_read(void *ctx, uint16_t addr) { (void)ctx; return ram[addr]; }
static void bus_write(void *ctx, uint16_t addr, uint8_t v) { (void)ctx; ram[addr] = v; }

#define SUCCESS_PC 0x3469
#define ENTRY_PC 0x0400
#define TEST_CASE_ADDR 0x0200
#define MAX_CYCLES 200000000ULL /* generous ceiling well above what the suite needs, so a real infinite-loop bug can't hang a test run forever */

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s <6502_functional_test.bin>\n", argv[0]);
        return 2;
    }
    FILE *f = fopen(argv[1], "rb");
    if (!f) { perror("fopen"); return 2; }
    size_t n = fread(ram, 1, sizeof(ram), f);
    fclose(f);
    if (n != sizeof(ram)) {
        fprintf(stderr, "expected a 65536-byte flat memory image, got %zu bytes\n", n);
        return 2;
    }

    Cpu6502 cpu;
    memset(&cpu, 0, sizeof(cpu));
    cpu.bus.read = bus_read;
    cpu.bus.write = bus_write;
    cpu.pc = ENTRY_PC;

    while (cpu.cycles < MAX_CYCLES) {
        uint16_t pc_before = cpu.pc;
        cpu_step(&cpu);
        if (cpu.pc == pc_before) {
            if (cpu.pc == SUCCESS_PC) {
                printf("PASS: all tests completed (%llu cycles)\n", (unsigned long long)cpu.cycles);
                return 0;
            }
            fprintf(stderr, "FAIL: trapped at $%04X (test_case=$%02X, %llu cycles)\n",
                    cpu.pc, ram[TEST_CASE_ADDR], (unsigned long long)cpu.cycles);
            return 1;
        }
    }
    fprintf(stderr, "FAIL: did not complete or trap within %llu cycles (stuck around $%04X)\n",
            (unsigned long long)MAX_CYCLES, cpu.pc);
    return 1;
}
