#ifndef C64EMU_SID_H
#define C64EMU_SID_H

#include <stdint.h>

/* SID (MOS 6581/8580, ../ROADMAP.md step 7): the 3-voice synth chip.
 * Each voice has an independent 24-bit phase-accumulator oscillator
 * (triangle/sawtooth/pulse/noise waveforms, hard sync and ring
 * modulation against a fixed "previous voice in the ring" - voice 1's
 * source is voice 3, voice 2's is voice 1, voice 3's is voice 2, real
 * hardware wiring) and an independent ADSR envelope generator. This is
 * a CHIP MODEL ONLY - it doesn't know about sample rates, audio
 * buffers, or an OS audio API, the same way vic.c doesn't know about
 * cairo: sid_tick() advances internal state by real SID clock cycles
 * (call it the same way machine_step() already calls cia_tick()/
 * vic_tick(), once per CPU instruction - SID shares the CPU's PHI2
 * clock on real hardware, no rate conversion needed here), and
 * sid_output() pulls the CURRENT instantaneous mixed sample - a caller
 * wanting actual audio at e.g. 44100 Hz would tick cycle-for-cycle
 * alongside the CPU and call sid_output() at whatever cadence produces
 * that rate. That wiring (and the GTK/OS audio backend it needs) is
 * NOT part of this module - see ../ROADMAP.md.
 *
 * Two real-hardware behaviors are deliberately approximated, not
 * exactly modeled, both flagged where they happen in sid.c:
 * - The analog filter ($D415-$D418's cutoff/resonance/routing/mode
 *   bits) is stored as plain read-back-what-was-written register
 *   state but never actually applied to the output - see
 *   ../ROADMAP.md's step 7 note. sid_output() is a pure linear mix of
 *   the 3 voices' waveform*envelope products, scaled by the volume
 *   nibble.
 * - Combined waveforms (more than one of triangle/sawtooth/pulse/
 *   noise selected at once in a voice's control register) use the
 *   common bitwise-AND-of-the-individually-computed-waveforms software
 *   approximation - real combined waveforms are an idiosyncratic
 *   analog quirk of each physical chip, not something a clean digital
 *   model reproduces exactly.
 *
 * Everything else - oscillator frequency/waveform generation, hard
 * sync, ring modulation, the noise LFSR's shift/feedback mechanism,
 * and the ADSR envelope's real rate-period and exponential-decay
 * tables - is modeled directly against the widely-published SID
 * reverse-engineering documentation (the same body of knowledge every
 * software SID emulator draws on), not independently re-verified
 * against real silicon by this project - same spirit as
 * VIC_BADLINE_STALL_CYCLES's or Pepto's palette's own citations
 * elsewhere in this codebase. The one specific simplification within
 * that: real hardware taps 8 specific NON-contiguous bits of the
 * 23-bit noise LFSR into its 12-bit waveform output; this model uses
 * a contiguous top-8-bits slice instead - see sid.c's
 * compute_voice_waveform().
 */
typedef enum {
    SID_ENV_ATTACK,
    SID_ENV_DECAY_SUSTAIN,
    SID_ENV_RELEASE,
} SidEnvState;

typedef struct {
    uint8_t regs[32]; /* $D400-$D41F, mirrored across all of $D400-$D7FF - registers 0x00-0x18 are real read/write storage (3 voices at 7 bytes each: FREQLO/FREQHI/PWLO/PWHI/CR/AD/SR, then FCLO/FCHI/RESFILT/MODEVOL); 0x19-0x1A (POTX/POTY) and 0x1B-0x1C (OSC3/ENV3) are read-only, computed rather than stored - see sid_read(); 0x1D-0x1F are unused. */

    uint32_t accum[3]; /* 24-bit phase accumulators, one per voice - top bits select waveform output, see sid.c's compute_voice_waveform() */
    uint32_t lfsr[3];  /* 23-bit noise shift registers, one per voice - reset to all-1s (0x7FFFFF) by sid_init() and by that voice's TEST bit, matching real hardware's documented reset state */
    uint8_t gate_prev[3]; /* previous GATE bit (voice control register bit0) per voice, so sid_write() can detect rising/falling edges and switch envelope state immediately, the same way real hardware does - NOT something sid_tick() polls for */

    SidEnvState env_state[3];
    uint8_t env_value[3];         /* 0-255, this voice's current envelope generator output */
    uint32_t env_rate_counter[3]; /* counts SID cycles up to the current phase's rate period (ENV_RATE_PERIOD[]) before an attack/decay/release step is even attempted */
    uint8_t env_exp_counter[3];   /* decay/release only: counts rate-period matches up to the current exponential divisor (see sid.c's env_exponential_divisor()) before actually stepping - attack is linear, doesn't use this */
} Sid;

