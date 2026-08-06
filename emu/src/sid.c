/*
 * sid.c - see sid.h's header comment for scope and the two documented
 * simplifications (no filter, AND-combined waveforms).
 */

#include "sid.h"
#include <string.h>

/* Real hardware's rate-period table (SID clock cycles a voice's rate
 * counter must reach before an attack/decay/release step is even
 * attempted), indexed by the 4-bit attack/decay/release nibble - the
 * widely-published SID reverse-engineering figures every software SID
 * emulator draws on, not independently re-verified against real
 * silicon by this project (see sid.h's header comment). */
static const uint16_t ENV_RATE_PERIOD[16] = {
    9, 32, 63, 95, 149, 220, 267, 313,
    392, 977, 1954, 3126, 3906, 11720, 19531, 31251,
};

/* Real hardware's decay/release envelope isn't linear - it approximates
 * an analog RC discharge by only actually stepping on 1-in-N rate-
 * period matches, where N depends on the CURRENT envelope value (higher
 * envelope values step every time, low ones step rarely) - see
 * advance_envelope(). Attack has no such divisor, it's linear. Widely-
 * published breakpoints, same citation as ENV_RATE_PERIOD above. */
static int env_exponential_divisor(uint8_t value) {
    if (value > 93) return 1;
    if (value > 53) return 2;
    if (value > 25) return 4;
    if (value > 13) return 8;
    if (value > 5) return 16;
    return 30;
}

void sid_init(Sid *sid) {
    memset(sid, 0, sizeof(*sid));
    for (int v = 0; v < 3; v++) sid->lfsr[v] = 0x7FFFFF; /* real chip's documented reset state - an all-zero LFSR would otherwise be a stuck state its own XOR feedback could never escape */
}

/* Shared between sid_output()'s mixing and sid_read()'s $D41B (OSC3):
 * this voice's current 12-bit waveform value, combining whichever of
 * triangle/sawtooth/pulse/noise its control register selects via
 * bitwise AND (see sid.h's header comment on why AND, not exact) - 0
 * if none are selected (real chip: silence). */
static uint16_t compute_voice_waveform(const Sid *sid, int v) {
    uint8_t ctrl = sid->regs[v * 7 + 4];
    int triangle_sel = (ctrl >> 4) & 1;
    int saw_sel = (ctrl >> 5) & 1;
    int pulse_sel = (ctrl >> 6) & 1;
    int noise_sel = (ctrl >> 7) & 1;
    int ring = (ctrl >> 2) & 1;

    uint16_t combined = 0xFFF;
    int any = 0;

    if (saw_sel) {
        combined &= (uint16_t)((sid->accum[v] >> 12) & 0xFFF);
        any = 1;
    }
    if (triangle_sel) {
        /* Ring modulation ($D01x-style control bit2, voice-specific
         * here): XORs in the SOURCE voice's accumulator MSB instead of
         * this voice's own, only meaningful for the triangle waveform
         * - real hardware behavior, not something that affects
         * sawtooth/pulse/noise. Source is the fixed "previous voice in
         * the ring": voice 1<-3, voice 2<-1, voice 3<-2. */
        int src = (v + 2) % 3;
        uint32_t msb_source = ring ? sid->accum[src] : sid->accum[v];
        uint32_t msb = (msb_source >> 23) & 1;
        uint32_t acc = sid->accum[v];
        uint32_t tri_acc = msb ? (~acc & 0xFFFFFF) : acc;
        combined &= (uint16_t)((tri_acc >> 11) & 0xFFF);
        any = 1;
    }
    if (pulse_sel) {
        uint16_t pw = (uint16_t)(sid->regs[v * 7 + 2] | ((sid->regs[v * 7 + 3] & 0x0F) << 8));
        uint16_t acc_top12 = (uint16_t)((sid->accum[v] >> 12) & 0xFFF);
        combined &= (acc_top12 >= pw) ? (uint16_t)0xFFF : (uint16_t)0x000;
        any = 1;
    }
    if (noise_sel) {
        /* SIMPLIFICATION: real hardware taps 8 specific NON-contiguous
         * bits of the 23-bit LFSR into the 12-bit output; this uses a
         * plain contiguous top-8-bits slice instead - see sid.h's
         * header comment. */
        uint16_t noise_val = (uint16_t)(((sid->lfsr[v] >> 15) & 0xFF) << 4);
        combined &= noise_val;
        any = 1;
    }
    return any ? combined : 0;
}

