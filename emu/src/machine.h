#ifndef C64EMU_MACHINE_H
#define C64EMU_MACHINE_H

#include "cpu.h"
#include "memory.h"
#include "cia.h"
#include "vic.h"
#include "sid.h"

/* Wires the CPU, memory map, both CIAs, VIC-II, and SID together into
 * one C64: registers a single IoBus with Memory that dispatches
 * $D000-$D3FF to VIC-II, $D400-$D7FF to SID, $D800-$DBFF to color RAM,
 * $DC00-$DCFF to CIA1, and $DD00-$DDFF to CIA2 (see machine.c's
 * io_read()/io_write()) - cartridge I/O still reads back the inert
 * placeholder memory.c already had, since no cartridge support exists.
 * Also owns the C64-specific keyboard-matrix/joystick wiring onto
 * CIA1's ports - that wiring is a property of how the C64 connects
 * CIA1 to its keyboard/joystick ports, not a property of the 6526 chip
 * itself (see cia.h), which is why it lives here and not in cia.c.
 * SID itself needs no such C64-specific wiring - sid_tick() is called
 * from machine_step() the same way cia_tick()/vic_tick() are, and
 * there's nothing beyond that for machine.c to own; pulling samples via
 * sid_output() at a real sample rate and feeding an OS audio API is a
 * GTK-shell-level concern instead, and lives in gtk/main.c (SDL2), the
 * same way blitting vic_render_frame()'s pixels is gtk/main.c's job,
 * not machine.c's. */
typedef struct {
    Cpu6502 cpu;
    Memory mem;
    Cia cia1; /* keyboard matrix, joystick 1/2, IRQ */
    Cia cia2; /* serial bus, VIC bank select, user port, NMI - only VIC bank select is modeled, via cia2's own PRA/DDRA (see machine_vic_bank()) */
    Vic vic;  /* text-mode display, see vic.h for exactly what's modeled */
    Sid sid;  /* 3-voice synth, see sid.h for exactly what's modeled - audio output lives in gtk/main.c, see this struct's own header comment */

    /* key_matrix[pa_bit] bit pb_bit set == that key is currently held.
     * pa_bit/pb_bit follow the standard published C64 keyboard matrix
     * (PA selects a column by driving it low, PB reads back which
     * rows in that column are pulled low by a held key) - see
     * gtk/main.c's keymap table for the actual key positions. */
    uint8_t key_matrix[8];

    /* Active-low, matching real hardware: bit=0 means held/pushed.
     * Bits 0-4 = up, down, left, right, fire; bits 5-7 unused, must
     * stay 1. Joystick 2 shares CIA1 PRA's pins 0-4 with the keyboard
     * column-select lines (a real, well-known hardware quirk - see
     * asm/docs/c64-memory-reference.md §6); joystick 1 shares PRB's. */
    uint8_t joystick1, joystick2;

    int cia2_nmi_prev; /* edge-detect state for CIA2's IRQ output -> CPU /NMI, see cpu_nmi()'s own edge-triggered contract */
} Machine;

void machine_init(Machine *m);

/* Forwards to memory_load_roms() - see roms/README.md for expected files/sizes. */
int machine_load_roms(Machine *m, const char *dir);

void machine_reset(Machine *m);

/* Normally: executes exactly one CPU instruction (or services a
 * pending IRQ/NMI, same as cpu_step()), ticks both CIAs, the VIC, and
 * SID by that many cycles, and updates the CPU's IRQ/NMI lines: CIA1
 * and VIC-II raster IRQs both feed /IRQ (real hardware wired-OR), CIA2
 * feeds /NMI (SID has no interrupt output). On a bad line instead (see
 * vic_take_badline_stall()): cpu_step() isn't called at all - the CPU
 * makes zero progress this call, only the CIAs/VIC/SID tick,
 * simulating a real /RDY stall (/RDY only pauses the CPU's own
 * instruction fetch/execute - every other chip keeps running off the
 * shared PHI2 clock regardless, same reason CIA1/CIA2/VIC already
 * ticked through a stall before SID existed). Either
 * way, returns the cycle count actually consumed. Callers driving a
 * whole video frame's worth of execution (e.g. gtk/main.c) should call
 * this in a loop rather than calling cpu_step() directly - ticking the
 * CIAs only once per frame, in a single large batch, would make their
 * timers grossly imprecise and could miss interrupts entirely. */
int machine_step(Machine *m);

/* pa_bit/pb_bit: 0-7, see key_matrix's own comment above. */
void machine_set_key(Machine *m, int pa_bit, int pb_bit, int pressed);

/* The VIC's currently selected 16K RAM bank (0-3), resolved from
 * CIA2 PRA bits 0-1 (active-low, hence the inversion) - a caller
 * rendering a frame (e.g. gtk/main.c) needs this to pass to
 * vic_render_frame(). */
uint8_t machine_vic_bank(Machine *m);

/* port: 1 or 2. bits: active-low, bits 0-4 meaningful (see joystick1/
 * joystick2's comment above) - bits 5-7 of `bits` are ignored, always
 * forced to 1. */
void machine_set_joystick(Machine *m, int port, uint8_t bits);

#endif
