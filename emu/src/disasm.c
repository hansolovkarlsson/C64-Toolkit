#include "disasm.h"
#include <stdio.h>

typedef enum {
    AM_IMP, AM_ACC, AM_IMM, AM_ZP, AM_ZPX, AM_ZPY,
    AM_ABS, AM_ABSX, AM_ABSY, AM_IND, AM_INDX, AM_INDY, AM_REL
} AddrMode;

typedef struct {
    const char *mnemonic; /* NULL = illegal/undocumented opcode, not in this table */
    AddrMode mode;
} OpcodeEntry;

/* Instruction length in bytes per addressing mode - ported from
 * asm/single_src/c64asm.py's MODE_SIZE. */
static const uint8_t mode_len[] = {
    [AM_IMP] = 1, [AM_ACC] = 1, [AM_IMM] = 2, [AM_ZP] = 2, [AM_ZPX] = 2,
    [AM_ZPY] = 2, [AM_ABS] = 3, [AM_ABSX] = 3, [AM_ABSY] = 3, [AM_IND] = 3,
    [AM_INDX] = 2, [AM_INDY] = 2, [AM_REL] = 2,
};

/* Ported 1:1 from asm/single_src/c64asm.py's OPCODES table (the 56
 * legal mnemonics, 151 opcode bytes total). Where an opcode byte has
 * both an 'acc' and an 'imp' encoding for the same mnemonic (ASL/LSR/
 * ROL/ROR with no operand - $0A/$4A/$2A/$6A), only the 'acc' entry is
 * kept, matching c64disasm.py's own DECODE table resolution ("asl a"
 * reads clearer than bare "asl"). Every index not set here stays
 * {NULL, AM_IMP} - an illegal/undocumented opcode byte. */
