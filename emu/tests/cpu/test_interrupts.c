/*
 * Dormann's suite (main.c/README.md in this directory) deliberately
 * never triggers a real RESET/NMI/IRQ mid-test - its own source
 * comments say so, and traps if one occurs. So it verifies every
 * opcode's own behavior thoroughly, but not cpu_reset()/cpu_nmi()/
 * cpu->irq_line/BRK's interrupt-entry machinery in cpu.c. This is a
 * small hand-written, hand-verified check for exactly that gap -
 * every expected value below was worked out by hand from the 6502's
 * documented interrupt-sequence behavior (see cpu.c's do_interrupt()
 * and cpu_reset()), the same way this toolkit's other test suites
 * (asm/, C/) check their own outputs against hand-calculated
 * expectations rather than just "does it run without crashing."
 */

#include "../../src/cpu.h"
#include <stdio.h>
#include <string.h>

static uint8_t ram[65536];
static uint8_t bus_read(void *ctx, uint16_t addr) { (void)ctx; return ram[addr]; }
static void bus_write(void *ctx, uint16_t addr, uint8_t v) { (void)ctx; ram[addr] = v; }

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { fprintf(stderr, "FAIL: %s\n", msg); failures++; } \
} while (0)

static void reset_cpu(Cpu6502 *cpu) {
    memset(&ram, 0, sizeof(ram));
    memset(cpu, 0, sizeof(*cpu));
    cpu->bus.read = bus_read;
    cpu->bus.write = bus_write;
}

/* cpu_reset(): SP -= 3, I set, PC loaded from $FFFC/$FFFD. A/X/Y/other
 * P bits are untouched (real hardware doesn't clear them on reset). */
static void test_reset(void) {
    Cpu6502 cpu;
    reset_cpu(&cpu);
    cpu.sp = 0xFF;
    cpu.a = 0x42; /* should survive reset untouched */
    ram[0xFFFC] = 0x00; ram[0xFFFD] = 0x80; /* reset vector -> $8000 */

    cpu_reset(&cpu);

    CHECK(cpu.sp == 0xFC, "reset: SP should decrement by exactly 3");
    CHECK(cpu.p & FLAG_I, "reset: I flag should be set");
    CHECK(cpu.pc == 0x8000, "reset: PC should load from $FFFC/$FFFD");
    CHECK(cpu.a == 0x42, "reset: A should be untouched by reset");
}

/* BRK: 2-byte instruction (opcode + a padding byte real software
 * conventionally ignores) - the pushed return address is opcode_addr+2,
 * not +1, so RTI lands one byte past the padding byte, not on it. */
static void test_brk(void) {
    Cpu6502 cpu;
    reset_cpu(&cpu);
    cpu.pc = 0x0300;
    cpu.sp = 0xFF;
    cpu.p = FLAG_U; /* I and B both clear beforehand, to prove BRK sets I and pushes B=1 */
    ram[0x0300] = 0x00; /* BRK */
    ram[0x0301] = 0xEA; /* padding byte, never executed */
    ram[0xFFFE] = 0x00; ram[0xFFFF] = 0x90; /* IRQ/BRK vector -> $9000 */

    int cycles = cpu_step(&cpu);

    CHECK(cycles == 7, "BRK: should take 7 cycles");
    CHECK(cpu.pc == 0x9000, "BRK: PC should load from $FFFE/$FFFF");
    CHECK(cpu.p & FLAG_I, "BRK: should set I");
    CHECK(cpu.sp == 0xFC, "BRK: should push 3 bytes (PCH, PCL, P)");
    uint8_t pushed_p = ram[0x0100 + 0xFD];
    uint8_t pushed_pcl = ram[0x0100 + 0xFE];
    uint8_t pushed_pch = ram[0x0100 + 0xFF];
    CHECK((pushed_p & FLAG_B) != 0, "BRK: pushed P should have B set");
    CHECK((pushed_p & FLAG_U) != 0, "BRK: pushed P should have U set");
    CHECK((uint16_t)(pushed_pcl | (pushed_pch << 8)) == 0x0302,
          "BRK: pushed return address should be opcode_addr+2 (skips the padding byte)");
}

/* IRQ (via cpu->irq_line, level-triggered): should be serviced between
 * instructions when I is clear, push B=0, and be ignored entirely
 * while I is set - unlike NMI, which cpu_nmi()/nmi_pending bypass I
 * for entirely (edge-triggered, sampled once regardless of I). */
