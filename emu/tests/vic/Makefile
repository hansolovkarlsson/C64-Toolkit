CC ?= cc
CFLAGS ?= -std=c99 -O2 -Wall -Wextra

BIN := test_vic
SOURCES := test_vic.c ../../src/vic.c ../../src/memory.c
HEADERS := ../../src/vic.h ../../src/memory.h ../../src/cpu.h

.PHONY: all run clean

all: $(BIN)

$(BIN): $(SOURCES) $(HEADERS)
	$(CC) $(CFLAGS) -o $(BIN) $(SOURCES)

run: $(BIN)
	./$(BIN)

clean:
	rm -f $(BIN)
