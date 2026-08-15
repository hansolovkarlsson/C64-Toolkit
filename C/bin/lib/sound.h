/*
 * sound.h - SID sound helpers, built entirely on peek()/poke() against
 * the SID's own absolute register addresses ($D400-$D418), the same
 * way graphics.h works against the VIC-II.
 *
 * Independent of asm/lib/sound.inc, not a wrapper around it - see
 * graphics.h's own header comment for why (no inline-asm/FFI
 * mechanism in cc64, and asm/lib/'s macro-based, raw-register calling
 * convention doesn't match cc64's own anyway). Unlike asm/lib/
 * sound.inc, which only ever drives voice 1 (baked into its macros as
 * fixed VOICE1_* addresses), every function here takes a voice number
 * (0-2) and works on any of the SID's 3 independent voices.
 *
 * HEADER-ONLY, like string.h/print.h/graphics.h: every function you
 * #include gets fully compiled into your program whether you call it
 * or not.
 *
 * `waveform` throughout is the raw top-nibble bits of a voice's
 * control register - the same byte layout real SID hardware and
 * asm/lib/sound.inc's PLAY_SOUND both use, so it can be a single value
 * (16=triangle, 32=sawtooth, 64=pulse, 128=noise) or more than one bit
 * set at once (a combined waveform - see c64-memory-reference.md for
 * why real hardware's combined-waveform output is an analog quirk, not
 * a clean bitwise mix, and emu/'s own SID model approximates it the
 * same bitwise-AND way most software does).
 */

/* $D418 low nibble - master volume, 0-15. Also where the (unmodeled
 * here - see graphics.h/README for what's peek/poke-reachable at all)
 * filter mode bits live in the high nibble; this only ever touches the
 * low nibble; a caller wanting the filter bits too can poke(54296, ...)
 * directly. */
void sid_volume(int v) {
    poke(54296, v);
}

/* Silences all 3 voices (gate off, matching sid_silence() below) and
 * sets the master volume to maximum. Call this once, early in your
 * program, the same way asm/lib/sound.inc's SID_INIT does. */
void sid_init(void) {
    poke(54276, 0);
    poke(54283, 0);
    poke(54290, 0);
    sid_volume(15);
}

/* Sets voice n's (0-2) oscillator frequency, the 16-bit value from
 * c64-memory-reference.md's frequency table (not a musical note number
 * or Hz - see that table to convert a real pitch to this). unsigned,
 * since the full 16-bit range is a valid frequency value with no sign
 * - a plain (signed) int would need the same sign-extension-then-mask
 * care print_hex's own comment explains, whereas an unsigned `>>`
 * (see the README's "How unsigned works") gets the high byte
 * correctly with no extra care needed. */
void sid_freq(int n, unsigned int freq) {
    int base;
    base = 54272 + n * 7;
    poke(base, freq & 255);
    poke(base + 1, (freq >> 8) & 255);
}

/* Sets voice n's (0-2) pulse width - only audible when its waveform
 * includes the pulse bit (32, see sid_gate() below). 12-bit (0-4095);
 * unsigned for the same reason as sid_freq() above, even though 4095
 * alone would already fit in a signed int - kept unsigned so both
 * "16-bit register value" helpers share the same contract. */
void sid_pulse_width(int n, unsigned int width) {
    int base;
    base = 54272 + n * 7;
    poke(base + 2, width & 255);
    poke(base + 3, (width >> 8) & 15);
}

/* Sets voice n's (0-2) envelope: attack/decay/sustain/release, each
 * 0-15 (the SID's own published rate-period table indexes - see
 * c64-memory-reference.md for what each step actually means in
 * milliseconds). Packs them into the SID's real 2-register layout
 * (attack:decay and sustain:release, each as a high:low nibble pair)
 * so the caller never has to do that bit-packing by hand the way
 * asm/lib/sound.inc's PLAY_SOUND macro requires of its own callers. */
void sid_adsr(int n, int attack, int decay, int sustain, int release) {
    int base;
    base = 54272 + n * 7;
    poke(base + 5, (attack << 4) | decay);
    poke(base + 6, (sustain << 4) | release);
}

/* Writes voice n's (0-2) control register directly: waveform bits (see
 * this file's own header comment) OR'd with the gate bit (nonzero =
 * on). Real SID hardware only actually starts/restarts the attack
 * phase on a 0->1 gate transition (and begins release on 1->0) - this
 * just writes the byte, the same as asm/lib/sound.inc's macros do; it
 * doesn't track previous state to detect that edge itself. */
void sid_gate(int n, int waveform, int gate) {
    int base;
    base = 54272 + n * 7;
    if (gate) poke(base + 4, waveform | 1);
    else poke(base + 4, waveform);
}

/* Gates voice n (0-2) off - equivalent to sid_gate(n, waveform, 0), but
 * without needing to know/repeat the waveform bits, since they don't
 * matter once gate is off. */
void sid_silence(int n) {
    poke(54272 + n * 7 + 4, 0);
}

/* Fire-and-forget one-shot sound effect on voice n (0-2): sets
 * frequency and envelope, then gates on with the given waveform - the
 * general, any-voice equivalent of asm/lib/sound.inc's PLAY_SOUND
 * macro (which is voice-1-only and needs its ad/sr nibbles pre-packed
 * by the caller). Calling this again on the same voice while a
 * previous sound is still sounding retriggers the envelope and cuts
 * the old one off, same as PLAY_SOUND. */
void sid_play(int n, unsigned int freq, int waveform, int attack, int decay, int sustain, int release) {
    sid_gate(n, waveform, 0);
    sid_freq(n, freq);
    sid_adsr(n, attack, decay, sustain, release);
    sid_gate(n, waveform, 1);
}
