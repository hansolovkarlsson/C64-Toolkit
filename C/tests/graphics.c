/* graphics.h library test for cc64 - verifies each function pokes the
 * VIC-II/screen-RAM/color-RAM addresses it documents, by peek()ing the
 * bytes back and printing them (there's no real VIC-II rendering to
 * look at in mini6502.py - see README.md's "Testing"). Every expected
 * value below is worked out by hand from the addresses/bit layouts
 * graphics.h's own comments document. */

#include <graphics.h>
#include <print.h>

void main(void) {
    border_color(6);
    print_int(peek(53280)); newline(); /* 6 */

    background_color(0);
    print_int(peek(53281)); newline(); /* 0 */

    /* plot_char: top-left corner and bottom-right corner, checking the
     * row*40+col offset math at both ends of the screen. */
    plot_char(0, 0, 65, 2);
    print_int(peek(1024)); newline();  /* 65 */
    print_int(peek(55296)); newline(); /* 2 */

    plot_char(39, 24, 90, 7);
    print_int(peek(1024 + 24 * 40 + 39)); newline();  /* 90 */
    print_int(peek(55296 + 24 * 40 + 39)); newline(); /* 7 */

    /* clear_screen: check both ends of the 1000-byte fill. */
    clear_screen(1);
    print_int(peek(1024)); newline();        /* 32 */
    print_int(peek(1024 + 999)); newline();  /* 32 */
    print_int(peek(55296)); newline();       /* 1 */
    print_int(peek(55296 + 999)); newline(); /* 1 */

    /* sprite_enable: set bit 0, then bit 2 too, then clear bit 0 -
     * checking it's a real read-modify-write, not an overwrite. */
    sprite_enable(0, 1);
    print_int(peek(53269)); newline(); /* 1 */
    sprite_enable(2, 1);
    print_int(peek(53269)); newline(); /* 5 */
    sprite_enable(0, 0);
    print_int(peek(53269)); newline(); /* 4 */

    /* sprite_pos: one sprite under 256 (MSB bit stays clear), one over
     * (MSB bit gets set) - the case graphics.h's own comment calls out. */
    sprite_pos(0, 100, 50);
    print_int(peek(53248)); newline(); /* 100 */
    print_int(peek(53249)); newline(); /* 50 */
    print_int(peek(53264)); newline(); /* 0 */

    sprite_pos(1, 300, 60);
    print_int(peek(53250)); newline(); /* 44 (300 & 255) */
    print_int(peek(53251)); newline(); /* 60 */
    print_int(peek(53264)); newline(); /* 2 (bit 1 set, sprite 0's bit 0 untouched) */

    sprite_color(3, 9);
    print_int(peek(53290)); newline(); /* 9 */

    sprite_pointer(5, 13);
    print_int(peek(2045)); newline(); /* 13 */

    sprite_multicolor(0, 1);
    print_int(peek(53276)); newline(); /* 1 */

    sprite_shared_colors(4, 8);
    print_int(peek(53285)); newline(); /* 4 */
    print_int(peek(53286)); newline(); /* 8 */

    sprite_expand(0, 1, 1);
    print_int(peek(53277)); newline(); /* 1 */
    print_int(peek(53271)); newline(); /* 1 */

    sprite_priority(0, 1);
    print_int(peek(53275)); newline(); /* 1 */

    puts("DONE.");
}
