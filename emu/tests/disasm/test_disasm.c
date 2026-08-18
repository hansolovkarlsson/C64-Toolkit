/*
 * Hand-verified checks for ../../src/disasm.c, worked out directly
 * against the ported opcode table (itself transcribed from
 * asm/single_src/c64asm.py's OPCODES) - one case per addressing mode,
 * plus the illegal-opcode fallback and a multi-instruction forward
 * run. There's no third-party 6502 disassembler test suite the way
 * tests/cpu/ has Klaus Dormann's, so every expected value here is
 * either hand-computed from the real 6502 opcode map or cross-checked
 * against asm/single_src/c64disasm.py's own output for the same bytes
 * (see this directory's README.md).
 */

#include "../../src/disasm.h"
#include "../../src/memory.h"
#include <stdio.h>
#include <string.h>

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { fprintf(stderr, "FAIL: %s\n", msg); failures++; } \
} while (0)

/* Asserts that decoding the bytes poked at addr produces exactly
 * expected_len bytes consumed and that the formatted line starts with
 * the address prefix and contains the expected mnemonic+operand text
 * somewhere after it - checking content, not exact column spacing,
 * which is a formatting detail, not the thing under test. */
static void check_one(Memory *mem, uint16_t addr, int expected_len,
                       const char *expected_text, const char *msg) {
    char out[64];
    int len = disasm_one(mem, addr, out, sizeof out);
    char prefix[8];
    snprintf(prefix, sizeof prefix, "$%04X:", addr);

    char full_msg[160];
    snprintf(full_msg, sizeof full_msg, "%s (got len=%d out=\"%s\")", msg, len, out);

    CHECK(len == expected_len, full_msg);
    CHECK(strncmp(out, prefix, strlen(prefix)) == 0, full_msg);
    CHECK(strstr(out, expected_text) != NULL, full_msg);
}

static void test_addressing_modes(void) {
    Memory mem;
    memory_init(&mem);

    memory_write(&mem, 0x0800, 0x00); /* BRK - imp */
    check_one(&mem, 0x0800, 1, "BRK", "imp: BRK");

    memory_write(&mem, 0x0810, 0x0A); /* ASL A - acc */
    check_one(&mem, 0x0810, 1, "ASL A", "acc: ASL A");

    memory_write(&mem, 0x0820, 0xA9); /* LDA #$05 - imm */
    memory_write(&mem, 0x0821, 0x05);
    check_one(&mem, 0x0820, 2, "LDA #$05", "imm: LDA #$05");

    memory_write(&mem, 0x0830, 0xA5); /* LDA $10 - zp */
    memory_write(&mem, 0x0831, 0x10);
    check_one(&mem, 0x0830, 2, "LDA $10", "zp: LDA $10");

    memory_write(&mem, 0x0840, 0xB5); /* LDA $10,X - zpx */
    memory_write(&mem, 0x0841, 0x10);
    check_one(&mem, 0x0840, 2, "LDA $10,X", "zpx: LDA $10,X");

    memory_write(&mem, 0x0850, 0xB6); /* LDX $10,Y - zpy */
    memory_write(&mem, 0x0851, 0x10);
    check_one(&mem, 0x0850, 2, "LDX $10,Y", "zpy: LDX $10,Y");

    memory_write(&mem, 0x0860, 0x4C); /* JMP $C000 - abs */
    memory_write(&mem, 0x0861, 0x00);
    memory_write(&mem, 0x0862, 0xC0);
    check_one(&mem, 0x0860, 3, "JMP $C000", "abs: JMP $C000");

    memory_write(&mem, 0x0870, 0xBD); /* LDA $1000,X - absx */
    memory_write(&mem, 0x0871, 0x00);
    memory_write(&mem, 0x0872, 0x10);
    check_one(&mem, 0x0870, 3, "LDA $1000,X", "absx: LDA $1000,X");

    memory_write(&mem, 0x0880, 0xB9); /* LDA $1000,Y - absy */
    memory_write(&mem, 0x0881, 0x00);
    memory_write(&mem, 0x0882, 0x10);
    check_one(&mem, 0x0880, 3, "LDA $1000,Y", "absy: LDA $1000,Y");

    memory_write(&mem, 0x0890, 0x6C); /* JMP ($1000) - ind */
    memory_write(&mem, 0x0891, 0x00);
    memory_write(&mem, 0x0892, 0x10);
    check_one(&mem, 0x0890, 3, "JMP ($1000)", "ind: JMP ($1000)");

    memory_write(&mem, 0x08A0, 0xA1); /* LDA ($10,X) - indx */
    memory_write(&mem, 0x08A1, 0x10);
    check_one(&mem, 0x08A0, 2, "LDA ($10,X)", "indx: LDA ($10,X)");

    memory_write(&mem, 0x08B0, 0xB1); /* LDA ($10),Y - indy */
    memory_write(&mem, 0x08B1, 0x10);
    check_one(&mem, 0x08B0, 2, "LDA ($10),Y", "indy: LDA ($10),Y");

    /* rel, forward: BEQ +5 at $08C0 -> target = $08C0 + 2 + 5 = $08C7 */
    memory_write(&mem, 0x08C0, 0xF0);
    memory_write(&mem, 0x08C1, 0x05);
    check_one(&mem, 0x08C0, 2, "BEQ $08C7", "rel: BEQ forward +5");

    /* rel, backward: BNE -2 ($FE) at $08D0 -> target = $08D0 + 2 - 2 = $08D0 (branches to itself) */
    memory_write(&mem, 0x08D0, 0xD0);
    memory_write(&mem, 0x08D1, 0xFE);
    check_one(&mem, 0x08D0, 2, "BNE $08D0", "rel: BNE backward -2 (self-branch)");
}

