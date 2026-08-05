# C64 Toolkit

A from-scratch Commodore 64 development toolchain: a 6502/6510
assembler and a small C compiler that targets it, both built without
leaning on any existing assembler, compiler, or toolchain — every
opcode table, addressing mode, and codegen path is hand-written and
verified against a purpose-built 6502 emulator (and, for the assembler,
real hardware and VICE too).

```c
// hello.c
#include <print.h>

int main(void) {
    puts("HELLO, C64!");
    return 0;
}
```

```sh
./C/bin/cc64 hello.c -o hello.asm
./asm/bin/c64asm hello.asm -o hello.prg
```

`hello.prg` loads and runs on a real C64 or in
[VICE](https://vice-emu.sourceforge.io/) — a BASIC `10 SYS ...` loader
stub gets you straight from `LOAD`/`RUN` to your program.

## What's here

- **[`asm/`](asm/README.md)** — `c64asm`, a two-pass 6502/6510 assembler,
  in two interchangeable, byte-identical-output implementations
  (Python, and a heavily-commented multi-file C99 split for reading).
  Includes a standard library (`lib/`), a from-scratch 6502/C64
  emulator used as the test harness (`mini6502.py`), a disassembler
  (`c64disasm.py`), and around 15 demo programs and games.
- **[`C/`](C/README.md)** — `cc64`, a small C compiler that compiles
  directly to `c64asm`-compatible assembly: pointers, structs, full
  recursion (via per-function frame save/restore, since there's no
  hardware call stack to spare), `#include` and a small standard
  library. Depends on `asm/` at build time — `cc64` only ever emits
  `.asm` text, it doesn't link against `c64asm` itself.

Each subproject's own README has the full feature list, build
instructions, and design notes — start there for anything specific to
one or the other. [`CLAUDE.md`](CLAUDE.md) has a denser, architecture-
level map of both (module layout, pipeline stages, load-bearing design
decisions) if you're working on the internals rather than just using
them.

## Building

```sh
cd asm && make               # -> asm/bin/c64asm
cd ../C && make               # -> C/bin/cc64
```

Both are portable C99 with no dependencies beyond the standard library
(`cc`/`clang`/`gcc` all work). See each subproject's README for the
full set of build targets (`make examples`, `make test`, the Python
implementations, etc.).

## Roadmap

[`ROADMAP.md`](ROADMAP.md) tracks direction for the toolkit as a
whole; [`asm/ROADMAP.md`](asm/ROADMAP.md) and
[`C/ROADMAP.md`](C/ROADMAP.md) track each subproject's own plans in
more detail.

## History

`asm/` and `C/` started as two separate repositories and were merged
into this one via `git subtree`, each keeping its full original commit
history (`git blame`/`git log <path>` on any file still reaches its
pre-merge commits).