static const OpcodeEntry opcode_table[256] = {
    [0x69] = {"ADC", AM_IMM}, [0x65] = {"ADC", AM_ZP},   [0x75] = {"ADC", AM_ZPX},
    [0x6D] = {"ADC", AM_ABS}, [0x7D] = {"ADC", AM_ABSX}, [0x79] = {"ADC", AM_ABSY},
    [0x61] = {"ADC", AM_INDX},[0x71] = {"ADC", AM_INDY},

    [0x29] = {"AND", AM_IMM}, [0x25] = {"AND", AM_ZP},   [0x35] = {"AND", AM_ZPX},
    [0x2D] = {"AND", AM_ABS}, [0x3D] = {"AND", AM_ABSX}, [0x39] = {"AND", AM_ABSY},
    [0x21] = {"AND", AM_INDX},[0x31] = {"AND", AM_INDY},

    [0x0A] = {"ASL", AM_ACC}, [0x06] = {"ASL", AM_ZP}, [0x16] = {"ASL", AM_ZPX},
    [0x0E] = {"ASL", AM_ABS}, [0x1E] = {"ASL", AM_ABSX},

    [0x90] = {"BCC", AM_REL},
    [0xB0] = {"BCS", AM_REL},
    [0xF0] = {"BEQ", AM_REL},
    [0x24] = {"BIT", AM_ZP}, [0x2C] = {"BIT", AM_ABS},
    [0x30] = {"BMI", AM_REL},
    [0xD0] = {"BNE", AM_REL},
    [0x10] = {"BPL", AM_REL},
    [0x00] = {"BRK", AM_IMP},
    [0x50] = {"BVC", AM_REL},
    [0x70] = {"BVS", AM_REL},
    [0x18] = {"CLC", AM_IMP},
    [0xD8] = {"CLD", AM_IMP},
    [0x58] = {"CLI", AM_IMP},
    [0xB8] = {"CLV", AM_IMP},

    [0xC9] = {"CMP", AM_IMM}, [0xC5] = {"CMP", AM_ZP},   [0xD5] = {"CMP", AM_ZPX},
    [0xCD] = {"CMP", AM_ABS}, [0xDD] = {"CMP", AM_ABSX}, [0xD9] = {"CMP", AM_ABSY},
    [0xC1] = {"CMP", AM_INDX},[0xD1] = {"CMP", AM_INDY},

    [0xE0] = {"CPX", AM_IMM}, [0xE4] = {"CPX", AM_ZP}, [0xEC] = {"CPX", AM_ABS},
    [0xC0] = {"CPY", AM_IMM}, [0xC4] = {"CPY", AM_ZP}, [0xCC] = {"CPY", AM_ABS},

    [0xC6] = {"DEC", AM_ZP}, [0xD6] = {"DEC", AM_ZPX}, [0xCE] = {"DEC", AM_ABS}, [0xDE] = {"DEC", AM_ABSX},
    [0xCA] = {"DEX", AM_IMP},
    [0x88] = {"DEY", AM_IMP},

    [0x49] = {"EOR", AM_IMM}, [0x45] = {"EOR", AM_ZP},   [0x55] = {"EOR", AM_ZPX},
    [0x4D] = {"EOR", AM_ABS}, [0x5D] = {"EOR", AM_ABSX}, [0x59] = {"EOR", AM_ABSY},
    [0x41] = {"EOR", AM_INDX},[0x51] = {"EOR", AM_INDY},

    [0xE6] = {"INC", AM_ZP}, [0xF6] = {"INC", AM_ZPX}, [0xEE] = {"INC", AM_ABS}, [0xFE] = {"INC", AM_ABSX},
    [0xE8] = {"INX", AM_IMP},
    [0xC8] = {"INY", AM_IMP},

    [0x4C] = {"JMP", AM_ABS}, [0x6C] = {"JMP", AM_IND},
    [0x20] = {"JSR", AM_ABS},

    [0xA9] = {"LDA", AM_IMM}, [0xA5] = {"LDA", AM_ZP},   [0xB5] = {"LDA", AM_ZPX},
    [0xAD] = {"LDA", AM_ABS}, [0xBD] = {"LDA", AM_ABSX}, [0xB9] = {"LDA", AM_ABSY},
    [0xA1] = {"LDA", AM_INDX},[0xB1] = {"LDA", AM_INDY},

    [0xA2] = {"LDX", AM_IMM}, [0xA6] = {"LDX", AM_ZP}, [0xB6] = {"LDX", AM_ZPY},
    [0xAE] = {"LDX", AM_ABS}, [0xBE] = {"LDX", AM_ABSY},

    [0xA0] = {"LDY", AM_IMM}, [0xA4] = {"LDY", AM_ZP}, [0xB4] = {"LDY", AM_ZPX},
    [0xAC] = {"LDY", AM_ABS}, [0xBC] = {"LDY", AM_ABSX},

    [0x4A] = {"LSR", AM_ACC}, [0x46] = {"LSR", AM_ZP}, [0x56] = {"LSR", AM_ZPX},
    [0x4E] = {"LSR", AM_ABS}, [0x5E] = {"LSR", AM_ABSX},

    [0xEA] = {"NOP", AM_IMP},

    [0x09] = {"ORA", AM_IMM}, [0x05] = {"ORA", AM_ZP},   [0x15] = {"ORA", AM_ZPX},
    [0x0D] = {"ORA", AM_ABS}, [0x1D] = {"ORA", AM_ABSX}, [0x19] = {"ORA", AM_ABSY},
    [0x01] = {"ORA", AM_INDX},[0x11] = {"ORA", AM_INDY},

    [0x48] = {"PHA", AM_IMP},
    [0x08] = {"PHP", AM_IMP},
    [0x68] = {"PLA", AM_IMP},
    [0x28] = {"PLP", AM_IMP},

    [0x2A] = {"ROL", AM_ACC}, [0x26] = {"ROL", AM_ZP}, [0x36] = {"ROL", AM_ZPX},
    [0x2E] = {"ROL", AM_ABS}, [0x3E] = {"ROL", AM_ABSX},

    [0x6A] = {"ROR", AM_ACC}, [0x66] = {"ROR", AM_ZP}, [0x76] = {"ROR", AM_ZPX},
    [0x6E] = {"ROR", AM_ABS}, [0x7E] = {"ROR", AM_ABSX},

    [0x40] = {"RTI", AM_IMP},
    [0x60] = {"RTS", AM_IMP},

    [0xE9] = {"SBC", AM_IMM}, [0xE5] = {"SBC", AM_ZP},   [0xF5] = {"SBC", AM_ZPX},
    [0xED] = {"SBC", AM_ABS}, [0xFD] = {"SBC", AM_ABSX}, [0xF9] = {"SBC", AM_ABSY},
    [0xE1] = {"SBC", AM_INDX},[0xF1] = {"SBC", AM_INDY},

    [0x38] = {"SEC", AM_IMP},
    [0xF8] = {"SED", AM_IMP},
    [0x78] = {"SEI", AM_IMP},

    [0x85] = {"STA", AM_ZP}, [0x95] = {"STA", AM_ZPX}, [0x8D] = {"STA", AM_ABS},
    [0x9D] = {"STA", AM_ABSX}, [0x99] = {"STA", AM_ABSY},
    [0x81] = {"STA", AM_INDX}, [0x91] = {"STA", AM_INDY},

    [0x86] = {"STX", AM_ZP}, [0x96] = {"STX", AM_ZPY}, [0x8E] = {"STX", AM_ABS},
    [0x84] = {"STY", AM_ZP}, [0x94] = {"STY", AM_ZPX}, [0x8C] = {"STY", AM_ABS},

    [0xAA] = {"TAX", AM_IMP},
    [0xA8] = {"TAY", AM_IMP},
    [0xBA] = {"TSX", AM_IMP},
    [0x8A] = {"TXA", AM_IMP},
    [0x9A] = {"TXS", AM_IMP},
    [0x98] = {"TYA", AM_IMP},
};

