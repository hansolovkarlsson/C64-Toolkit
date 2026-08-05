# Memory / bank-switching checks

`test_memory.c` is a hand-written, hand-verified correctness check for
`../../src/memory.c` - every expected value in it was worked out
directly against the bank-switching mode table cited in `memory.c`'s
own header comment (https://www.c64-wiki.com/wiki/Bank_Switching),
not just "does it run without crashing." Unlike `../cpu/`, there's no
existing third-party test suite for this - the C64's own memory map
isn't a generic 6502 property, it's specific to this one machine's
PLA wiring.

Covers: every bank-switching mode reachable without a cartridge
(BASIC ROM needing BOTH LORAM and HIRAM, not LORAM alone; the
LORAM=HIRAM=0 special case where $D000-$DFFF is RAM regardless of
CHAREN); "write under ROM" (a write still lands in RAM even while ROM
is what's visible for reads at that address); I/O read/write routing
through the `IoBus` callback (and NOT also silently hitting the
underlying RAM while I/O is banked in, unlike the ROM regions); the
$0000/$0001 6510 I/O port's DDR/data readback, including the
input-bit-reads-as-1 simplification; and `memory_load_roms()`'s
size-checking and per-file success/failure reporting.

```sh
make run
```
