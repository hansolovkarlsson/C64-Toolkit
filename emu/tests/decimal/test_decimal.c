/*
 * Decimal-mode ADC/SBC verification, exhaustive.
 *
 * Dormann's suite (../cpu/) runs every legal opcode and addressing mode,
 * but its own source says this about the one area it stops short of:
 *
 *     ; decimal add/subtract test
 *     ; *** WARNING - tests documented behavior only! ***
 *     ;   only valid BCD operands are tested, N V Z flags are ignored
 *
 * and its checking code confirms it, comparing the result byte and then
 * `and #1  ;mask carry`. So the accumulator and C are covered there and
 * N, V and Z in decimal mode are not, on either instruction, and invalid
 * BCD operands are never presented at all.
 *
 * That is exactly the part of cpu.c with the most intricate logic in it:
 * op_adc()'s N and V come from an UNCORRECTED intermediate rather than
 * the final BCD-adjusted accumulator, its Z comes from a plain binary
 * addition ignoring decimal mode entirely, and op_sbc()'s N/V/Z/C are
 * all binary even with D set. Every one of those derivations was
 * unverified before this file existed.
 *
 * WHAT THIS DOES. For ADC and SBC, immediate and zero page, it walks all
 * 256 accumulator values against all 256 operand values against both
 * carry-in values, executing a real instruction through cpu_step() and
 * the normal CpuBus rather than reaching into cpu.c, and compares A, N,
 * V, Z and C against a predicted value. 524,288 cases.
 *
 * WHERE THE EXPECTATIONS COME FROM, which is the part that matters. The
 * predictor below is a port of Bruce Clark's 6502_decimal_test.a65
 * (public domain, http://www.6502.org/tutorials/decimal_mode.html) with
 * its cputype = 0 flag semantics: for ADC, NF and VF are taken from the
 * P register captured immediately after the high-nibble add and BEFORE
 * the $60 correction, while ZF is taken from the binary add; for SBC,
 * NF, VF, ZF and CF are all taken from the binary subtraction. Note that
 * Clark's own test ships with chk_n, chk_v and chk_z set to 0, so
 * building it unmodified would not have closed this gap either.
 *
 * A predictor written from the same understanding that produced cpu.c
 * would happily agree with a bug in it, so this port was checked against
 * an independent source before being trusted: SingleStepTests/
 * ProcessorTests' 69.json and e9.json, which carry per-opcode fixtures
 * with full before/after CPU state. Of their 10,000 cases each, 4,962
 * (ADC) and 4,921 (SBC) enter with D set, 3,051 of the ADC ones with an
 * invalid BCD nibble. The predictor matched all 9,883 with zero
 * mismatches. Those fixtures are ~3.3MB each and are NOT vendored or
 * fetched by this gate; they were a one-off validation of the code
 * below, recorded here so the claim can be re-checked rather than taken.
 */

#include "../../src/cpu.h"
#include <stdio.h>
#include <string.h>

static uint8_t ram[65536];
static uint8_t bus_read(void *ctx, uint16_t addr) { (void)ctx; return ram[addr]; }
static void bus_write(void *ctx, uint16_t addr, uint8_t v) { (void)ctx; ram[addr] = v; }

typedef struct { uint8_t a; int n, v, z, c; } Expected;

/* Bruce Clark's ADD predictor, cputype = 0. n2h0/n2h1 are his N2H table:
 * the high nibble alone, and the high nibble with $0F OR'd in, picked by
 * whether the low-nibble add carried. */
static Expected predict_adc(uint8_t n1, uint8_t n2, int cin) {
    uint8_t n1l = n1 & 0x0F, n1h = n1 & 0xF0;
    uint8_t n2l = n2 & 0x0F, n2h0 = n2 & 0xF0, n2h1 = (uint8_t)((n2 & 0xF0) | 0x0F);
    int c = cin, x;
    int t = n1l + n2l + c;
    uint8_t a = (uint8_t)t;

    if (a >= 0x0A) {                    /* cmp #$0A set C, so adc #5 adds 6 */
        x = 1;
        a = (uint8_t)((a + 5 + 1) & 0x0F);
        c = 1;                          /* sec */
    } else {
        x = 0;
        c = 0;                          /* cmp #$0A cleared C */
    }

    a |= n1h;
    uint8_t op = x ? n2h1 : n2h0;
    t = a + op + c;
    uint8_t unc = (uint8_t)t;           /* php here: this is what NF/VF see */
    int unc_c = t > 0xFF;

    Expected e;
    e.n = (unc & 0x80) != 0;
    e.v = ((~(a ^ op) & (a ^ unc)) & 0x80) != 0;
    if (unc_c || unc >= 0xA0) {         /* adc #$5F with C set, then sec */
        e.a = (uint8_t)(unc + 0x5F + 1);
        e.c = 1;
    } else {
        e.a = unc;
        e.c = 0;
    }
    e.z = (uint8_t)(n1 + n2 + cin) == 0; /* ZF = HNVZC, the binary add */
    return e;
}

