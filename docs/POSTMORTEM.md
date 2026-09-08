# Postmortem

*The **scoring**. One entry per mistake or prediction that has met evidence:
one that held, one that failed, or one that held and then failed, under
**Issue**, **Root cause**, **Solution**, **Learnings**. Not a bug log: a defect
belongs here when what it taught outlives it, and a day that scored nothing
adds nothing. The learning is the part that has to be true a year from now, so
it says what would have caught the thing rather than resolving to be more
careful.*

Scoped to the whole toolkit, not one subproject, since the mistakes worth
keeping here have tended to cross the boundary: a `cc64` program was what first
ran through `c64emu`'s `--prg` path, and a real C64 was what caught the
zero-page assumption that `mini6502.py` had been happy with.

This file is new as of 2026-09-08, and the material that would have gone in it
before then was written into whichever roadmap or README owned the feature at
the time. Those entries stay where they are; entries from that day on go here.

The other records answer narrower questions: what is left
([`../ROADMAP.md`](../ROADMAP.md) and each subproject's own), what shipped in
the assembler ([`../asm/docs/CHANGELOG.md`](../asm/docs/CHANGELOG.md)), and why
a day went the way it did ([the work journal](work-journal/)).

## 2026-09-08 A silent no-op was fixed once and its twin was never looked for
**Issue**: The audit found that `asm/`'s `make test` runs no tests. It pipes each of the 16 demo `.prg` files into `python3 examples/mini6502.py`, which has no command-line entry point, so every run produces no output, no error and a zero exit. The same trap had already been found and fixed in root `CLAUDE.md`'s documented `C/` test command, and that fix is recorded in the root roadmap's "Recently done" as a bug caught by accident. The `asm/Makefile` copy survived that fix by weeks.
**Root cause**: The first fix treated the defect as one wrong command rather than as a shape, a runner that reports success without checking anything, and nothing prompted a search for other places the shape occurred. The audit's other three findings about the test paths have the same shape: a per-demo suite that cannot find the assembler from where the documentation runs it, a compiler test loop that exits zero on a `BRK` halt and compares nothing, and a build script that looks for a binary where it has never been built.
**Solution**: All four instances are recorded together on the root roadmap, under one entry, so that whoever fixes one sees the others. None is fixed yet.
**Learnings**: A silent success is the hardest defect to notice, because the thing that would report it is the thing that is broken. When one instance is found, that is the moment to ask what else has the same shape, and a grep for the same command or the same script is a minute's work. The check that would have caught all four is to make each documented test command fail on purpose once, by breaking the thing it claims to test, and see whether it notices.

## 2026-09-08 A count of seven written without checking each of the seven
**Issue**: The audit's first commit said `emu/ROADMAP.md` still opens with "Nothing here is implemented yet" although all seven of its build-order steps are done. Six are. The seventh, SID, reads "in progress", with the chip core, the address-map wiring and the SDL2 audio output done and the analog filter not started.
**Root cause**: The count was a claim about seven things made from the impression the page gives, six struck-through headings and a seventh that looks finished from a distance, rather than from reading each of the seven. The entry's argument was unaffected, which is exactly what made the number easy to write without checking: it was supporting detail, not the point.
**Solution**: Corrected the same morning, in `078a566`. It surfaced only because a second sweep re-ran every number the first one had committed. The other five numbers in the same entry held exactly.
**Learnings**: A number in a record is a claim, and the ones that are not the point get the least checking and are the ones that go wrong. The check that caught this is cheap and general: before committing an entry with numbers in it, re-derive each number from the tree rather than from the sentence that contains it. Futamura's postmortem of 2026-09-07 records the same lesson from the other side, where forty observed counts were right and three derived ones were wrong.

## 2026-09-08 A third-party suite's coverage was taken on trust
**Issue**: `emu/`'s CPU core has been gated by Klaus Dormann's functional test since it was written, and root `CLAUDE.md` describes that gate as covering "every legal opcode/addressing-mode combination". It does not cover decimal-mode flags. The suite's own source says so in a comment above the section: it tests documented behaviour only, uses valid BCD operands only, and ignores N, V and Z, and its checking code masks off everything but carry. Decimal `ADC` and `SBC` in `cpu.c` take N and V from an uncorrected intermediate and Z from a plain binary addition, which is the most intricate logic in the file, and not one of those derivations had ever been checked by anything.
**Root cause**: The suite passes, it is exhaustive over opcodes and addressing modes, and it is the standard the hobby uses, so its result was read as "the CPU is correct" rather than as "the CPU is correct on what this suite checks". Only its pass line had ever been read, never its source. A third-party gate is the easiest place for this to happen, because the reason to adopt one is precisely that somebody else already did the thinking.
**Solution**: `emu/tests/decimal/` (`8df41de`), an exhaustive sweep of both instructions across every accumulator, operand and carry combination in two addressing modes, checking the accumulator and all four flags: 524288 cases. `cpu.c` passed every one of them unchanged, so the gap was in the testing and not in the emulator. The predictor was validated against an independent fixture set before being relied on, and the gate was then checked by breaking `cpu.c` on purpose five ways and confirming it noticed each.
**Learnings**: A passing third-party suite tells you what it checks, not what you assumed it checks, and the distance between those two is invisible until somebody reads its source. The check is cheap and belongs at adoption time rather than years later: read the suite's own statement of its limits once, and write those limits down beside the gate that runs it. The warning here was a comment sitting in this repository the whole time. Two further gaps found the same way are now recorded rather than left to be rediscovered: no per-instruction cycle count is verified anywhere, and `mini6502.py`, which gates all sixteen demos and all sixteen compiler tests, has twenty-five assertions behind it.
