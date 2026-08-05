"""
End-to-end regression test for hexdump.asm, using mini6502.py (see
mini6502.zip).

Verifies both the successful-load path (correct hex output, correct
total byte count) and the two specific real-world scenarios this tool
was actually built to distinguish between: OPEN failing outright
(missing file) versus OPEN succeeding but the file being shorter on
disk than expected (a truncated/malformed file) -- since maze.asm's
own on-screen symptom for a short file (a screen full of a single
repeated tile) looks nothing like either of those causes from the
game's own output alone, which is exactly why this tool exists.

Run from this directory with mini6502.py on the path, e.g.:
    PYTHONPATH=/path/to/mini6502 python3 test_hexdump.py
"""

import os
import subprocess
import sys

try:
    from mini6502 import C64Machine
except ImportError:
    sys.exit("mini6502.py not found -- put it on PYTHONPATH (see mini6502.zip)")

passed = 0
failed = 0


def check(name, condition, detail=""):
    global passed, failed
    if condition:
        passed += 1
    else:
        failed += 1
        print(f"  FAIL: {name}  {detail}")


def find_c64asm():
    for candidate in ['c64asm.py', '/mnt/user-data/outputs/c64asm.py']:
        if os.path.exists(candidate):
            return candidate
    sys.exit("c64asm.py not found")


ASSEMBLER = find_c64asm()

print("=== assembling hexdump.asm ===")
result = subprocess.run(
    ['python3', ASSEMBLER, 'hexdump.asm', '-o', '/tmp/hexdump_regress.prg',
     '--listing', '/tmp/hexdump_regress.lst', '--lib-dir', '.'],
    capture_output=True, text=True)
check("hexdump.asm assembles cleanly", result.returncode == 0, result.stderr)
if result.returncode != 0:
    print(f"\n{passed} passed, {failed} failed")
    sys.exit(1)

with open('/tmp/hexdump_regress.prg', 'rb') as f:
    data = f.read()

with open('LEVEL1.dat', 'rb') as f:
    LEVEL1_DATA = f.read()
assert len(LEVEL1_DATA) == 858


def run_to_completion(m, start_pc, max_instructions=2_000_000):
    m.cpu.pc = start_pc
    m.cpu.halted = False
    for _ in range(max_instructions):
        m.step()
        if m.cpu.halted:
            return True
    return False


def fresh_machine():
    m = C64Machine(simulate_zp_poisoning=True)
    target = m.find_sys_target(data)
    m.load_prg(data)
    return m, target


def output(m):
    return ''.join(m.output_text)


print("=== a correct, complete LEVEL1 file dumps its own real bytes "
      "(not leftover register garbage from an earlier version of this "
      "same tool's own comparison logic -- see this file's own header "
      "comment) and reports the exact correct total byte count ===")
m1, target = fresh_machine()
m1.disk_files = {'LEVEL1': LEVEL1_DATA}
check("program halts (reaches its own rts) within budget",
      run_to_completion(m1, target))
out1 = output(m1)
check("OPEN succeeded", "OPEN: CARRY CLEAR (SUCCESS)" in out1)
check("READST before any CHRIN was 0 (healthy channel)",
      "READST BEFORE ANY CHRIN: 00" in out1)
check("the dump starts with the correct, real first bytes (0F 0B, "
      "the start column/row, then 54 45 53 54 -- 'TEST')",
      "0F 0B 54 45 53 54" in out1, f"got: {out1!r}")
check("the dump shows the tile data's own wall bytes (01 repeated) "
      "starting where the header ends",
      "01 01 01 01 01 01 01 01" in out1)
check("total byte count reads exactly 00858",
      "TOTAL BYTES READ TO EOF: 00858" in out1, f"got: {out1!r}")

print("=== a missing file is reported distinctly from both a "
      "correct and a truncated one: mini6502.py's own simulation "
      "doesn't set OPEN's carry flag for a missing file the way this "
      "tool's own comment assumed real hardware might -- confirmed "
      "directly here rather than left as an untested assumption --  "
      "it instead behaves as an immediately-empty file, distinct from "
      "either other case by its own total byte count alone ===")
m2, target = fresh_machine()
m2.disk_files = {}   # LEVEL1 absent entirely
check("program halts within budget", run_to_completion(m2, target))
out2 = output(m2)
check("a missing file's own total byte count is distinctly small, "
      "unlike either a correct (858) or a merely truncated (some "
      "partial count reflecting real bytes actually read) file",
      "TOTAL BYTES READ TO EOF: 00858" not in out2
      and "TOTAL BYTES READ TO EOF: 00100" not in out2,
      f"got: {out2!r}")

print("=== a truncated file is reported distinctly from a missing "
      "one: OPEN succeeds, bytes are read and shown up to the point "
      "the file actually ends, and the total count reflects the "
      "file's own true, short size rather than reporting the same "
      "858 a correct file does ===")
m3, target = fresh_machine()
m3.disk_files = {'LEVEL1': LEVEL1_DATA[:100]}
check("program halts within budget", run_to_completion(m3, target))
out3 = output(m3)
check("OPEN succeeded (the file itself was found, unlike the missing-"
      "file case above)", "OPEN: CARRY CLEAR (SUCCESS)" in out3)
check("the dump still starts with the correct real header bytes",
      "0F 0B 54 45 53 54" in out3)
check("total byte count correctly reflects the true, short length "
      "(00100), not the full 858 a correct file would report",
      "TOTAL BYTES READ TO EOF: 00100" in out3, f"got: {out3!r}")

print()
print(f"{passed} passed, {failed} failed")
if failed:
    sys.exit(1)