int disasm_one(Memory *mem, uint16_t addr, char *out, size_t out_size) {
    uint8_t op = memory_read(mem, addr);
    const OpcodeEntry *e = &opcode_table[op];

    if (e->mnemonic == NULL) {
        snprintf(out, out_size, "$%04X: %02X        ???", addr, op);
        return 1;
    }

    AddrMode mode = e->mode;
    int len = mode_len[mode];
    uint8_t b1 = (len > 1) ? memory_read(mem, (uint16_t)(addr + 1)) : 0;
    uint8_t b2 = (len > 2) ? memory_read(mem, (uint16_t)(addr + 2)) : 0;
    uint16_t val16 = (uint16_t)(b1 | (b2 << 8));

    char hex[16];
    if (len == 1) snprintf(hex, sizeof hex, "%02X", op);
    else if (len == 2) snprintf(hex, sizeof hex, "%02X %02X", op, b1);
    else snprintf(hex, sizeof hex, "%02X %02X %02X", op, b1, b2);

    char operand[16];
    operand[0] = '\0';
    switch (mode) {
        case AM_IMP:  break;
        case AM_ACC:  snprintf(operand, sizeof operand, "A"); break;
        case AM_IMM:  snprintf(operand, sizeof operand, "#$%02X", b1); break;
        case AM_ZP:   snprintf(operand, sizeof operand, "$%02X", b1); break;
        case AM_ZPX:  snprintf(operand, sizeof operand, "$%02X,X", b1); break;
        case AM_ZPY:  snprintf(operand, sizeof operand, "$%02X,Y", b1); break;
        case AM_ABS:  snprintf(operand, sizeof operand, "$%04X", val16); break;
        case AM_ABSX: snprintf(operand, sizeof operand, "$%04X,X", val16); break;
        case AM_ABSY: snprintf(operand, sizeof operand, "$%04X,Y", val16); break;
        case AM_IND:  snprintf(operand, sizeof operand, "($%04X)", val16); break;
        case AM_INDX: snprintf(operand, sizeof operand, "($%02X,X)", b1); break;
        case AM_INDY: snprintf(operand, sizeof operand, "($%02X),Y", b1); break;
        case AM_REL: {
            /* Relative to the address right after this 2-byte
             * instruction, not the opcode's own address - real 6502
             * semantics, matches c64disasm.py's identical computation. */
            int8_t offset = (int8_t)b1;
            uint16_t target = (uint16_t)(addr + 2 + offset);
            snprintf(operand, sizeof operand, "$%04X", target);
            break;
        }
    }

    if (operand[0])
        snprintf(out, out_size, "$%04X: %-8s %s %s", addr, hex, e->mnemonic, operand);
    else
        snprintf(out, out_size, "$%04X: %-8s %s", addr, hex, e->mnemonic);

    return len;
}
