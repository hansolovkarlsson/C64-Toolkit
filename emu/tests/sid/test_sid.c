/*
 * Hand-verified checks for ../../src/sid.c - see README.md for what's
 * covered and why each expected value is derived the way it is.
 */

#include "../../src/sid.h"
#include <stdio.h>

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { fprintf(stderr, "FAIL: %s\n", msg); failures++; } \
} while (0)

enum {
    FREQLO1 = 0x00, FREQHI1 = 0x01, PWLO1 = 0x02, PWHI1 = 0x03, CR1 = 0x04, AD1 = 0x05, SR1 = 0x06,
    FREQLO2 = 0x07, FREQHI2 = 0x08, CR2 = 0x0B,
    FREQLO3 = 0x0E, FREQHI3 = 0x0F, PWLO3 = 0x10, PWHI3 = 0x11, CR3 = 0x12, AD3 = 0x13, SR3 = 0x14,
    MODEVOL = 0x18,
    POTX = 0x19, POTY = 0x1A, OSC3 = 0x1B, ENV3 = 0x1C,
};

static void set_freq(Sid *sid, int freqlo_reg, uint16_t freq) {
    sid_write(sid, (uint8_t)freqlo_reg, (uint8_t)(freq & 0xFF));
    sid_write(sid, (uint8_t)(freqlo_reg + 1), (uint8_t)(freq >> 8));
}

static void test_sawtooth_waveform(void) {
    Sid sid;
    sid_init(&sid);
    sid_write(&sid, CR3, 0x20); /* sawtooth only */
    set_freq(&sid, FREQLO3, 4096); /* accum top-12 bits == tick count exactly, for tick counts < 4096 */

    sid_tick(&sid, 100);
    CHECK(sid_read(&sid, OSC3) == 6, "sawtooth: OSC3 (top 8 of the 12-bit waveform) should read 100>>4 == 6 after 100 ticks");
    sid_tick(&sid, 100); /* 200 total */
    CHECK(sid_read(&sid, OSC3) == 12, "sawtooth: should ramp monotonically - 200>>4 == 12 after 200 ticks");
}

static void test_triangle_waveform(void) {
    Sid sid;
    sid_init(&sid);
    sid_write(&sid, CR3, 0x10); /* triangle only */
    set_freq(&sid, FREQLO3, 0x2000);

    sid_tick(&sid, 1024); /* accum == 1024*0x2000 == 0x800000 exactly - the ramp's midpoint, where the MSB flips and triangle peaks */
    CHECK(sid_read(&sid, OSC3) == 255, "triangle: should peak (0xFFF, top8=255) exactly at the accumulator's midpoint");

    sid_tick(&sid, 512); /* accum == 0xC00000 - past the midpoint, descending */
    CHECK(sid_read(&sid, OSC3) == 127, "triangle: should descend past the midpoint, not keep rising");
}

static void test_pulse_waveform(void) {
    Sid sid;
    sid_init(&sid);
    sid_write(&sid, CR3, 0x40); /* pulse only */
    set_freq(&sid, FREQLO3, 4096); /* accum top-12 == tick count, same trick as the sawtooth test */
    sid_write(&sid, PWLO3, 0x00);
    sid_write(&sid, PWHI3, 0x08); /* pulse width == 0x800 (2048), half-scale duty cycle */

    sid_tick(&sid, 100); /* accum top-12 == 100 < 2048 */
    CHECK(sid_read(&sid, OSC3) == 0, "pulse: should be low while the accumulator is below the pulse width");

    sid_tick(&sid, 2900); /* 3000 total, accum top-12 == 3000 >= 2048 */
    CHECK(sid_read(&sid, OSC3) == 255, "pulse: should snap high once the accumulator reaches the pulse width");
}

static void test_combined_waveform_is_bitwise_and(void) {
    Sid sid;
    sid_init(&sid);
    sid_write(&sid, CR3, 0x30); /* sawtooth AND triangle both selected */
    set_freq(&sid, FREQLO3, 4096);

    sid_tick(&sid, 100);
    /* Sawtooth at tick 100: (accum>>12)&0xFFF == 100 == 0x064.
     * Triangle at the same accum (409600, well below the 0x800000
     * midpoint, so no inversion): (accum>>11)&0xFFF == 200 == 0x0C8.
     * 0x064 & 0x0C8 == 0x040 (64), top 8 bits == 64>>4 == 4. */
    CHECK(sid_read(&sid, OSC3) == 4, "combined waveform: multiple selected waveforms should bitwise-AND together (documented approximation, see sid.h)");
}

