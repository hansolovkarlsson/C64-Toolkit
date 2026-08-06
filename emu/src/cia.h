#ifndef C64EMU_CIA_H
#define C64EMU_CIA_H

#include <stdint.h>

/* One MOS 6526/8520 CIA - chip-generic, no C64-specific wiring
 * assumptions (the C64 has two: CIA1 at $DC00-$DCFF wired to the
 * keyboard matrix/joystick 1-2/IRQ, CIA2 at $DD00-$DDFF wired to the
 * serial bus/VIC bank select/RS-232/NMI - that wiring lives in
 * src/machine.c, not here). Register layout (addr & 0x0F, mirrored
 * every 16 bytes across each chip's 256-byte block):
 *
 *   $0 PRA   $1 PRB   $2 DDRA  $3 DDRB
 *   $4 TALO  $5 TAHI  $6 TBLO  $7 TBHI
 *   $8 TOD10THS  $9 TODSEC  $A TODMIN  $B TODHR
 *   $C SDR   $D ICR   $E CRA   $F CRB
 *
 * SIMPLIFICATIONS (no known C64 boot/keyboard path depends on any of
 * these - revisit if a specific piece of software turns out to
 * need one):
 *   - TOD (time-of-day clock) registers are passive storage only -
 *     reads return whatever was last written, no real 50/60Hz ticking,
 *     no alarm comparison/interrupt.
 *   - SDR (serial data register) is passive storage only - no real
 *     shift-in/shift-out timing, so the "SDR full" interrupt source
 *     never fires.
 *   - CRA/CRB's INMODE bit for counting external CNT-pin pulses isn't
 *     modeled (the CNT pin isn't wired to anything in this emulator)
 *     - a timer configured for CNT input simply never counts. Timer B
 *     counting Timer A's underflows (the other INMODE option, used
 *     for chained 32-bit timing) IS modeled.
 *   - Timer underflow timing is period = (latch value) + 1 ticks of
 *     whatever it's counting - not verified cycle-exact against real
 *     hardware, just against the publicly documented register
 *     semantics (unlike cpu.c/memory.c, there's no third-party test
 *     suite this was cross-checked against).
 */
typedef struct {
    uint8_t pra, prb;     /* output latches - what was last WRITTEN, regardless of DDR */
    uint8_t ddra, ddrb;   /* 1 bit = that pin is an output */
    uint8_t porta_in, portb_in; /* external pin state for INPUT-configured bits, set by the caller before a read - see machine.c */

    uint16_t ta_latch, ta_counter;
    uint16_t tb_latch, tb_counter;

    uint8_t tod_10ths, tod_sec, tod_min, tod_hr; /* passive storage - see header comment */
    uint8_t sdr;                                  /* passive storage - see header comment */

    uint8_t icr_mask;    /* which of bits 0-4 currently trigger the IRQ/NMI line */
    uint8_t icr_pending; /* which of bits 0-4 are currently latched (cleared by reading ICR) */

    uint8_t cra, crb;
} Cia;

void cia_init(Cia *cia);

/* addr should already be reduced to a register number (addr & 0x0F) -
 * machine.c owns deciding which physical address range maps to which
 * chip. Reading $0D (ICR) clears icr_pending and so de-asserts the
 * IRQ/NMI line, per real hardware - see cia_irq_line() below. */
uint8_t cia_read(Cia *cia, uint8_t reg);
void cia_write(Cia *cia, uint8_t reg, uint8_t v);

/* Advances both timers by `cycles` PHI2 ticks (call once per
 * cpu_step(), with that call's returned cycle count - timer
 * underflows need to be checked at CPU-instruction granularity, not
 * batched per video frame, so interrupts fire on time). */
void cia_tick(Cia *cia, int cycles);

/* True exactly when this chip is currently requesting an interrupt:
 * (icr_pending & icr_mask) != 0. CIA1's is wired to the CPU's /IRQ
 * line (level-triggered - see cpu.h), CIA2's to /NMI (edge-triggered -
 * machine.c must track the previous value itself and call cpu_nmi()
 * only on a 0->1 transition). */
int cia_irq_line(const Cia *cia);

#endif
