# Makefile for c64asm, built from its per-module source files.
#
#   make			builds ./c64asm
#   make clean		removes build artifacts
#	make tests		build test prg files
#	make examples	build example prg files
#
# Works with gcc or clang; on macOS the Xcode command-line tools provide
# a `cc` that behaves the same way, so this Makefile needs no changes
# there.

# Assembler
CC      ?= cc
CFLAGS  ?= -std=c99 -O2 -Wall -Wextra

# Multi-file
SRC_DIR := src
BIN_DIR := bin
SOURCES := $(wildcard $(SRC_DIR)/*.c)
OBJECTS := $(SOURCES:.c=.o)
TARGET  := $(BIN_DIR)/c64asm

# single_src/ also holds c64asm.py (the reference, no-dependency
# implementation) and c64disasm.py -- no Makefile target needed for
# either, since Python runs directly from source.

# C64 Programs
# CASM = [ bin/c64asm "python3 single_src/c64asm.py"]
CASM    ?= $(TARGET)
EXA_DIR := examples
EXA_SRC := $(wildcard $(EXA_DIR)/*.asm)
EXA_PRG := $(EXA_SRC:.asm=.prg)
EXA_DAT := $(wildcard $(EXA_DIR)/*.dat)
LIB_DIR := lib
AFLAGS  ?=
#--warn-unused
# --warn-unused-all


DISK_DIR = disk
DISK_FILE = $(DISK_DIR)/examples.d64
VICE_C1541 = /Applications/vice-arm64-gtk3-3.10/bin/c1541


# Make
all: $(TARGET)

bin: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $(CFLAGS) -o $@ $(OBJECTS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Cleanup
clean: clean_examples
	rm -f $(TARGET) $(SRC_DIR)/*.o

clean_examples:
	rm -f $(EXA_PRG) $(EXA_DIR)/*.lst $(EXA_DIR)/*.vice $(EXA_DIR)/vice.log ./vice.log
	

# Assembly files to PRG
# 	@for file in $(EXA_PRG); do \
# 		@new_file=$${file%.txt}.md; \
# 		@echo "Converting $$file to $$new_file"; \
# 		mv "$$file" "$$new_file"; \
# 	done


# The real regression suite is examples/test_*.py: each drives its demo
# through mini6502.py end to end (win conditions, simulated keyboard and
# joystick input), rather than only checking that it assembles. They
# assemble their own .asm via single_src/c64asm.py, so this target needs
# no prior `make examples` and no built bin/c64asm.
#
# mini6502.py is a library with no CLI entry point. An earlier version of
# this target piped every built .prg into it directly, which produced no
# output, no error and a zero exit, so `make test` reported success while
# running nothing at all. The empty-glob guard below exists for the same
# reason: a suite that finds no tests must fail, not pass quietly.
test:
	@cd $(EXA_DIR) && failed=0; total=0; \
	for t in test_*.py; do \
		[ -e "$$t" ] || continue; \
		total=$$((total + 1)); \
		printf '*** %s ***\n' "$$t"; \
		python3 "$$t" || failed=$$((failed + 1)); \
	done; \
	if [ $$total -eq 0 ]; then \
		echo "FAILED: no test_*.py found in $(EXA_DIR)/ - nothing ran"; \
		exit 1; \
	fi; \
	if [ $$failed -ne 0 ]; then \
		echo "FAILED: $$failed of $$total test scripts"; \
		exit 1; \
	fi; \
	echo "PASSED: all $$total test scripts"


examples: $(TARGET) $(EXA_PRG) $(DISK_FILE) data

$(EXA_PRG):

%.prg: %.asm
	@touch $<
	@echo "$(CASM) $< -o $@"
	@$(CASM) $< -o $@ --lib-dir $(LIB_DIR) --listing $(<:.asm=.lst) --vice-labels $(<:.asm=.vice) $(AFLAGS) >> asm.log
	@touch $@


# Disk

clean_disk:
	rm $(DISK_FILE)

disk: $(DISK_FILE)

$(DISK_FILE): $(EXA_PRG)
	@echo "Creating disk $(DISK_FILE)"
	@-rm $(DISK_FILE)
	@$(VICE_C1541) -format "c64asm,1" d64  $(DISK_FILE) >> disk.log
	@for file in $(EXA_PRG); do \
		progname=$${file//_/-}; \
		progname=$${progname//.prg/}; \
		progname=$${progname//examples\//}; \
		echo "adding file: $$file as $$progname"; \
		$(VICE_C1541) $(DISK_FILE) -write "$$file" "$$progname" >> disk.log;\
	done

data:
	@echo "Adding data files $(EXA_DAT)"
	@for file in $(EXA_DAT); do \
		lower=$$(echo "$${file}" | tr 'A-Z' 'a-z'); \
		lower=$${lower//examples\//}; \
		lower=$${lower//.dat/}; \
		echo "adding file: $$file as $$lower"; \
		$(VICE_C1541) $(DISK_FILE) -write "$$file" "$$lower,s" >> disk.log; \
	done