static void test_noise_lfsr_shift(void) {
    Sid sid;
    sid_init(&sid);
    sid_write(&sid, CR3, 0x80); /* noise only */
    set_freq(&sid, FREQLO3, 32768); /* accum crosses bit19 (0x80000) exactly once, at tick 16 */

    sid_tick(&sid, 15);
    CHECK(sid.lfsr[2] == 0x7FFFFF, "noise: LFSR should not have shifted yet - accumulator hasn't reached bit19 (0x80000)");

    sid_tick(&sid, 1); /* 16 total - accum == 524288 == 0x80000, bit19's rising edge */
    /* Hand-derived from the reset seed 0x7FFFFF: feedback = (bit22 ^
     * bit17) of 0x7FFFFF == (1 ^ 1) == 0, so the new bit0 is 0 - the
     * shift clears exactly the low bit, giving 0x7FFFFE. */
    CHECK(sid.lfsr[2] == 0x7FFFFE, "noise: LFSR should shift exactly once when accumulator bit19 rises, feedback bit computed from the reset seed");

    sid_tick(&sid, 4); /* still within the same bit19 "high" block - no new rising edge */
    CHECK(sid.lfsr[2] == 0x7FFFFE, "noise: LFSR should not shift again until bit19 falls and rises again");
}

static void test_hard_sync(void) {
    Sid sid;
    sid_init(&sid);
    /* Voice 2's (index 1) sync source is voice 1 (index 0) - see
     * sid.h's fixed ring wiring (v+2)%3. Voice 1 runs fast (freq
     * 0xFFFF) so its accumulator's MSB rises (crossing the 0x800000
     * midpoint) at a precisely computable tick; voice 2 runs slow
     * (freq 1) so its own un-synced growth is trivial to track. */
    set_freq(&sid, FREQLO1, 0xFFFF);
    set_freq(&sid, FREQLO2, 1);
    sid_write(&sid, CR2, 0x02); /* SYNC bit only */

    /* accum[0] after 128 ticks: 128*65535 == 8388480 (< 0x800000 ==
     * 8388608, MSB still 0). After 129 ticks: 129*65535 == 8454015
     * (>= 8388608, MSB now 1) - the sync-triggering cycle. */
    sid_tick(&sid, 129);
    CHECK(sid.accum[1] == 0, "hard sync: destination accumulator should be forced to 0 the instant the source's MSB rises, overriding its own tentative advance");

    sid_tick(&sid, 11); /* 140 total - 11 more ticks of voice 2's own freq=1 growth since the sync reset */
    CHECK(sid.accum[1] == 11, "hard sync: destination should resume accumulating normally (freq=1) after the reset, not stay pinned at 0");
}

static void test_attack_linear_and_transitions_to_decay(void) {
    Sid sid;
    sid_init(&sid);
    sid_write(&sid, AD3, 0x00); /* attack rate 0 -> ENV_RATE_PERIOD[0] == 9 cycles/step */
    sid_write(&sid, SR3, 0x00);
    sid_write(&sid, CR3, 0x01); /* GATE on -> attack begins */

    sid_tick(&sid, 2294); /* floor(2294/9) == 254 completed steps */
    CHECK(sid_read(&sid, ENV3) == 254, "attack: linear, exactly one step every ENV_RATE_PERIOD[rate] cycles - 254 steps after 2294 cycles");

    sid_tick(&sid, 1); /* 2295 total == 255*9 exactly - the 255th and final step */
    CHECK(sid_read(&sid, ENV3) == 255, "attack: should reach 255 exactly at 255 full rate periods");
}

static void test_decay_stops_at_sustain_level(void) {
    Sid sid;
    sid_init(&sid);
    /* Skip the ramp-up by poking the envelope directly into a settled
     * Attack-complete state, the same white-box approach ../machine/
     * already uses where no register exposes the internal state being
     * checked (there's no way to read an envelope's internal STATE,
     * only its current 8-bit value via ENV3). */
    sid.env_value[2] = 255;
    sid.env_state[2] = SID_ENV_DECAY_SUSTAIN;
    sid_write(&sid, AD3, 0x00); /* decay rate 0 (fastest) */
    sid_write(&sid, SR3, 0x50); /* sustain nibble 5 -> target 5*0x11 == 0x55 == 85 */

    sid_tick(&sid, 7000); /* generous budget - the full 255->0 decay needs well under 7000 cycles even through every exponential-divisor bracket */
    CHECK(sid_read(&sid, ENV3) == 85, "decay: should settle exactly at the sustain level (nibble*0x11), not keep decaying toward 0");

    sid_tick(&sid, 5000);
    CHECK(sid_read(&sid, ENV3) == 85, "decay: should stay pinned at the sustain level once reached, not undershoot");
}

