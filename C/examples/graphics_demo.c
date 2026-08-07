/*
 * graphics_demo.c - a real, visually-verifiable cc64 program exercising
 * printf, poke()-based VIC-II register access, and screen/color-RAM
 * writes through nested loops - the first cc64 output ever run against
 * c64emu (see ../docs/cc64-reference.md for the language this compiles).
 *
 * cc64 has no graphics library yet (see ../ROADMAP.md), so this uses the
 * same primitive every cc64 program has available today: peek()/poke()
 * against absolute addresses, same as a hand-written asm program would
 * hit $D020/$D021/screen RAM/color RAM directly.
 */

void main(void) {
    printf("CC64 x C64EMU DEMO\n");
    printf("POKE-BASED GRAPHICS TEST\n\n");

    poke(53280, 0);   /* $D020 border = black */
    poke(53281, 6);   /* $D021 background = blue */

    int row;
    for (row = 0; row < 8; row++) {
        int col;
        for (col = 0; col < 8; col++) {
            int screen_addr;
            int color_addr;
            screen_addr = 1024 + (row + 5) * 40 + (col + 16);
            color_addr = 55296 + (row + 5) * 40 + (col + 16);
            poke(screen_addr, 160);  /* screen code $A0 - solid reversed-space block */
            poke(color_addr, 14);    /* light blue */
        }
    }

    printf("SQUARE DRAWN AT ROW %d\n", 5);
    printf("DONE.\n");
}
