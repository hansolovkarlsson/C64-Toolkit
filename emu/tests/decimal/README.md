# Decimal-mode ADC/SBC gate

Exhaustive verification of `src/cpu.c`'s BCD arithmetic: all 256
accumulator values against all 256 operand values against both carry-in
values, for `ADC` and `SBC`, in immediate and zero-page addressing.
**524,288 cases**, checking the accumulator and all four affected flags
(N, V, Z, C).

## Why this exists separately from `../cpu/`

Klaus Dormann's suite runs every legal opcode and addressing mode, but
its own source stops short in exactly one place:

```
; decimal add/subtract test
; *** WARNING - tests documented behavior only! ***
;   only valid BCD operands are tested, N V Z flags are ignored
```

and its checking code confirms it, comparing the result byte and then
`and #1  ;mask carry`. So before this gate existed, decimal mode was
verified for the accumulator and C only, on valid BCD operands only.

That left the most intricate logic in `cpu.c` untested: `op_adc()` takes
N and V from an **uncorrected** intermediate rather than the final
BCD-adjusted accumulator, takes Z from a plain binary addition ignoring
decimal mode entirely, and `op_sbc()` takes N, V, Z and C all from the
binary subtraction even with D set. Those are the real NMOS 6502 quirks,
they are what `cpu.c`'s header comment describes at length, and none of
them was checked by anything.

## Where the expected values come from

`test_decimal.c`'s predictor is a port of Bruce Clark's
`6502_decimal_test.a65` (public domain, see
<http://www.6502.org/tutorials/decimal_mode.html>, distributed in the
root of the same Klaus2m5 repository `../cpu/` fetches from) with its
`cputype = 0` flag semantics.

Two things about that original are worth knowing before reaching for it
directly. It ships with `chk_n`, `chk_v` and `chk_z` all set to `0`, so
building it unmodified checks the accumulator and carry and would not
have closed this gap either. And no prebuilt binary of it is published
in that repo's `bin_files/`, only the AS65 source, so running it at all
means assembling it first.

## Why the predictor is trusted

A predictor written from the same understanding that produced `cpu.c`
would agree with a bug in it, so this port was checked against an
independent source before being relied on: SingleStepTests/
ProcessorTests' `6502/v1/69.json` and `e9.json`, per-opcode fixtures
with full before-and-after CPU state. Of their 10,000 cases each, 4,962
(ADC) and 4,921 (SBC) enter with D set, 3,051 of the ADC ones with an
invalid BCD nibble. **The predictor matched all 9,883 with zero
mismatches.**

Those fixtures are ~3.3MB each and are deliberately **not** vendored or
fetched here. They were a one-off validation of the predictor, recorded
so the claim can be re-checked rather than taken on trust. The gate
itself needs no download and runs in well under a second.

## The gate was checked against deliberate bugs

A test that has never failed has not been shown to work. Four mutations
were applied to a scratch copy of `cpu.c`, each caught:

| Mutation | Cases failed |
|---|---|
| ADC's N from the corrected value, not the uncorrected intermediate | 167,760 |
| ADC's V from the corrected value (the textbook binary formula) | 69,456 |
| ADC's Z from the decimal result, not the binary sum | 2,078 |
| SBC's N from the decimal result, not the binary one | 131,072 |
| ADC's low-nibble correction dropped entirely | 210,944 |

## Running it

```sh
cd emu/tests/decimal
make run
```

## What "pass" looks like

```
PASS: all decimal-mode checks passed (524288 cases)
```

A failure names the operands and prints expected against actual for the
accumulator and all four flags, up to the first ten, then a total.