static void test_release_reaches_zero(void) {
    Sid sid;
    sid_init(&sid);
    sid.env_value[2] = 85;
    sid.env_state[2] = SID_ENV_RELEASE;
    sid_write(&sid, SR3, 0x00); /* release rate 0 (fastest) */

    sid_tick(&sid, 8000); /* generous budget for a full 85->0 release */
    CHECK(sid_read(&sid, ENV3) == 0, "release: should reach 0 given enough time");

    sid_tick(&sid, 1000);
    CHECK(sid_read(&sid, ENV3) == 0, "release: should stay at 0, not wrap or go negative");
}

static void test_exponential_decay_is_slower_at_low_values(void) {
    Sid sid;
    sid_init(&sid);
    sid_write(&sid, AD3, 0x00); /* decay rate 0 -> period 9, so exactly 200/9 == 22 rate-period matches occur in 200 cycles */
    sid_write(&sid, SR3, 0x00); /* sustain 0 - always still above target, keeps decrementing throughout */

    sid.env_value[2] = 200; /* > 93 -> exponential divisor 1: every rate-period match actually steps */
    sid.env_state[2] = SID_ENV_DECAY_SUSTAIN;
    sid_tick(&sid, 200);
    CHECK(sid_read(&sid, ENV3) == 178, "exponential decay: at divisor 1, should lose exactly one step per rate period - 200-22 == 178");

    Sid sid_low;
    sid_init(&sid_low);
    sid_write(&sid_low, AD3, 0x00);
    sid_write(&sid_low, SR3, 0x00);
    sid_low.env_value[2] = 10; /* in the 6-13 bracket -> divisor 16, so only floor(200/(9*16)) == 1 step occurs in the same 200 cycles */
    sid_low.env_state[2] = SID_ENV_DECAY_SUSTAIN;
    sid_tick(&sid_low, 200);
    CHECK(sid_read(&sid_low, ENV3) == 9, "exponential decay: the SAME 200-cycle budget should produce far fewer steps at a low envelope value, demonstrating the real slowdown");
}

static void test_gate_edge_handling(void) {
    Sid sid;
    sid_init(&sid);

    sid_write(&sid, CR3, 0x01); /* GATE rising edge */
    CHECK(sid.env_state[2] == SID_ENV_ATTACK, "gate: rising edge should switch straight to Attack, without waiting for a tick");

    sid.env_value[2] = 200; /* simulate a partially-completed attack */
    sid_write(&sid, CR3, 0x00); /* GATE falling edge */
    CHECK(sid.env_state[2] == SID_ENV_RELEASE, "gate: falling edge should switch straight to Release");
    CHECK(sid.env_value[2] == 200, "gate: release should start from wherever the envelope currently sits, not reset first");

    sid_write(&sid, CR3, 0x01); /* re-trigger: GATE rising edge again, mid-release */
    CHECK(sid.env_state[2] == SID_ENV_ATTACK, "gate: re-trigger should switch back to Attack");
    CHECK(sid.env_value[2] == 200, "gate: re-triggered attack should CONTINUE from the current envelope value, not reset to 0 - real hardware behavior");
}

static void test_register_read_write_semantics(void) {
    Sid sid;
    sid_init(&sid);

    sid_write(&sid, FREQLO1, 0x42);
    CHECK(sid_read(&sid, FREQLO1) == 0x42, "registers: plain read/write for a normal register");

    CHECK(sid_read(&sid, POTX) == 0xFF, "POTX: no paddle hardware modeled, should read as unconnected (0xFF)");
    CHECK(sid_read(&sid, POTY) == 0xFF, "POTY: same");

    sid_write(&sid, 0x1B, 0x55); /* OSC3 is read-only - this write must be a no-op */
    sid_write(&sid, 0x1C, 0x55); /* ENV3 likewise */
    CHECK(sid_read(&sid, OSC3) == 0, "OSC3: writes should be ignored - with no waveform selected on voice 3, it should read 0");
    CHECK(sid_read(&sid, ENV3) == 0, "ENV3: writes should be ignored - voice 3's envelope hasn't been gated on, should read 0");

    sid_write(&sid, 0x1D, 0x77); /* unused register */
    CHECK(sid_read(&sid, 0x1D) == 0x00, "unused registers ($1D-$1F): should read 0 regardless of what's written");
}

int main(void) {
    test_sawtooth_waveform();
    test_triangle_waveform();
    test_pulse_waveform();
    test_combined_waveform_is_bitwise_and();
    test_noise_lfsr_shift();
    test_hard_sync();
    test_attack_linear_and_transitions_to_decay();
    test_decay_stops_at_sustain_level();
    test_release_reaches_zero();
    test_exponential_decay_is_slower_at_low_values();
    test_gate_edge_handling();
    test_register_read_write_semantics();

    if (failures == 0) {
        printf("PASS: all SID checks passed\n");
        return 0;
    }
    fprintf(stderr, "FAIL: %d check(s) failed\n", failures);
    return 1;
}
