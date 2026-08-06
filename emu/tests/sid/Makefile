CC ?= cc
CFLAGS ?= -std=c99 -O2 -Wall -Wextra

BIN := test_sid

.PHONY: all run clean

all: $(BIN)

$(BIN): test_sid.c ../../src/sid.c ../../src/sid.h
	$(CC) $(CFLAGS) -o $(BIN) test_sid.c ../../src/sid.c

run: $(BIN)
	./$(BIN)

clean:
	rm -f $(BIN)
