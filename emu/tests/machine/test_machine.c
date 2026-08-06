/*
 * Hand-verified checks for ../../src/machine.c - specifically the
 * C64-specific wiring on top of the generic CIA chip (../cia/ already
 * covers the chip itself): keyboard-matrix/joystick pin-pulldown
 * modeling on CIA1's ports, and CIA1/CIA2 IRQ/NMI propagation into
 * the CPU. Every expected value below is worked out by hand against
 * the standard published C64 keyboard matrix and real CIA1/CIA2 wiring
 * (see machine.h's header comment), the same way ../memory/ and
 * ../cia/ check against their own hand-derived expectations.
 */

#include "../../src/machine.h"
#include <stdio.h>

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { fprintf(stderr, "FAIL: %s\n", msg); failures++; } \
} while (0)

enum { REG_PRA = 0x0, REG_PRB = 0x1, REG_DDRA = 0x2, REG_DDRB = 0x3, REG_ICR = 0xD, REG_CRA = 0xE };

static void test_keyboard_scan_column_to_row(void) {
    Machine m;
    machine_init(&m);

    /* Standard KERNAL wiring: PRA (columns) all-output, PRB (rows)
     * all-input. "A" is at PA1/PB2 (see gtk/main.c's keymap). */
    memory_write(&m.mem, 0xDC02, 0xFF); /* DDRA = all output */
    memory_write(&m.mem, 0xDC03, 0x00); /* DDRB = all input */

    machine_set_key(&m, 1, 2, 1); /* hold "A" */

    memory_write(&m.mem, 0xDC00, (uint8_t)~(1u << 1)); /* select column 1 (active low) */
    uint8_t row = memory_read(&m.mem, 0xDC01);
    CHECK((row & (1u << 2)) == 0, "keyboard scan: selecting A's column should read A's row bit low");
    CHECK((row & ~(1u << 2)) == (uint8_t)(~(1u << 2)), "keyboard scan: no other row bit should be affected by A alone");

    memory_write(&m.mem, 0xDC00, (uint8_t)~(1u << 3)); /* select a different, unrelated column */
    row = memory_read(&m.mem, 0xDC01);
    CHECK(row == 0xFF, "keyboard scan: a column with no held key should read all rows high");

    machine_set_key(&m, 1, 2, 0); /* release "A" */
    memory_write(&m.mem, 0xDC00, (uint8_t)~(1u << 1));
    row = memory_read(&m.mem, 0xDC01);
    CHECK(row == 0xFF, "keyboard scan: releasing the key should stop pulling its row low");
}

static void test_keyboard_scan_reverse_direction(void) {
    Machine m;
    machine_init(&m);

    /* Reverse wiring: PRB (rows) as output/select, PRA (columns) as
     * input/read - some real software scans this direction too. */
    memory_write(&m.mem, 0xDC02, 0x00); /* DDRA = all input */
    memory_write(&m.mem, 0xDC03, 0xFF); /* DDRB = all output */

    machine_set_key(&m, 1, 2, 1); /* "A" again: pa=1, pb=2 */

    memory_write(&m.mem, 0xDC01, (uint8_t)~(1u << 2)); /* select row 2 */
    uint8_t col = memory_read(&m.mem, 0xDC00);
    CHECK((col & (1u << 1)) == 0, "reverse-direction scan: selecting A's row should read A's column bit low");
}

static void test_joystick2_shares_pra(void) {
    Machine m;
    machine_init(&m);

    memory_write(&m.mem, 0xDC02, 0x00); /* DDRA = all input, i.e. reading raw joystick 2 - no keyboard column-select in progress */
    CHECK(memory_read(&m.mem, 0xDC00) == 0xFF, "joystick 2: idle stick should read all 1s");

    machine_set_joystick(&m, 2, (uint8_t)~0x01); /* hold "up" (bit 0) */
    CHECK((memory_read(&m.mem, 0xDC00) & 0x01) == 0, "joystick 2: holding up should pull PRA bit 0 low");

    machine_set_joystick(&m, 2, 0xFF); /* release */
    CHECK(memory_read(&m.mem, 0xDC00) == 0xFF, "joystick 2: releasing should stop pulling the bit low");
}

