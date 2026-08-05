#ifndef C64EMU_CPU_H
#define C64EMU_CPU_H

#include <stdint.h>

/* cpu.c only ever touches memory through this vtable - it never needs
 * to know whether it's driving a flat test-harness RAM array
 * (tests/cpu/) or the real bank-switched C64 memory map (src/memory.c,
 * not implemented yet - see ../ROADMAP.md). `ctx` is opaque and passed
 * through unchanged to both callbacks. */
typedef struct {
    uint8_t (*read)(void *ctx, uint16_t addr);
    void (*write)(void *ctx, uint16_t addr, uint8_t value);
    void *ctx;
} CpuBus;

/* Status register (P) flag bits, in their real 6502 bit positions. */
#define FLAG_C 0x01u /* carry */
#define FLAG_Z 0x02u /* zero */
#define FLAG_I 0x04u /* interrupt disable */
#define FLAG_D 0x08u /* decimal mode */
#define FLAG_B 0x10u /* break - only meaningful in the byte BRK/PHP push, not a real latch (see cpu.c) */
#define FLAG_U 0x20u /* unused - always reads/pushes as 1 */
#define FLAG_V 0x40u /* overflow */
#define FLAG_N 0x80u /* negative */

typedef struct {
    uint8_t a, x, y, sp, p;
    uint16_t pc;
    CpuBus bus;
    uint64_t cycles;  /* total cycles executed since cpu_reset() - VIC/CIA timing will read this later */
    int nmi_pending;  /* edge-triggered - see cpu_nmi() */
    int irq_line;     /* level-triggered: nonzero while any IRQ source asserts it, same as the real 6510's /IRQ pin - set/clear directly, no cpu_irq() setter needed */
} Cpu6502;

/* The real power-on/reset sequence: A/X/Y/P are left whatever they
 * already were (matching real hardware - reset doesn't clear them),
 * except I is forced set; SP -= 3 (the real 6502 doesn't actually
 * write during those 3 cycles, it just decrements SP as if it had);
 * PC is loaded from the reset vector at $FFFC/$FFFD. A test harness
 * that wants to start execution at a specific address instead of
 * whatever the reset vector says (e.g. Dormann's suite, entered
 * directly at $0400) should zero the registers it cares about and set
 * cpu->pc itself AFTER calling this. */
void cpu_reset(Cpu6502 *cpu);

/* Executes exactly one instruction, or services a pending NMI/IRQ if
 * one is asserted (checked once, at instruction boundaries - matching
 * real 6502 behavior of only sampling interrupt lines between
 * instructions). Returns the number of cycles it took; also adds that
 * to cpu->cycles. */
int cpu_step(Cpu6502 *cpu);

/* Edge-triggered: call exactly once per NMI edge (low-to-high on the
 * real /NMI pin), not once per cycle it's held - unlike IRQ, which is
 * level-triggered via cpu->irq_line directly. */
void cpu_nmi(Cpu6502 *cpu);

#endif