static void advance_envelope(Sid *sid, int v) {
    uint8_t ad = sid->regs[v * 7 + 5];
    uint8_t sr = sid->regs[v * 7 + 6];
    uint8_t attack = (ad >> 4) & 0x0F;
    uint8_t decay = ad & 0x0F;
    uint8_t sustain_nibble = (sr >> 4) & 0x0F;
    uint8_t release = sr & 0x0F;
    uint8_t sustain_level = (uint8_t)(sustain_nibble * 0x11); /* nibble replicated across the full byte, e.g. 0xA -> 0xAA, the standard scaling from a 4-bit register to the 8-bit envelope range */

    uint8_t rate = (sid->env_state[v] == SID_ENV_ATTACK) ? attack
                 : (sid->env_state[v] == SID_ENV_DECAY_SUSTAIN) ? decay
                 : release;

    sid->env_rate_counter[v]++;
    if (sid->env_rate_counter[v] < ENV_RATE_PERIOD[rate]) return;
    sid->env_rate_counter[v] = 0;

    if (sid->env_state[v] == SID_ENV_ATTACK) {
        if (sid->env_value[v] < 0xFF) sid->env_value[v]++;
        if (sid->env_value[v] == 0xFF) sid->env_state[v] = SID_ENV_DECAY_SUSTAIN;
        return;
    }

    /* Decay/sustain and release both use the real chip's exponential
     * approximation to an analog RC discharge - attack (above) doesn't. */
    sid->env_exp_counter[v]++;
    if (sid->env_exp_counter[v] < env_exponential_divisor(sid->env_value[v])) return;
    sid->env_exp_counter[v] = 0;

    if (sid->env_state[v] == SID_ENV_DECAY_SUSTAIN) {
        if (sid->env_value[v] > sustain_level) sid->env_value[v]--;
    } else {
        if (sid->env_value[v] > 0) sid->env_value[v]--;
    }
}

uint8_t sid_read(Sid *sid, uint8_t reg) {
    reg &= 0x1F;
    if (reg == 0x19 || reg == 0x1A) return 0xFF; /* POTX/POTY - no paddle hardware modeled */
    if (reg == 0x1B) return (uint8_t)(compute_voice_waveform(sid, 2) >> 4); /* OSC3 */
    if (reg == 0x1C) return sid->env_value[2]; /* ENV3 */
    if (reg >= 0x1D) return 0x00; /* unused */
    return sid->regs[reg];
}

void sid_write(Sid *sid, uint8_t reg, uint8_t v) {
    reg &= 0x1F;
    if (reg > 0x18) return; /* $D419-$D41F: read-only outputs or unused - real chip ignores writes here */

    if (reg % 7 == 4) { /* one of the 3 voice control registers (4, 11, 18) */
        int voice = reg / 7;
        uint8_t gate = v & 0x01;
        if (gate && !sid->gate_prev[voice]) {
            sid->env_state[voice] = SID_ENV_ATTACK; /* real hardware: attack continues from wherever the envelope currently sits, doesn't reset to 0 */
        } else if (!gate && sid->gate_prev[voice]) {
            sid->env_state[voice] = SID_ENV_RELEASE;
        }
        sid->gate_prev[voice] = gate;
        if (v & 0x08) { /* TEST bit: hold this voice's oscillator and noise LFSR reset */
            sid->accum[voice] = 0;
            sid->lfsr[voice] = 0x7FFFFF;
        }
    }
    sid->regs[reg] = v;
}