static void test_cia1_irq_reaches_cpu(void) {
    Machine m;
    machine_init(&m);
    machine_reset(&m);

    memory_write(&m.mem, 0xDC04, 0x01); /* TALO=1 */
    memory_write(&m.mem, 0xDC05, 0x00); /* TAHI=0 -> latch=1, period=2 cycles (timer stopped, so this also loads the counter) */
    memory_write(&m.mem, 0xDC0D, 0x81); /* ICR: unmask timer A */
    memory_write(&m.mem, 0xDC0E, 0x01); /* CRA: START, continuous */

    CHECK(m.cpu.irq_line == 0, "CIA1 IRQ: line should be clear before any timer underflow");
    int total_cycles = 0;
    while (total_cycles < 20) { /* comfortably more than one period */
        total_cycles += machine_step(&m);
        if (m.cpu.irq_line) break;
    }
    CHECK(m.cpu.irq_line != 0, "CIA1 IRQ: cpu.irq_line should go high once timer A underflows with its interrupt unmasked");
}

static void test_vic_raster_irq_reaches_cpu(void) {
    Machine m;
    machine_init(&m);
    machine_reset(&m);

    memory_write(&m.mem, 0xD011, 0x00); /* raster compare MSB = 0 */
    memory_write(&m.mem, 0xD012, 1);    /* raster compare = line 1 */
    memory_write(&m.mem, 0xD01A, 0x01); /* unmask the raster IRQ source */

    CHECK(m.cpu.irq_line == 0, "VIC-II raster IRQ: line should be clear before the compare line is reached");
    int total_cycles = 0;
    while (total_cycles < 200) { /* comfortably more than one line (63 cycles) */
        total_cycles += machine_step(&m);
        if (m.cpu.irq_line) break;
    }
    CHECK(m.cpu.irq_line != 0, "VIC-II raster IRQ: cpu.irq_line should go high once the raster reaches the compare line with the source unmasked");
}

static void test_bad_line_stalls_cpu(void) {
    Machine m;
    machine_init(&m);
    machine_reset(&m);

    memory_write(&m.mem, 0xD011, 0x10); /* DEN=1, YSCROLL=0 - first bad line is at raster line $30 (48) */

    /* Step until we hit the dedicated stall-consuming machine_step()
     * call - identifiable by its cycle count, VIC_BADLINE_STALL_CYCLES
     * (40), since no real 6502 instruction takes anywhere near that
     * many cycles (max ~7-8). */
    int found_stall = 0;
    int total = 0;
    while (total < 200000 && !found_stall) {
        uint16_t pc_before = m.cpu.pc;
        uint64_t cpu_cycles_before = m.cpu.cycles;
        int c = machine_step(&m);
        total += c;
        if (c == VIC_BADLINE_STALL_CYCLES) {
            found_stall = 1;
            CHECK(m.cpu.pc == pc_before, "bad line stall: CPU PC should not advance during the stall call");
            CHECK(m.cpu.cycles == cpu_cycles_before, "bad line stall: cpu.cycles (only incremented inside cpu_step()) should not advance during the stall call");
        }
    }
    CHECK(found_stall, "bad line stall: should have encountered a 40-cycle stall call while stepping toward line $30");
}

static void test_cia2_nmi_reaches_cpu(void) {
    Machine m;
    machine_init(&m);
    machine_reset(&m);
    m.cpu.nmi_pending = 0;

    memory_write(&m.mem, 0xDD04, 0x01); /* CIA2 TALO=1 */
    memory_write(&m.mem, 0xDD05, 0x00); /* CIA2 TAHI=0 -> latch=1 */
    memory_write(&m.mem, 0xDD0D, 0x81); /* CIA2 ICR: unmask timer A */
    memory_write(&m.mem, 0xDD0E, 0x01); /* CIA2 CRA: START, continuous */

    int total_cycles = 0;
    while (total_cycles < 20) {
        total_cycles += machine_step(&m);
        if (m.cpu.nmi_pending) break;
    }
    CHECK(m.cpu.nmi_pending != 0, "CIA2 IRQ output: should edge-trigger cpu_nmi() once timer A underflows with its interrupt unmasked");
}

int main(void) {
    test_keyboard_scan_column_to_row();
    test_keyboard_scan_reverse_direction();
    test_joystick2_shares_pra();
    test_cia1_irq_reaches_cpu();
    test_vic_raster_irq_reaches_cpu();
    test_bad_line_stalls_cpu();
    test_cia2_nmi_reaches_cpu();

    if (failures == 0) {
        printf("PASS: all machine-level checks passed\n");
        return 0;
    }
    fprintf(stderr, "FAIL: %d check(s) failed\n", failures);
    return 1;
}
