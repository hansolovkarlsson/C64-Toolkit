# ROM images

`c64emu` needs three real Commodore 64 ROM images to boot a working
BASIC/KERNAL environment. **None of them are included in this
repository**, and never will be — they're Commodore's copyrighted
binaries, not something this project can distribute. `git` is
configured to ignore everything in this directory except this file
(see `emu/.gitignore`), so dropping them here won't accidentally get
them committed.

You'll need to supply your own, legally dumped from a real Commodore
64 you own (there are well-known tools/cartridges for reading a C64's
ROM chips directly). Do not ask an AI assistant working in this
repository to find or fetch copies for you — it won't, for the same
reason this file doesn't link to any.

## Expected files

Once step 2 of `../ROADMAP.md` ("Memory map + bank switching") lands,
the loader will look for these three files in this directory, using
the same names/sizes VICE and most other C64 emulators expect:

| File          | Size      | Mapped at (when banked in)      |
|---------------|-----------|----------------------------------|
| `kernal.rom`  | 8192 bytes (`$2000`) | `$E000`-`$FFFF`      |
| `basic.rom`   | 8192 bytes (`$2000`) | `$A000`-`$BFFF`      |
| `chargen.rom` | 4096 bytes (`$1000`) | `$D000`-`$DFFF` (banked in place of I/O registers, not simultaneously addressable with them - see `../docs/memory-map.md` once it exists) |

All three are the standard PAL C64 ROM set (`kernal.901227-03.bin`,
`basic.901226-01.bin`, `characters.901225-01.bin` in most archival
naming schemes) — any PAL C64 with the "255 F49E" (or later) KERNAL
revision should produce compatible dumps. NTSC ROMs aren't targeted
yet (see `../ROADMAP.md`'s "Not yet scheduled").
