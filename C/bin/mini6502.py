#!/usr/bin/env python3
"""
mini6502.py - thin CLI wrapper around asm/examples/mini6502.py's
C64Machine, kept here so `C/`'s own test loop (see README.md's
"Testing") can keep invoking `python3 mini6502.py program.prg
program.lst` without every call site needing to know where asm/ lives
relative to whoever's running it.

This used to be a second, independently-maintained ~350-line CPU
implementation, parallel to asm/examples/mini6502.py's - a real
divergence risk (see root ROADMAP.md's former "Two separate
mini6502.py copies" entry): it had its own separate opcode
implementation and its own hand-rolled copy of the KERNAL zero-page
poisoning simulation, with no guarantee the two ever agreed on every
opcode's exact behavior, and no way for a fix made in one to reach the
other. Now it just imports the one, actively-maintained implementation
directly, so `cc64` output is checked against the exact same CPU core
`c64asm`'s own demos are.

program.lst is still accepted (existing call sites pass it) but no
longer read - the entry point is now found the same way asm/'s own
test scripts find it: by parsing the .prg's own BASIC-stub SYS token
directly (C64Machine.find_sys_target()), not by scanning the listing
file for its first address column.
"""
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "asm", "examples"))
from mini6502 import C64Machine  # noqa: E402 (path must be set up first)


def main():
    with open(sys.argv[1], "rb") as f:
        data = f.read()

    m = C64Machine()
    target = m.find_sys_target(data)
    m.load_prg(data)
    halt_reason = m.run_until_return(target)

    sys.stdout.write("".join(m.output_text))
    sys.stdout.write("\n")
    if halt_reason is not None:
        sys.stderr.write("[halted: %s]\n" % halt_reason)
    else:
        sys.stderr.write("[ok: %d instructions executed]\n" % m.cpu.instructions_run)


if __name__ == "__main__":
    main()