/* Clark's SUB1 predictor for the accumulator; every flag comes from the
 * plain binary subtraction, which is the NMOS decimal-SBC quirk. */
static Expected predict_sbc(uint8_t n1, uint8_t n2, int cin) {
    uint8_t n1l = n1 & 0x0F, n1h = n1 & 0xF0;
    uint8_t n2l = n2 & 0x0F, n2h0 = n2 & 0xF0, n2h1 = (uint8_t)((n2 & 0xF0) | 0x0F);
    int c = cin, x;
    int t = n1l - n2l - (1 - c);
    uint8_t a = (uint8_t)t;
    c = t >= 0;

    if (!c) {
        x = 1;
        a = (uint8_t)((uint8_t)(a - 5 - 1) & 0x0F);
        c = 0;                          /* clc */
    } else {
        x = 0;
    }

    a |= n1h;
    uint8_t op = x ? n2h1 : n2h0;
    t = a - op - (1 - c);
    a = (uint8_t)t;
    if (t < 0) a = (uint8_t)(a - 0x5F - 1);

    int tb = (int)n1 - (int)n2 - (1 - cin);
    uint8_t bin8 = (uint8_t)tb;

    Expected e;
    e.a = a;
    e.n = (bin8 & 0x80) != 0;
    e.z = bin8 == 0;
    e.c = tb >= 0;
    e.v = (((n1 ^ n2) & (n1 ^ bin8)) & 0x80) != 0;
    return e;
}

static long long checked = 0;
static int failures = 0;

static void report(const char *what, uint8_t n1, uint8_t n2, int cin,
                   Expected e, const Cpu6502 *cpu) {
    if (failures < 10) {
        fprintf(stderr,
                "FAIL: %s A=$%02X operand=$%02X C=%d\n"
                "        expected A=$%02X N=%d V=%d Z=%d C=%d\n"
                "        got      A=$%02X N=%d V=%d Z=%d C=%d\n",
                what, n1, n2, cin,
                e.a, e.n, e.v, e.z, e.c,
                cpu->a, (cpu->p & FLAG_N) != 0, (cpu->p & FLAG_V) != 0,
                (cpu->p & FLAG_Z) != 0, (cpu->p & FLAG_C) != 0);
    }
    failures++;
}

/* opcode/operand are poked fresh each iteration; the rest of RAM stays
 * zeroed, so there is no per-case memset of all 64K. */
static void run_one(const char *what, uint8_t opcode, int zero_page,
                    uint8_t n1, uint8_t n2, int cin, Expected e) {
    Cpu6502 cpu;
    memset(&cpu, 0, sizeof(cpu));
    cpu.bus.read = bus_read;
    cpu.bus.write = bus_write;
    cpu.pc = 0x0300;
    cpu.sp = 0xFF;
    cpu.a = n1;
    cpu.p = (uint8_t)(FLAG_U | FLAG_D | (cin ? FLAG_C : 0));

    ram[0x0300] = opcode;
    if (zero_page) {
        ram[0x0301] = 0x80;   /* zero-page address $80 */
        ram[0x0080] = n2;
    } else {
        ram[0x0301] = n2;
    }

    cpu_step(&cpu);
    checked++;

    if (cpu.a != e.a ||
        ((cpu.p & FLAG_N) != 0) != e.n ||
        ((cpu.p & FLAG_V) != 0) != e.v ||
        ((cpu.p & FLAG_Z) != 0) != e.z ||
        ((cpu.p & FLAG_C) != 0) != e.c) {
        report(what, n1, n2, cin, e, &cpu);
    }

    /* D must survive the instruction: nothing here should clear it. */
    if (!(cpu.p & FLAG_D) && failures < 10) {
        fprintf(stderr, "FAIL: %s cleared the D flag (A=$%02X operand=$%02X)\n",
                what, n1, n2);
        failures++;
    }
}

static void sweep(const char *what, uint8_t opcode, int zero_page,
                  Expected (*predict)(uint8_t, uint8_t, int)) {
    for (int cin = 0; cin <= 1; cin++)
        for (int n1 = 0; n1 <= 0xFF; n1++)
            for (int n2 = 0; n2 <= 0xFF; n2++)
                run_one(what, opcode, zero_page,
                        (uint8_t)n1, (uint8_t)n2, cin,
                        predict((uint8_t)n1, (uint8_t)n2, cin));
}

int main(void) {
    memset(ram, 0, sizeof(ram));

    sweep("ADC #imm", 0x69, 0, predict_adc);
    sweep("ADC $zp",  0x65, 1, predict_adc);
    sweep("SBC #imm", 0xE9, 0, predict_sbc);
    sweep("SBC $zp",  0xE5, 1, predict_sbc);

    if (failures == 0) {
        printf("PASS: all decimal-mode checks passed (%lld cases)\n", checked);
        return 0;
    }
    if (failures > 10)
        fprintf(stderr, "... and %d more\n", failures - 10);
    fprintf(stderr, "FAIL: %d of %lld decimal-mode cases wrong\n", failures, checked);
    return 1;
}
