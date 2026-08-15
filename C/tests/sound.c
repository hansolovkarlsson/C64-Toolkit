/* sound.h library test for cc64 - verifies each function pokes the
 * SID's registers it documents, by peek()ing the bytes back and
 * printing them (there's no real audio output to listen to in
 * mini6502.py - see README.md's "Testing"). Every expected value below
 * is worked out by hand from sound.h's own register-layout comments. */

#include <sound.h>
#include <print.h>

void main(void) {
    sid_init();
    print_int(peek(54296)); newline(); /* 15 (volume) */
    print_int(peek(54276)); newline(); /* 0 (voice 0 ctrl) */
    print_int(peek(54283)); newline(); /* 0 (voice 1 ctrl) */
    print_int(peek(54290)); newline(); /* 0 (voice 2 ctrl) */

    sid_volume(7);
    print_int(peek(54296)); newline(); /* 7 */

    /* sid_freq: one value under 256 (high byte 0), one that exercises
     * the full 16-bit unsigned range - see sound.h's own comment on
     * why freq is unsigned. */
    sid_freq(0, 1000);
    print_int(peek(54272)); newline(); /* 232 (1000 & 255) */
    print_int(peek(54273)); newline(); /* 3   (1000 >> 8) */

    sid_freq(1, 60000);
    print_int(peek(54279)); newline(); /* 96  (60000 & 255) */
    print_int(peek(54280)); newline(); /* 234 (60000 >> 8) */

    /* sid_pulse_width: 12-bit, so the high byte is masked to 4 bits. */
    sid_pulse_width(2, 2500);
    print_int(peek(54288)); newline(); /* 196 (2500 & 255) */
    print_int(peek(54289)); newline(); /* 9   ((2500 >> 8) & 15) */

    /* sid_adsr: checks the nibble-packing into the SID's real 2-register layout. */
    sid_adsr(0, 9, 2, 12, 5);
    print_int(peek(54277)); newline(); /* 146 ((9 << 4) | 2) */
    print_int(peek(54278)); newline(); /* 197 ((12 << 4) | 5) */

    sid_gate(0, 16, 1);
    print_int(peek(54276)); newline(); /* 17 (16 | 1) */

    sid_silence(0);
    print_int(peek(54276)); newline(); /* 0 */

    /* sid_play: one call sets frequency, envelope, and gates on. */
    sid_play(1, 5000, 64, 0, 9, 15, 6);
    print_int(peek(54279)); newline(); /* 136 (5000 & 255) */
    print_int(peek(54280)); newline(); /* 19  (5000 >> 8) */
    print_int(peek(54284)); newline(); /* 9   ((0 << 4) | 9) */
    print_int(peek(54285)); newline(); /* 246 ((15 << 4) | 6) */
    print_int(peek(54283)); newline(); /* 65  (64 | 1) */

    puts("DONE.");
}
