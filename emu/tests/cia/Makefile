CC ?= cc
CFLAGS ?= -std=c99 -O2 -Wall -Wextra

BIN := test_cia

.PHONY: all run clean

all: $(BIN)

$(BIN): test_cia.c ../../src/cia.c ../../src/cia.h
	$(CC) $(CFLAGS) -o $(BIN) test_cia.c ../../src/cia.c

run: $(BIN)
	./$(BIN)

clean:
	rm -f $(BIN)
