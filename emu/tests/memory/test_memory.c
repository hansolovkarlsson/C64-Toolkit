/*
 * Hand-verified bank-switching checks - every expected value below
 * was worked out directly against the mode table cited in
 * ../../src/memory.c's header comment, the same way this toolkit's
 * other test suites (asm/, C/, tests/cpu/) check against
 * hand-calculated expectations rather than just "does it run."
 */

#include "../../src/memory.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { fprintf(stderr, "FAIL: %s\n", msg); failures++; } \
} while (0)

/* Sentinel bytes at the very start of each ROM buffer, distinct from
 * each other and from 0, so a wrong-source read is unmistakable. */
static void seed_roms(Memory *mem) {
    mem->basic_rom[0] = 0xAA;
    mem->kernal_rom[0] = 0xBB;
    mem->char_rom[0] = 0xCC;
}

static void test_bank_switching(void) {
    Memory mem;
    memory_init(&mem);
    seed_roms(&mem);

    /* Default ($37 = LORAM=HIRAM=CHAREN=1): BASIC+I/O+KERNAL, the
     * normal running configuration. */
    CHECK(memory_read(&mem, 0xA000) == 0xAA, "default config: $A000 should read BASIC ROM");
    CHECK(memory_read(&mem, 0xE000) == 0xBB, "default config: $E000 should read KERNAL ROM");
    CHECK(memory_read(&mem, 0xD000) == 0xFF, "default config: $D000 should read the unmapped-I/O placeholder");

    /* LORAM=0, HIRAM=1, CHAREN=1 ($06): BASIC banked OUT even though
     * only LORAM changed - this is the "needs BOTH bits" case. */
    mem.port_data = 0x06;
    CHECK(memory_read(&mem, 0xA000) == mem.ram[0xA000], "loram=0,hiram=1: $A000 should read RAM, not BASIC ROM");
    CHECK(memory_read(&mem, 0xE000) == 0xBB, "loram=0,hiram=1: $E000 should still read KERNAL ROM");
    CHECK(memory_read(&mem, 0xD000) == 0xFF, "loram=0,hiram=1,charen=1: $D000 should still be I/O");

    /* LORAM=1, HIRAM=0, CHAREN=1 ($05): confirms BASIC really does
     * need HIRAM too, not just LORAM. */
    mem.port_data = 0x05;
    CHECK(memory_read(&mem, 0xA000) == mem.ram[0xA000], "loram=1,hiram=0: $A000 should read RAM (HIRAM missing)");
    CHECK(memory_read(&mem, 0xE000) == mem.ram[0xE000], "loram=1,hiram=0: $E000 should read RAM");
    CHECK(memory_read(&mem, 0xD000) == 0xFF, "loram=1,hiram=0,charen=1: $D000 should still be I/O (not both LORAM/HIRAM are 0)");

    /* LORAM=0, HIRAM=0, CHAREN=1 ($04): the special case - $D000 is
     * RAM here even though CHAREN=1, because LORAM and HIRAM are BOTH
     * clear. */
    mem.port_data = 0x04;
    CHECK(memory_read(&mem, 0xD000) == mem.ram[0xD000], "loram=0,hiram=0,charen=1: $D000 should be RAM despite CHAREN=1");

    /* LORAM=1, HIRAM=1, CHAREN=0 ($03): character ROM visible. */
    mem.port_data = 0x03;
    CHECK(memory_read(&mem, 0xA000) == 0xAA, "loram=1,hiram=1,charen=0: $A000 should still read BASIC ROM");
    CHECK(memory_read(&mem, 0xE000) == 0xBB, "loram=1,hiram=1,charen=0: $E000 should still read KERNAL ROM");
    CHECK(memory_read(&mem, 0xD000) == 0xCC, "loram=1,hiram=1,charen=0: $D000 should read character ROM");

    /* All zero ($00): everything is RAM. */
    mem.port_data = 0x00;
    CHECK(memory_read(&mem, 0xA000) == mem.ram[0xA000], "all-RAM config: $A000 should be RAM");
    CHECK(memory_read(&mem, 0xD000) == mem.ram[0xD000], "all-RAM config: $D000 should be RAM");
    CHECK(memory_read(&mem, 0xE000) == mem.ram[0xE000], "all-RAM config: $E000 should be RAM");
}

static void test_write_under_rom(void) {
    Memory mem;
    memory_init(&mem); /* default: BASIC/KERNAL/I-O all visible */
    seed_roms(&mem);

    memory_write(&mem, 0xA000, 0x99);
    CHECK(memory_read(&mem, 0xA000) == 0xAA,
          "write under ROM: reading $A000 right after the write should still show BASIC ROM (unaffected)");

    mem.port_data = 0x06; /* bank BASIC out (LORAM=0) - see test_bank_switching for why HIRAM alone isn't enough to keep it in */
    CHECK(memory_read(&mem, 0xA000) == 0x99,
          "write under ROM: banking BASIC out should reveal the RAM byte the earlier write actually landed in");
}

static uint8_t io_last_write_addr_lo;
static uint8_t io_last_write_val;
static int io_write_calls;
static int io_read_calls;

