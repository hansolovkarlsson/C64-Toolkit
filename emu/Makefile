CC ?= cc
# c11, not c99 like the rest of this toolkit's C - GTK4's own headers
# rely on C11 typedef-redefinition tolerance (G_DECLARE_*_TYPE macros
# forward-declare types that get redefined later in the same header
# chain), which -std=c99 warns on for every GTK/GDK/GSK type. src/
# itself (cpu.c/memory.c) is plain C99 and unaffected either way.
CFLAGS ?= -std=c11 -O2 -Wall -Wextra
PKG_CFLAGS := $(shell pkg-config --cflags gtk4)
PKG_LIBS := $(shell pkg-config --libs gtk4)

BIN := bin/c64emu
SOURCES := gtk/main.c src/cpu.c src/memory.c

.PHONY: all clean

all: $(BIN)

$(BIN): $(SOURCES) src/cpu.h src/memory.h
	@mkdir -p bin
	$(CC) $(CFLAGS) $(PKG_CFLAGS) -o $(BIN) $(SOURCES) $(PKG_LIBS)

clean:
	rm -f $(BIN)