void sid_init(Sid *sid);

/* `reg` must already be reduced to a register number (`addr & 0x1F`) -
 * real hardware only decodes 5 address lines within $D400-$D7FF, the
 * same mirroring convention VIC-II's registers use across $D000-$D3FF
 * (see vic_read()'s own comment) - the caller (machine.c, once SID is
 * wired in) owns that reduction, not this file.
 *
 * Writing a voice's control register ($04/$0B/$12) is where GATE
 * edges get detected and acted on (switching that voice's envelope
 * into Attack or Release immediately - real hardware behavior, not
 * something sid_tick() has to poll for) and where the TEST bit (bit3)
 * resets that voice's accumulator and noise LFSR. $D419-$D41F write as
 * a no-op - $19/$1A (POTX/POTY) and $1B/$1C (OSC3/ENV3) are read-only
 * outputs on real hardware, and $1D-$1F are unused.
 *
 * Reading $19/$1A (POTX/POTY) returns 0xFF - SIMPLIFICATION: no
 * paddle/potentiometer hardware is modeled, matching this codebase's
 * existing "unconnected input reads as 1" convention used elsewhere
 * (memory.c's 6510 I/O port, vic_color_ram_read()'s upper nibble).
 * Reading $1B (OSC3) returns voice 3's CURRENT waveform output's top 8
 * bits (real software - notably C64 BASIC's RND-via-SID trick - reads
 * this off the noise waveform for randomness); $1C (ENV3) returns
 * voice 3's current 8-bit envelope value directly. Every other
 * register reads back what was last written. */
uint8_t sid_read(Sid *sid, uint8_t reg);
void sid_write(Sid *sid, uint8_t reg, uint8_t v);

/* Advances every voice's oscillator, noise LFSR, and envelope
 * generator by `cycles` real SID clock cycles - call this the same way
 * machine_step() already calls cia_tick()/vic_tick(), once per CPU
 * instruction (SID shares the CPU's PHI2 clock, no rate conversion
 * needed). Internally loops cycle-by-cycle (`cycles` is always small in
 * practice - one CPU instruction's worth, 2-7, or one bad-line stall's
 * worth, comfortably under 64 - matching the same "cycles must be
 * small" contract vic_tick() documents) rather than batching the
 * accumulator math, specifically so hard sync and the noise LFSR's
 * clock (both edge-triggered off an accumulator bit crossing 0->1)
 * stay exact even when a single tick call spans a wrap - no
 * "at most one crossing per call" assumption needed here, unlike
 * vic_tick()'s raster-line boundary check. */
void sid_tick(Sid *sid, int cycles);

/* Returns the CURRENT instantaneous mixed output sample: each voice's
 * 12-bit waveform value times its 8-bit envelope value, summed (voice
 * 3 excluded if $D418 bit7 - "voice 3 off" - is set, a real feature
 * songs use to free voice 3 for $D41B/$D41C oscillator-read tricks
 * without also hearing it), then scaled by the volume nibble ($D418
 * bits0-3) into the low half of the int16_t range (0..32767) - NOT
 * AC-centered around 0. That's deliberate, not an oversight: real SID
 * output actually IS a DC-biased signal at the chip's output pin
 * (this is exactly why abrupt volume-register changes cause the
 * well-known "SID click/pop" on real hardware). gtk/main.c's playback
 * code feeds this value straight through as-is rather than shifting it
 * - silence is always exactly 0 this way, which matters more than it
 * sounds: an earlier version DID shift it, and every routine audio
 * ring-buffer underrun (GTK's timer and a real-time audio thread never
 * stay perfectly in lockstep) then produced an audible click, jumping
 * between the shifted "silence" and the buffer's own unshifted 0 fill
 * value - see gtk/main.c's tick() for the full story. NO filter is
 * applied - see this file's header comment. */
int16_t sid_output(const Sid *sid);

#endif
