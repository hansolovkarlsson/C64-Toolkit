# Disassembler correctness checks

`test_disasm.c` is a hand-written, hand-verified correctness check for
`../../src/disasm.c` - there's no third-party 6502 disassembler test
suite the way `../cpu/` has Klaus Dormann's, so every expected value
here was worked out by hand against the real 6502 opcode map (and
cross-checked against `../../../asm/single_src/c64disasm.py`'s own
output for the same byte sequences, since that table is the source
`disasm.c`'s own opcode table was ported from), the same way
`../vic/` and `../sid/` check against their own hand-derived
expectations.

Covers: all 13 addressing modes (`imp`, `acc`, `imm`, `zp`, `zpx`,
`zpy`, `abs`, `absx`, `absy`, `ind`, `indx`, `indy`, `rel` - including
both a forward and a backward relative-branch offset, to confirm the
signed-offset math wraps correctly), the illegal/undocumented-opcode
fallback (`$02`/KIL decodes as `???` and still advances by exactly 1
byte, so a debugger view can keep scrolling forward through a run that
includes one), and a 3-instruction forward run (`LDA #$05` / `STA
$D020` / `RTS`) decoded by repeatedly advancing by `disasm_one()`'s own
returned length - the same pattern the GTK debugger's disassembly view
uses to render a listing.

Does NOT cover: illegal/undocumented opcodes' actual mnemonics (out of
scope for `disasm.c` - see `disasm.h`'s header comment), and flow-
following/label generation (`disasm.c` is a linear single-instruction
decoder for a live debugger view, not `c64disasm.py`'s whole-program
static disassembler - see `disasm.h` for why those are deliberately
different tools with different jobs).

```sh
make run
```
