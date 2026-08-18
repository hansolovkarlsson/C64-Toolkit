CC ?= cc
CFLAGS ?= -std=c99 -O2 -Wall -Wextra

BIN := test_disasm
SOURCES := test_disasm.c ../../src/disasm.c ../../src/memory.c
HEADERS := ../../src/disasm.h ../../src/memory.h ../../src/cpu.h

.PHONY: all run clean

all: $(BIN)

$(BIN): $(SOURCES) $(HEADERS)
	$(CC) $(CFLAGS) -o $(BIN) $(SOURCES)

run: $(BIN)
	./$(BIN)

clean:
	rm -f $(BIN)
