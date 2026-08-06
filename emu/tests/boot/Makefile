CC ?= cc
CFLAGS ?= -std=c99 -O2 -Wall -Wextra

BIN := test_boot
SOURCES := test_boot.c ../../src/machine.c ../../src/cia.c ../../src/vic.c ../../src/memory.c ../../src/cpu.c
HEADERS := ../../src/machine.h ../../src/cia.h ../../src/vic.h ../../src/memory.h ../../src/cpu.h

.PHONY: all run clean fetch

all: $(BIN)

$(BIN): $(SOURCES) $(HEADERS)
	$(CC) $(CFLAGS) -o $(BIN) $(SOURCES)

# MEGA65's open-roms project (github.com/MEGA65/open-roms,
# GPL-3.0/LGPL-3.0) - a from-scratch, unencumbered KERNAL/BASIC/
# character ROM replacement, explicitly NOT Commodore's copyrighted
# binaries (see ../../roms/README.md for why those specifically can
# never be fetched by an AI assistant working in this repo - that
# reasoning doesn't apply here, this project's whole point is being
# free of that encumbrance). Not vendored into this repo either way,
# same reasoning as tests/cpu/'s Klaus Dormann suite - fetched on
# demand instead of committing a third-party binary this repo doesn't
# need to distribute.
fetch:
	@mkdir -p roms
	curl -sL -o roms/kernal.rom https://raw.githubusercontent.com/MEGA65/open-roms/master/bin/kernal_generic.rom
	curl -sL -o roms/basic.rom https://raw.githubusercontent.com/MEGA65/open-roms/master/bin/basic_generic.rom
	curl -sL -o roms/chargen.rom https://raw.githubusercontent.com/MEGA65/open-roms/master/bin/chargen_openroms.rom

run: $(BIN)
	./$(BIN)

clean:
	rm -f $(BIN)
