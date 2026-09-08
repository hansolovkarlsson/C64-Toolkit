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

Nothing has been scored here yet. This file is new, and the material that would
have gone in it up to now was written into whichever roadmap or README owned
the feature at the time. Those entries stay where they are; this is where the
next one goes.

The other records answer narrower questions: what is left
([`../ROADMAP.md`](../ROADMAP.md) and each subproject's own), what shipped in
the assembler ([`../asm/docs/CHANGELOG.md`](../asm/docs/CHANGELOG.md)), and why
a day went the way it did ([the work journal](work-journal/)).