static uint8_t fake_io_read(void *ctx, uint16_t addr) {
    (void)ctx; (void)addr;
    io_read_calls++;
    return 0x42;
}
static void fake_io_write(void *ctx, uint16_t addr, uint8_t v) {
    (void)ctx;
    io_write_calls++;
    io_last_write_addr_lo = (uint8_t)(addr & 0xFF);
    io_last_write_val = v;
}

static void test_io_routing(void) {
    Memory mem;
    memory_init(&mem); /* default: I/O visible at $D000-$DFFF */
    mem.io.read = fake_io_read;
    mem.io.write = fake_io_write;
    mem.io.ctx = NULL;

    io_read_calls = io_write_calls = 0;
    uint8_t v = memory_read(&mem, 0xD020);
    CHECK(io_read_calls == 1, "I/O banked in: a read at $D020 should call the registered io.read");
    CHECK(v == 0x42, "I/O banked in: the value read should come from io.read, not RAM");

    memory_write(&mem, 0xD020, 0x77);
    CHECK(io_write_calls == 1, "I/O banked in: a write at $D020 should call the registered io.write");
    CHECK(io_last_write_addr_lo == 0x20 && io_last_write_val == 0x77, "I/O banked in: io.write should see the real address/value");
    CHECK(mem.ram[0xD020] == 0, "I/O banked in: the write should NOT also land in the underlying RAM");

    /* Bank I/O out (loram=hiram=0) - now $D000-$DFFF should be plain
     * RAM, completely bypassing the io callbacks. */
    mem.port_data = 0x00;
    io_read_calls = io_write_calls = 0;
    memory_write(&mem, 0xD020, 0x55);
    CHECK(io_write_calls == 0, "I/O banked out: a write at $D020 should NOT reach io.write");
    CHECK(mem.ram[0xD020] == 0x55, "I/O banked out: the write should land in RAM instead");
    CHECK(memory_read(&mem, 0xD020) == 0x55, "I/O banked out: reading it back should show the RAM value");
    CHECK(io_read_calls == 0, "I/O banked out: the read should NOT reach io.read");
}

static void test_zp_io_port(void) {
    Memory mem;
    memory_init(&mem);

    memory_write(&mem, 0x0000, 0xFF); /* all 8 bits output */
    memory_write(&mem, 0x0001, 0x37);
    CHECK(memory_read(&mem, 0x0000) == 0xFF, "port DDR readback should match what was written");
    CHECK(memory_read(&mem, 0x0001) == 0x37, "port data readback should match what was written when every bit is an output");

    /* Bit 2 (CHAREN) configured as INPUT: should read back as 1
     * regardless of what was written to that bit in port_data - see
     * memory.c's header comment on this simplification. */
    memory_write(&mem, 0x0000, (uint8_t)(0xFF & ~0x04));
    memory_write(&mem, 0x0001, 0x00); /* tries to write CHAREN=0, but it's an input bit now */
    CHECK((memory_read(&mem, 0x0001) & 0x04) != 0, "an input-configured port bit should read back as 1, not its written value");
}

static void test_load_roms(void) {
    /* A plain system("mkdir -p ...") rather than mkdtemp() - the
     * latter is POSIX but gated behind platform-specific visibility
     * macros (_DARWIN_C_SOURCE on macOS, differently elsewhere), and a
     * fixed scratch path is fine for a one-shot test fixture like
     * this (no concurrent test runs need to avoid colliding here). */
    const char *dir = "/tmp/c64emu_memtest";
    if (system("mkdir -p /tmp/c64emu_memtest") != 0) {
        fprintf(stderr, "FAIL: could not create scratch dir for ROM-loading test\n");
        failures++;
        return;
    }

    char path[1200];
    snprintf(path, sizeof(path), "%s/kernal.rom", dir);
    FILE *f = fopen(path, "wb");
    uint8_t kbuf[8192]; memset(kbuf, 0x11, sizeof(kbuf));
    fwrite(kbuf, 1, sizeof(kbuf), f);
    fclose(f);

    snprintf(path, sizeof(path), "%s/basic.rom", dir);
    f = fopen(path, "wb");
    uint8_t wrong_size[100]; memset(wrong_size, 0x22, sizeof(wrong_size));
    fwrite(wrong_size, 1, sizeof(wrong_size), f); /* deliberately the wrong size */
    fclose(f);
    /* chargen.rom deliberately not created at all */

    Memory mem;
    memory_init(&mem);
    int n = memory_load_roms(&mem, dir);

    CHECK(n == 1, "memory_load_roms: exactly one of the three files should have loaded successfully");
    CHECK(mem.has_kernal == 1, "memory_load_roms: kernal.rom (correct size) should report loaded");
    CHECK(mem.kernal_rom[0] == 0x11 && mem.kernal_rom[8191] == 0x11, "memory_load_roms: kernal_rom's contents should match the file");
    CHECK(mem.has_basic == 0, "memory_load_roms: basic.rom (wrong size) should report NOT loaded");
    CHECK(mem.has_char == 0, "memory_load_roms: a missing chargen.rom should report NOT loaded");
}

int main(void) {
    test_bank_switching();
    test_write_under_rom();
    test_io_routing();
    test_zp_io_port();
    test_load_roms();

    if (failures == 0) {
        printf("PASS: all memory/bank-switching checks passed\n");
        return 0;
    }
    fprintf(stderr, "FAIL: %d check(s) failed\n", failures);
    return 1;
}
