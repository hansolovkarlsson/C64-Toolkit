# CPU correctness gate

`main.c` runs [Klaus Dormann's 6502 functional test
suite](https://github.com/Klaus2m5/6502_65C02_functional_tests)
against `../../src/cpu.c` - it exercises every legal opcode/
addressing-mode combination (including decimal-mode ADC/SBC's NMOS
flag quirks) and traps immediately on the first wrong result. This is
the gate step 1 of `../../ROADMAP.md` is checked against; nothing
downstream (memory/banking, VIC-II, CIA, SID) is worth building on top
of a CPU core that hasn't passed this.

## Getting the test binary

**Not vendored into this repo.** The suite is GPL-3.0-licensed, and
this project currently has no license of its own - same reasoning as
`../../roms/README.md`'s copyrighted ROM images: don't commit
third-party binaries this repo doesn't need to distribute, fetch them
on demand instead.

```sh
make fetch   # downloads 6502_functional_test.bin into this directory
make run     # builds test_cpu and runs it against that binary
```

## What "pass" looks like

```
PASS: all tests completed (26935329 cycles)
```

Every trap in the suite - including the final "all tests passed"
state - is a `JMP *` (jump to its own address), so `main.c` detects
completion by noticing the PC stopped advancing between two
`cpu_step()` calls, then checks whether that address is specifically
`$3469` (this build's success address - see "Re-deriving the success
address" below if a future suite revision ever moves it) or some
other trap (a failure, at whatever specific check caught it).

On failure:

```
FAIL: trapped at $37xx (test_case=$NN, 1234567 cycles)
```

`test_case` is the suite's own current-test counter (memory address
`$0200`) - cross-reference the `.lst` listing (`make fetch` only
grabs the `.bin`; the matching `.lst` is at the same GitHub path with
a `.lst` extension) to find which specific instruction/addressing-mode
check was running around that trap address.

## Re-deriving the success address

If a future revision of the suite changes `$3469`, find the new
success trap by downloading the matching `.lst` file and searching for
the LAST `jmp *           ;test passed, no errors` line in the
listing (there are many more `;failed anyway`-labeled traps earlier in
the file - the success one is specifically the one after the highest
`test_num` counter) and updating `SUCCESS_PC` in `main.c`.

## Load address / entry point

The `.bin` is a flat, self-contained 65536-byte memory image loaded
directly at `$0000` - no offset. Execution starts at `$0400` (not
through the reset vector - the suite treats an actual RESET occurring
mid-test as a bug and traps it, so `main.c` sets `cpu.pc` directly
instead of calling `cpu_reset()`).
