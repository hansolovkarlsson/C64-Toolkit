/*
 * Hand-verified checks for ../../src/cia.c, worked out directly
 * against the publicly documented 6526 register semantics cited in
 * cia.h's header comment - there's no third-party test suite for this
 * chip the way tests/cpu/ has Klaus Dormann's, so every expected value
 * below is derived by hand from the register documentation, the same
 * way tests/memory/ checks against the C64 bank-switching mode table.
 */

#include "../../src/cia.h"
#include <stdio.h>

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { fprintf(stderr, "FAIL: %s\n", msg); failures++; } \
} while (0)

enum {
    REG_PRA = 0x0, REG_PRB = 0x1, REG_DDRA = 0x2, REG_DDRB = 0x3,
    REG_TALO = 0x4, REG_TAHI = 0x5, REG_TBLO = 0x6, REG_TBHI = 0x7,
    REG_ICR = 0xD, REG_CRA = 0xE, REG_CRB = 0xF
};

static void test_port_ddr_semantics(void) {
    Cia cia;
    cia_init(&cia);

    /* All bits input by default (ddra/ddrb start at 0): reading PRA
     * should reflect the external pin state, not anything written. */
    cia.porta_in = 0xA5;
    cia_write(&cia, REG_PRA, 0xFF); /* output latch updated regardless of DDR - real hardware always latches */
    CHECK(cia_read(&cia, REG_PRA) == 0xA5, "all-input PRA: read should reflect porta_in, ignoring the output latch");

    /* Configure the low nibble as output: those bits should now read
     * back the output latch; the high nibble should still read the
     * external pin state. */
    cia_write(&cia, REG_DDRA, 0x0F);
    cia_write(&cia, REG_PRA, 0x3C); /* low nibble=0xC (output), high nibble=0x3 (ignored, input) */
    /* expected: (0x3C & 0x0F) | (0xA5 & 0xF0) = 0x0C | 0xA0 = 0xAC */
    CHECK(cia_read(&cia, REG_PRA) == 0xAC, "mixed DDR: output bits should read the latch, input bits the external pins");
}

static void test_timer_a_continuous(void) {
    Cia cia;
    cia_init(&cia);

    cia_write(&cia, REG_TALO, 0x03);
    cia_write(&cia, REG_TAHI, 0x00); /* latch=3; timer stopped, so this also force-loads the counter */
    CHECK(cia_read(&cia, REG_TALO) == 0x03, "TAHI write while stopped should force-reload the counter from the latch");

    cia_write(&cia, REG_CRA, 0x01); /* START, continuous, phi2 input */

    /* Period is latch+1 = 4 ticks per underflow (see cia.h). */
    cia_tick(&cia, 3);
    CHECK((cia_read(&cia, REG_ICR) & 0x01) == 0, "timer A: no underflow yet after 3 of 4 ticks");
    cia_tick(&cia, 1);
    uint8_t icr = cia_read(&cia, REG_ICR);
    CHECK((icr & 0x01) != 0, "timer A: 4th tick should underflow and set ICR bit 0");
    CHECK((icr & 0x80) == 0, "timer A: ICR bit 7 should be clear since the interrupt mask is still 0");
    CHECK((cia_read(&cia, REG_ICR) & 0x01) == 0, "timer A: reading ICR should have cleared the pending bit");
    CHECK((cia_read(&cia, REG_CRA) & 0x01) != 0, "timer A: continuous mode should still be running after underflow");

    /* Reload should have happened, so another 4 ticks underflows again. */
    cia_tick(&cia, 4);
    CHECK((cia_read(&cia, REG_ICR) & 0x01) != 0, "timer A: continuous mode should underflow again after reload");
}

static void test_timer_a_oneshot_and_irq_mask(void) {
    Cia cia;
    cia_init(&cia);

    cia_write(&cia, REG_TALO, 0x01);
    cia_write(&cia, REG_TAHI, 0x00); /* latch=1, period=2 ticks */
    cia_write(&cia, REG_ICR, 0x81);  /* SET mask bit 0 - unmask timer A */
    cia_write(&cia, REG_CRA, 0x09);  /* START | RUNMODE(one-shot) */

    CHECK(cia_irq_line(&cia) == 0, "timer A: IRQ line should be clear before any underflow");
    cia_tick(&cia, 2);
    CHECK(cia_irq_line(&cia) != 0, "timer A: IRQ line should assert once the masked source underflows");
    CHECK((cia_read(&cia, REG_CRA) & 0x01) == 0, "timer A: one-shot mode should auto-clear START after underflow");
    CHECK(cia_irq_line(&cia) != 0, "timer A: IRQ line should stay asserted until ICR is actually read");
    cia_read(&cia, REG_ICR);
    CHECK(cia_irq_line(&cia) == 0, "timer A: reading ICR should de-assert the IRQ line by clearing the pending bit");
}

static void test_timer_b_cascade_on_timer_a_underflow(void) {
    Cia cia;
    cia_init(&cia);

    cia_write(&cia, REG_TALO, 0x00);
    cia_write(&cia, REG_TAHI, 0x00); /* latch=0, period=1 tick - underflows every tick */
    cia_write(&cia, REG_CRA, 0x01);  /* START, continuous */

    cia_write(&cia, REG_TBLO, 0x02);
    cia_write(&cia, REG_TBHI, 0x00); /* latch=2 */
    cia_write(&cia, REG_CRB, 0x41);  /* START | INMODE=10 (count timer A underflows) */

    cia_tick(&cia, 1); /* TA underflows -> TB counts one */
    cia_tick(&cia, 1); /* TA underflows -> TB counts one */
    CHECK((cia_read(&cia, REG_ICR) & 0x02) == 0, "timer B cascade: should not have underflowed after only 2 of 3 TA underflows");
    cia_tick(&cia, 1); /* TA underflows a 3rd time -> TB's 3rd count -> underflow (latch=2, period=3) */
    CHECK((cia_read(&cia, REG_ICR) & 0x02) != 0, "timer B cascade: should underflow on the 3rd TA underflow (latch=2 -> period 3)");
}

static void test_force_load(void) {
    Cia cia;
    cia_init(&cia);

    cia_write(&cia, REG_TALO, 0x05);
    cia_write(&cia, REG_TAHI, 0x00);
    cia_write(&cia, REG_CRA, 0x01); /* start running - counter now counting down from 5 */
    cia_tick(&cia, 3);              /* counter should be 2 now */
    CHECK(cia_read(&cia, REG_TALO) == 2, "sanity: counter should have decremented to 2 after 3 ticks");

    cia_write(&cia, REG_CRA, 0x11); /* START | FORCELOAD strobe */
    CHECK(cia_read(&cia, REG_TALO) == 5, "FORCELOAD: should immediately reset the counter to the latch value");
    CHECK((cia_read(&cia, REG_CRA) & 0x10) == 0, "FORCELOAD: the strobe bit itself should never read back set");
}

int main(void) {
    test_port_ddr_semantics();
    test_timer_a_continuous();
    test_timer_a_oneshot_and_irq_mask();
    test_timer_b_cascade_on_timer_a_underflow();
    test_force_load();

    if (failures == 0) {
        printf("PASS: all CIA checks passed\n");
        return 0;
    }
    fprintf(stderr, "FAIL: %d check(s) failed\n", failures);
    return 1;
}
