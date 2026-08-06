# Machine-level (CIA <-> C64) wiring checks

`test_machine.c` checks `../../src/machine.c` specifically - the
C64-specific wiring on top of the generic CIA chip that `../cia/`
already covers on its own. Every expected value here was worked out by
hand against the standard published C64 keyboard matrix and CIA1/CIA2
wiring (see `../../src/machine.h`'s header comment), the same way
`../memory/` and `../cia/` check against their own hand-derived
expectations - there's no third-party reference test suite for any of
this.

Covers: keyboard-matrix column->row scanning (and the reverse
direction, row->column, since real software occasionally scans that
way too); joystick 2 sharing CIA1 PRA's pins with keyboard column
selection (a real, well-known hardware quirk); CIA1's interrupt output
reaching `cpu.irq_line`; and CIA2's interrupt output edge-triggering
`cpu_nmi()`.

Does NOT re-check the CIA timer/ICR/DDR semantics themselves - see
`../cia/` for that.

```sh
make run
```
