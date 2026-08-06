# CIA correctness checks

`test_cia.c` is a hand-written, hand-verified correctness check for
`../../src/cia.c`, the same way `../memory/` checks `memory.c` -
there's no third-party test suite for the 6526 CIA the way
`../cpu/` has Klaus Dormann's for the 6502, so every expected value
here was worked out by hand against the register semantics documented
in `cia.h`'s own header comment.

Covers: DDR-based port-read semantics (output-configured bits read the
output latch, input-configured bits read the external pin state);
Timer A counting/underflow/reload in continuous mode; Timer A one-shot
mode auto-stopping plus the ICR mask/pending/IRQ-line-assert/read-clears
interrupt path; Timer B cascading off Timer A's underflows (INMODE);
and the CRA/CRB FORCELOAD strobe.

Does NOT cover the C64-specific keyboard-matrix/joystick wiring onto
CIA1's ports, or CIA1/CIA2 IRQ/NMI propagation into the CPU - both are
a property of how `../../src/machine.c` wires a generic CIA into a C64
(not the chip itself), and are checked in `../machine/` instead.

```sh
make run
```