void sid_tick(Sid *sid, int cycles) {
    for (int c = 0; c < cycles; c++) {
        uint8_t old_msb[3], old_b19[3], test[3];
        for (int v = 0; v < 3; v++) {
            old_msb[v] = (uint8_t)((sid->accum[v] >> 23) & 1);
            old_b19[v] = (uint8_t)((sid->accum[v] >> 19) & 1);
            test[v] = (uint8_t)((sid->regs[v * 7 + 4] & 0x08) != 0);
        }

        for (int v = 0; v < 3; v++) {
            if (test[v]) {
                sid->accum[v] = 0;
            } else {
                uint16_t freq = (uint16_t)(sid->regs[v * 7] | (sid->regs[v * 7 + 1] << 8));
                sid->accum[v] = (sid->accum[v] + freq) & 0xFFFFFF;
            }
        }

        /* Hard sync: a voice with its SYNC bit (control register bit1)
         * set has its accumulator forced to 0 the instant the SOURCE
         * voice's (see compute_voice_waveform()'s comment on the fixed
         * ring wiring) accumulator MSB rises 0->1, i.e. the source just
         * wrapped - checked against old_msb (captured before this
         * cycle's update) vs. the source's just-updated value, so a
         * sync destination can't also BE the source of its own check
         * mid-update. */
        for (int v = 0; v < 3; v++) {
            uint8_t sync = (uint8_t)((sid->regs[v * 7 + 4] & 0x02) != 0);
            if (!sync) continue;
            int src = (v + 2) % 3;
            uint8_t new_src_msb = (uint8_t)((sid->accum[src] >> 23) & 1);
            if (!old_msb[src] && new_src_msb) sid->accum[v] = 0;
        }

        /* Noise LFSR: shifts once per rising edge of THIS voice's own
         * accumulator bit19 - real hardware ties the noise "clock" to
         * the same oscillator the other waveforms use, which is why
         * noise pitch is controllable via the frequency register just
         * like they are. 23-bit Fibonacci LFSR, taps at bits 22 and 17
         * feeding bit0 - see sid.h's header comment for the (simplified)
         * output tap. */
        for (int v = 0; v < 3; v++) {
            if (test[v]) { sid->lfsr[v] = 0x7FFFFF; continue; }
            uint8_t new_b19 = (uint8_t)((sid->accum[v] >> 19) & 1);
            if (!old_b19[v] && new_b19) {
                uint32_t fb = ((sid->lfsr[v] >> 22) ^ (sid->lfsr[v] >> 17)) & 1;
                sid->lfsr[v] = (uint32_t)(((sid->lfsr[v] << 1) | fb) & 0x7FFFFF);
            }
        }

        for (int v = 0; v < 3; v++) advance_envelope(sid, v);
    }
}

int16_t sid_output(const Sid *sid) {
    uint8_t modevol = sid->regs[0x18];
    int voice3_off = (modevol >> 7) & 1;
    uint8_t volume = modevol & 0x0F;

    int64_t mix = 0;
    for (int v = 0; v < 3; v++) {
        if (v == 2 && voice3_off) continue;
        uint16_t wave = compute_voice_waveform(sid, v);
        mix += (int64_t)wave * sid->env_value[v];
    }

    /* Linear mix + volume scaling only - NO filter (see sid.h). Scaled
     * into the low half of int16_t's range (0..32767), not AC-centered
     * - see sid_output()'s own doc comment in sid.h for why that's
     * deliberate. */
    int64_t max_mix = (int64_t)3 * 0xFFF * 0xFF;
    int64_t scaled = (mix * volume * 32767) / (15 * max_mix);
    if (scaled > 32767) scaled = 32767;
    if (scaled < 0) scaled = 0;
    return (int16_t)scaled;
}