static void test_irq(void) {
    Cpu6502 cpu;
    reset_cpu(&cpu);
    cpu.pc = 0x0300;
    cpu.sp = 0xFF;
    cpu.p = FLAG_U; /* I clear */
    ram[0x0300] = 0xEA; /* NOP, in case IRQ is (wrongly) not taken and this executes instead */
    ram[0xFFFE] = 0x00; ram[0xFFFF] = 0xA0; /* IRQ vector -> $A000 */
    cpu.irq_line = 1;

    cpu_step(&cpu);

    CHECK(cpu.pc == 0xA000, "IRQ: should be serviced (I was clear) and jump to $FFFE vector");
    CHECK(cpu.p & FLAG_I, "IRQ: should set I");
    uint8_t pushed_p = ram[0x0100 + 0xFD];
    CHECK((pushed_p & FLAG_B) == 0, "IRQ: pushed P should have B clear (unlike BRK)");

    /* Masked while I is set - the pending IRQ should be silently
     * skipped, letting the NOP at $0300 execute normally instead. */
    reset_cpu(&cpu);
    cpu.pc = 0x0300;
    cpu.sp = 0xFF;
    cpu.p = (uint8_t)(FLAG_U | FLAG_I);
    ram[0x0300] = 0xEA;
    cpu.irq_line = 1;
    cpu_step(&cpu);
    CHECK(cpu.pc == 0x0301, "IRQ: should be masked while I is set (NOP should execute instead)");
}

/* NMI: edge-triggered via cpu_nmi(), NOT masked by I (unlike IRQ),
 * uses the $FFFA/$FFFB vector, and clears nmi_pending after servicing
 * so a single edge only fires the handler once. */
static void test_nmi(void) {
    Cpu6502 cpu;
    reset_cpu(&cpu);
    cpu.pc = 0x0300;
    cpu.sp = 0xFF;
    cpu.p = (uint8_t)(FLAG_U | FLAG_I); /* I SET - NMI must fire anyway */
    ram[0xFFFA] = 0x00; ram[0xFFFB] = 0xB0; /* NMI vector -> $B000 */
    cpu_nmi(&cpu);

    cpu_step(&cpu);

    CHECK(cpu.pc == 0xB000, "NMI: should be serviced even though I was set");
    CHECK(cpu.nmi_pending == 0, "NMI: nmi_pending should clear once serviced");

    /* A second step with no new edge should NOT re-fire it. */
    ram[0xB000] = 0xEA; /* NOP at the handler entry */
    cpu_step(&cpu);
    CHECK(cpu.pc == 0xB001, "NMI: should not re-fire without a fresh edge");
}

/* RTI should restore PC and P (with U forced set, B ignored/cleared -
 * it was never a real latch) from exactly what an interrupt sequence
 * pushed - a round trip through BRK's own entry sequence. */
static void test_rti_roundtrip(void) {
    Cpu6502 cpu;
    reset_cpu(&cpu);
    cpu.pc = 0x0300;
    cpu.sp = 0xFF;
    cpu.p = (uint8_t)(FLAG_U | FLAG_C | FLAG_N);
    ram[0x0300] = 0x00; /* BRK */
    ram[0xFFFE] = 0x00; ram[0xFFFF] = 0x90;
    ram[0x9000] = 0x40; /* RTI, right at the handler entry */

    cpu_step(&cpu); /* BRK */
    uint8_t p_before_rti = cpu.p;
    cpu_step(&cpu); /* RTI */

    CHECK(cpu.pc == 0x0302, "RTI: should restore PC to BRK's own pushed return address");
    CHECK(cpu.sp == 0xFF, "RTI: should restore SP to its pre-interrupt value");
    CHECK((cpu.p & (FLAG_C | FLAG_N)) == (p_before_rti & (FLAG_C | FLAG_N)) &&
          (cpu.p & (FLAG_C | FLAG_N)) == (FLAG_C | FLAG_N),
          "RTI: should restore C and N exactly as they were before BRK");
}

int main(void) {
    test_reset();
    test_brk();
    test_irq();
    test_nmi();
    test_rti_roundtrip();

    if (failures == 0) {
        printf("PASS: all interrupt/reset checks passed\n");
        return 0;
    }
    fprintf(stderr, "FAIL: %d check(s) failed\n", failures);
    return 1;
}
