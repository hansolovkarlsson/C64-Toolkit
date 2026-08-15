# `--prg` SYS-injection return-trampoline check

`test_prg_inject.c` is a regression test for a real bug found while
running a `cc64`-compiled program through `c64emu` for the first time
(see `../../src/machine.h`'s doc comment on
`machine_push_prg_return_trampoline()` and `../../ROADMAP.md`'s
"Running `cc64`'s output" section for the full narrative).

`gtk/main.c`'s `--prg` flag jumps the CPU straight to a loaded
program's BASIC-stub `SYS` target, skipping the real `JSR` a typed
`RUN` would perform on actual hardware. Every `asm/examples/` demo
tested that way before this bug was found loops forever and never
reaches its own top-level `RTS`, so nothing ever popped whatever
happened to be sitting on the stack at injection time - until a
program that legitimately finishes and returns (the normal shape of a
`cc64` program, unlike a game) tried to `RTS` and crashed by popping
garbage.

This test reproduces that with the same minimal control case used to
bisect the original bug: a hand-assembled `.basic`-style `.prg` (built
as a raw byte array in the test itself, no `c64asm`/`cc64`/real ROMs
involved) whose "program" is nothing but an immediate `RTS`. It drives
the exact same sequence `gtk/main.c`'s `try_inject_prg()` does -
`machine_load_prg()` -> `machine_find_sys_target()` ->
`machine_push_prg_return_trampoline()` -> `cpu.pc = sys_target` - then
runs the CPU and checks:

- the `RTS` lands in the return trampoline instead of crashing or
  wandering into unrelated memory,
- the stack pointer is back to its exact pre-injection value (proving
  the push/pop is balanced, not just accidentally survivable), and
- the trampoline is a genuine stable `JMP`-to-self, not a one-shot
  landing pad.

```sh
make run
```

## What "pass" looks like

```
PASS: all prg_inject checks passed
```

On failure, this is a regression in `machine_push_prg_return_trampoline()`
or in how `gtk/main.c`'s `try_inject_prg()` calls it - not a CPU/memory/
CIA/VIC-II issue (those are covered by `../cpu/`, `../memory/`,
`../cia/`, `../machine/`, `../vic/`, `../boot/`).