static void test_illegal_opcode_fallback(void) {
    Memory mem;
    memory_init(&mem);

    /* $02 (KIL) is a real NMOS opcode byte, but illegal/undocumented -
     * not in disasm.c's legal-only table, so this must fall back to
     * "???" and still advance by exactly 1 byte, the same as
     * c64disasm.py treats any opcode outside its own DECODE table. */
    memory_write(&mem, 0x0900, 0x02);
    check_one(&mem, 0x0900, 1, "???", "illegal opcode $02 falls back to ???");
}

static void test_forward_run(void) {
    /* LDA #$05 / STA $D020 / RTS - a real, complete 3-instruction
     * sequence, decoded by repeatedly advancing addr by disasm_one()'s
     * own returned length, the way the debugger's disassembly view
     * will actually use it. */
    Memory mem;
    memory_init(&mem);
    uint16_t addr = 0x1000;
    memory_write(&mem, addr + 0, 0xA9); /* LDA #$05 */
    memory_write(&mem, addr + 1, 0x05);
    memory_write(&mem, addr + 2, 0x8D); /* STA $D020 */
    memory_write(&mem, addr + 3, 0x20);
    memory_write(&mem, addr + 4, 0xD0);
    memory_write(&mem, addr + 5, 0x60); /* RTS */

    char out[64];
    uint16_t cur = addr;

    int len1 = disasm_one(&mem, cur, out, sizeof out);
    CHECK(len1 == 2, "forward run: LDA #$05 is 2 bytes");
    CHECK(strstr(out, "LDA #$05") != NULL, "forward run: first instruction decodes as LDA #$05");
    cur += (uint16_t)len1;

    int len2 = disasm_one(&mem, cur, out, sizeof out);
    CHECK(len2 == 3, "forward run: STA $D020 is 3 bytes");
    CHECK(strstr(out, "STA $D020") != NULL, "forward run: second instruction decodes as STA $D020");
    cur += (uint16_t)len2;

    int len3 = disasm_one(&mem, cur, out, sizeof out);
    CHECK(len3 == 1, "forward run: RTS is 1 byte");
    CHECK(strstr(out, "RTS") != NULL, "forward run: third instruction decodes as RTS");
    cur += (uint16_t)len3;

    CHECK(cur == addr + 6, "forward run: total bytes consumed matches the 6-byte sequence");
}

int main(void) {
    test_addressing_modes();
    test_illegal_opcode_fallback();
    test_forward_run();

    if (failures == 0) {
        printf("PASS: all disassembler checks passed\n");
        return 0;
    }
    fprintf(stderr, "FAIL: %d check(s) failed\n", failures);
    return 1;
}
