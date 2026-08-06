# VIC-II correctness checks

`test_vic.c` is a hand-written, hand-verified correctness check for
`../../src/vic.c` - there's no third-party VIC-II test suite the way
`../cpu/` has Klaus Dormann's, so every expected value here was worked
out by hand against the register semantics and hardware quirks
documented in `../../src/vic.h`/`vic.c`'s own header comments, the same
way `../memory/` and `../cia/` check against their own hand-derived
expectations.

Covers: the free-running raster counter driving `$D011`/`$D012` (low
byte, MSB, and wraparound at the end of a PAL frame); DEN ($D011 bit 4)
blanking the display to the border color; text-mode rendering pulling
the right character bitmap/color-RAM nibble for a cell; and the real
hardware quirk where the character ROM is only visible to the VIC in
banks 0 and 2, never 1 or 3, when the character pointer selects offset
`$1000`/`$1800`.

Does NOT cover raster IRQs, multicolor/bitmap modes, or sprites - none
of that is implemented yet, see `vic.h`'s header comment.

```sh
make run
```
