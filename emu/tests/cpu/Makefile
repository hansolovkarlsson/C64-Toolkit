CC ?= cc
CFLAGS ?= -std=c99 -O2 -Wall -Wextra

BIN := test_cpu
IRQ_BIN := test_interrupts
VECTORS := 6502_functional_test.bin

.PHONY: all run run-all clean fetch

all: $(BIN) $(IRQ_BIN)

$(BIN): main.c ../../src/cpu.c ../../src/cpu.h
	$(CC) $(CFLAGS) -o $(BIN) main.c ../../src/cpu.c

# test_interrupts.c covers cpu_reset()/BRK/IRQ/NMI/RTI - Dormann's
# suite deliberately never triggers a real interrupt mid-test (see its
# own source comments), so it can't exercise this path at all.
$(IRQ_BIN): test_interrupts.c ../../src/cpu.c ../../src/cpu.h
	$(CC) $(CFLAGS) -o $(IRQ_BIN) test_interrupts.c ../../src/cpu.c

# Klaus Dormann's 6502 functional test suite (GPL-3.0) - not vendored
# into this repo, see README.md's "Getting the test binary" for why.
# `make fetch` pulls the exact prebuilt binary this harness expects.
fetch:
	curl -sL -o $(VECTORS) \
	  https://raw.githubusercontent.com/Klaus2m5/6502_65C02_functional_tests/master/bin_files/6502_functional_test.bin

run: $(BIN)
	@if [ ! -f $(VECTORS) ]; then \
	  echo "$(VECTORS) not found - run 'make fetch' first (see README.md)"; \
	  exit 2; \
	fi
	./$(BIN) $(VECTORS)

run-all: run $(IRQ_BIN)
	./$(IRQ_BIN)

clean:
	rm -f $(BIN) $(IRQ_BIN)
