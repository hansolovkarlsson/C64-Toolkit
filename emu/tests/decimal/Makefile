CC ?= cc
CFLAGS ?= -std=c99 -O2 -Wall -Wextra

BIN := test_decimal

.PHONY: all run clean

all: $(BIN)

$(BIN): test_decimal.c ../../src/cpu.c ../../src/cpu.h
	$(CC) $(CFLAGS) -o $(BIN) test_decimal.c ../../src/cpu.c

run: $(BIN)
	./$(BIN)

clean:
	rm -f $(BIN)
