CC ?= cc
CFLAGS ?= -std=c99 -O2 -Wall -Wextra

BIN := test_memory

.PHONY: all run clean

all: $(BIN)

$(BIN): test_memory.c ../../src/memory.c ../../src/memory.h ../../src/cpu.h
	$(CC) $(CFLAGS) -o $(BIN) test_memory.c ../../src/memory.c

run: $(BIN)
	./$(BIN)

clean:
	rm -f $(BIN)
