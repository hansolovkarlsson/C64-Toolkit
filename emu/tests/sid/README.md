# SID correctness checks

`test_sid.c` is a hand-written, hand-verified correctness check for
`../../src/sid.c`, the same way `../cia/` checks `cia.c` - there's no
third-party test suite for the SID the way `../cpu/` has Klaus
Dormann's for the 6502, so every expected value here was worked out by
hand against the register/waveform/envelope semantics documented in
`sid.h`'s own header comment (which is itself drawn from the widely-
published SID reverse-engineering documentation every software SID
emulator relies on, not independently re-verified against real silicon
- see that header comment for exactly which two behaviors are
deliberately approximated rather than modeled exactly: the analog
filter, not applied at all yet, and combined waveforms, using the
common bitwise-AND approximation).

Covers: sawtooth/triangle/pulse waveform shapes (each checked via
voice 3's $D41B/OSC3 read-back, which exposes a voice's current 12-bit
waveform value independent of its envelope - handy for testing
waveform generation in isolation); a bitwise-AND'd combined waveform;
the noise LFSR's shift/feedback mechanism, checked against a hand-
computed exact expected value; hard sync forcing a voice's accumulator
to 0 at the precise cycle its sync source's MSB crosses the midpoint of
its own ramp (verified via a fully hand-derived cycle count, checking
the destination voice's internal accumulator directly - there's no
register that exposes it, the same class of white-box check
`../machine/` already uses for the CPU's PC/cycle count during a
bad-line stall); the ADSR envelope's linear attack timing (exact,
since attack has no exponential divisor), its exponential decay/release
slowdown at low envelope values (checked qualitatively - the same fixed
cycle budget produces far fewer steps from a low starting value than a
high one, directly demonstrating the "exponential" shape rather than
re-deriving its exact per-step cycle counts by hand); decay correctly
stopping at the sustain level rather than continuing to 0; gate-edge
handling (attack/release triggered immediately on write, not on the
next tick; attack resumes from wherever the envelope currently sits
rather than resetting to 0); and register read/write semantics
($D419-$D41F's read-only/unused registers, $D41B/$D41C's live
OSC3/ENV3 values, $D419/$D41A's no-paddle-hardware placeholder).

Does NOT cover wiring SID into the C64 address map ($D400-$D7FF
mirroring, machine.c's I/O dispatch) or actual audio output (sample-
rate conversion, an OS audio API) - neither exists yet, see
`../../ROADMAP.md`.

```sh
make run
```
