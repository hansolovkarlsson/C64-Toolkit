CC ?= cc
CFLAGS ?= -std=c99 -O2 -Wall -Wextra

BIN := test_machine
SOURCES := test_machine.c ../../src/machine.c ../../src/cia.c ../../src/vic.c ../../src/sid.c ../../src/memory.c ../../src/cpu.c
HEADERS := ../../src/machine.h ../../src/cia.h ../../src/vic.h ../../src/sid.h ../../src/memory.h ../../src/cpu.h

.PHONY: all run clean

all: $(BIN)

$(BIN): $(SOURCES) $(HEADERS)
	$(CC) $(CFLAGS) -o $(BIN) $(SOURCES)

run: $(BIN)
	./$(BIN)

clean:
	rm -f $(BIN)
