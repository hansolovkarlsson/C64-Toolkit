/*
 * Regression test for machine_push_prg_return_trampoline() (see
 * machine.h's doc comment for the full bug narrative). Reproduces the
 * exact bisection control case from that bug report directly: a
 * minimal hand-assembled ".basic"-style .prg whose machine code is
 * nothing but an immediate RTS - no cc64, no c64asm, no real ROMs
 * involved, since the bug was in the emulator's own SYS-target
 * injection shortcut, not anything program-specific.
 *
 * Drives the exact same sequence gtk/main.c's try_inject_prg() does -
 * machine_load_prg(), machine_find_sys_target(),
 * machine_push_prg_return_trampoline(), then cpu.pc = sys_target - and
 * checks that running the program actually reaches the trampoline's
 * landing pad (proving the RTS popped a valid address instead of
 * garbage) with the stack pointer restored to its pre-injection value
 * (proving the push/pop is exactly balanced, not just accidentally
 * landing somewhere survivable), and that the trampoline is a genuine
 * stable self-loop rather than falling through into unrelated memory.
 */

#include "../../src/machine.h"
#include <stdio.h>

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { fprintf(stderr, "FAIL: %s\n", msg); failures++; } \
} while (0)

#define FIXTURE_PATH "prg_inject_fixture.prg.tmp"
#define FIXTURE_LOAD_ADDR 0x0801
#define FIXTURE_SYS_TARGET 0x0810 /* = decimal 2064, see FIXTURE_PRG below */

/* [2-byte load address header: $0801]
 * [next-line ptr, 2 bytes - unused by machine_find_sys_target()]
 * [line number, 2 bytes - unused]
 * [$9E "SYS" token]["2064" ASCII digits][$00 end of BASIC line]
 * [padding up to $0810]
 * [$60 RTS, at $0810 - the whole "program"] */
static const uint8_t FIXTURE_PRG[] = {
    0x01, 0x08,
    0x00, 0x00,
    0x00, 0x00,
    0x9E,
    '2', '0', '6', '4',
    0x00,
    0x00, 0x00, 0x00, 0x00, 0x00,
    0x60,
};

static void write_fixture(void) {
    FILE *f = fopen(FIXTURE_PATH, "wb");
    if (!f) { fprintf(stderr, "FAIL: couldn't create fixture file %s\n", FIXTURE_PATH); failures++; return; }
    fwrite(FIXTURE_PRG, 1, sizeof(FIXTURE_PRG), f);
    fclose(f);
}

#define MAX_CYCLES 1000

static void test_terminating_program_returns_cleanly(void) {
    write_fixture();

    Machine m;
    machine_init(&m);
    machine_reset(&m);

    uint16_t load_addr = machine_load_prg(&m, FIXTURE_PATH);
    CHECK(load_addr == FIXTURE_LOAD_ADDR, "fixture should load at $0801");

    uint16_t sys_target = machine_find_sys_target(&m, load_addr);
    CHECK(sys_target == FIXTURE_SYS_TARGET, "fixture's BASIC stub should parse to SYS $0810");

    uint8_t sp_before = m.cpu.sp;
    machine_push_prg_return_trampoline(&m);
    CHECK(m.cpu.sp == (uint8_t)(sp_before - 2), "pushing the trampoline's return address should consume exactly 2 stack bytes");

    m.cpu.pc = sys_target;

    int reached_trampoline = 0;
    long total = 0;
    while (total < MAX_CYCLES) {
        total += machine_step(&m);
        if (m.cpu.pc == MACHINE_PRG_RETURN_TRAMPOLINE_ADDR) { reached_trampoline = 1; break; }
    }
    CHECK(reached_trampoline, "program's RTS should land in the return trampoline instead of crashing/wandering off");
    CHECK(m.cpu.sp == sp_before, "stack pointer should be restored to its pre-injection value after the RTS pops the trampoline's return address");

    /* One more instruction: the trampoline is JMP <itself>, so PC must
     * still be sitting right there - not just passed through once. */
    machine_step(&m);
    CHECK(m.cpu.pc == MACHINE_PRG_RETURN_TRAMPOLINE_ADDR, "return trampoline should be a stable JMP-to-self, not a one-shot landing pad");

    remove(FIXTURE_PATH);
}

int main(void) {
    test_terminating_program_returns_cleanly();

    if (failures == 0) {
        printf("PASS: all prg_inject checks passed\n");
        return 0;
    }
    fprintf(stderr, "FAIL: %d check(s) failed\n", failures);
    return 1;
}
