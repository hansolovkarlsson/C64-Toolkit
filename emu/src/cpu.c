/*
 * cpu.c - a cycle-stepped NMOS 6502/6510 core: full legal instruction
 * set (the 6510 is functionally a 6502 with an extra 6-bit I/O port
 * bolted onto zero-page addresses $00/$01 - see src/memory.c, not
 * implemented yet - so this file is indistinguishable from a plain
 * 6502 core). No illegal/undocumented opcodes - see the `default:`
 * case in cpu_step() below for what happens if one is ever fetched.
 *
 * Verified against Klaus Dormann's 6502 functional test suite (see
 * tests/cpu/README.md) - not vendored into this repo (GPL-3.0,
 * unrelated to this project's own licensing), fetched on demand the
 * same way roms/README.md handles copyrighted ROM images.
 *
 * Decimal-mode ADC/SBC follow the NMOS quirks documented at
 * https://6502.org/tutorials/decimal_mode.html (Bruce Clark) rather
 * than the "obviously correct" BCD-everywhere behavior the 65C02
 * actually has - on real NMOS silicon (and so on a real C64's 6510),
 * the N/V/Z flags after a decimal ADC/SBC are derived from different,
 * partially-uncorrected intermediate values than the accumulator
 * result itself. See op_adc()/op_sbc() below for exactly which.
 */

#include "cpu.h"

static uint8_t rd(Cpu6502 *cpu, uint16_t addr) { return cpu->bus.read(cpu->bus.ctx, addr); }
static void wr(Cpu6502 *cpu, uint16_t addr, uint8_t v) { cpu->bus.write(cpu->bus.ctx, addr, v); }

/* Plain 16-bit read (absolute operands, interrupt/reset vectors) - the
 * high byte comes from addr+1 with normal 16-bit wraparound. */
static uint16_t rd16(Cpu6502 *cpu, uint16_t addr) {
    uint8_t lo = rd(cpu, addr);
    uint8_t hi = rd(cpu, (uint16_t)(addr + 1));
    return (uint16_t)(lo | (hi << 8));
}

/* Zero-page-indirect read ((zp,X) and (zp),Y both go through this) -
 * the high byte wraps WITHIN the zero page (reads $FF then $00, never
 * spilling into page 1), a real 6502 quirk since the increment only
 * ever touches the low byte of the pointer address itself. */
static uint16_t rd16_zp(Cpu6502 *cpu, uint8_t addr) {
    uint8_t lo = rd(cpu, addr);
    uint8_t hi = rd(cpu, (uint8_t)(addr + 1));
    return (uint16_t)(lo | (hi << 8));
}

static void push8(Cpu6502 *cpu, uint8_t v) {
    wr(cpu, (uint16_t)(0x0100 + cpu->sp), v);
    cpu->sp--;
}
static uint8_t pop8(Cpu6502 *cpu) {
    cpu->sp++;
    return rd(cpu, (uint16_t)(0x0100 + cpu->sp));
}
static void push16(Cpu6502 *cpu, uint16_t v) {
    push8(cpu, (uint8_t)(v >> 8));
    push8(cpu, (uint8_t)(v & 0xFF));
}
static uint16_t pop16(Cpu6502 *cpu) {
    uint8_t lo = pop8(cpu);
    uint8_t hi = pop8(cpu);
    return (uint16_t)(lo | (hi << 8));
}

static void set_flag(Cpu6502 *cpu, uint8_t flag, int cond) {
    if (cond) cpu->p |= flag; else cpu->p = (uint8_t)(cpu->p & ~flag);
}
static void set_nz(Cpu6502 *cpu, uint8_t v) {
    set_flag(cpu, FLAG_Z, v == 0);
    set_flag(cpu, FLAG_N, (v & 0x80) != 0);
}

/* ===================================================================
 * Addressing modes - each fetches its own operand byte(s) from
 * cpu->pc (advancing it), and returns the EFFECTIVE ADDRESS. The
 * indexed-absolute and (zp),Y forms also report whether
 * adding the index crossed a page boundary, via `crossed` - read
 * instructions in these modes take one extra cycle when it did; write
 * and read-modify-write instructions always take the extra cycle
 * unconditionally (a real dummy read happens either way), so their
 * call sites just ignore `crossed` and add the cycle themselves.
 * =================================================================== */

static uint16_t am_zp(Cpu6502 *cpu) { return rd(cpu, cpu->pc++); }
static uint16_t am_zpx(Cpu6502 *cpu) { uint8_t zp = rd(cpu, cpu->pc++); return (uint8_t)(zp + cpu->x); }
static uint16_t am_zpy(Cpu6502 *cpu) { uint8_t zp = rd(cpu, cpu->pc++); return (uint8_t)(zp + cpu->y); }
static uint16_t am_abs(Cpu6502 *cpu) { uint16_t a = rd16(cpu, cpu->pc); cpu->pc = (uint16_t)(cpu->pc + 2); return a; }
static uint16_t am_absx(Cpu6502 *cpu, int *crossed) {
    uint16_t base = rd16(cpu, cpu->pc); cpu->pc = (uint16_t)(cpu->pc + 2);
    uint16_t addr = (uint16_t)(base + cpu->x);
    *crossed = (base & 0xFF00) != (addr & 0xFF00);
    return addr;
}
static uint16_t am_absy(Cpu6502 *cpu, int *crossed) {
    uint16_t base = rd16(cpu, cpu->pc); cpu->pc = (uint16_t)(cpu->pc + 2);
    uint16_t addr = (uint16_t)(base + cpu->y);
    *crossed = (base & 0xFF00) != (addr & 0xFF00);
    return addr;
}
static uint16_t am_indx(Cpu6502 *cpu) {
    uint8_t zp = rd(cpu, cpu->pc++);
    return rd16_zp(cpu, (uint8_t)(zp + cpu->x));
}
static uint16_t am_indy(Cpu6502 *cpu, int *crossed) {
    uint8_t zp = rd(cpu, cpu->pc++);
    uint16_t base = rd16_zp(cpu, zp);
    uint16_t addr = (uint16_t)(base + cpu->y);
    *crossed = (base & 0xFF00) != (addr & 0xFF00);
    return addr;
}

/* ===================================================================
 * ADC/SBC - see this file's header comment for why decimal mode is
 * its own careful thing, not just "do BCD addition."
 * =================================================================== */

static void op_adc(Cpu6502 *cpu, uint8_t b) {
    int c = (cpu->p & FLAG_C) ? 1 : 0;
    if (cpu->p & FLAG_D) {
        int al = (cpu->a & 0x0F) + (b & 0x0F) + c;
        if (al >= 0x0A) al = ((al + 0x06) & 0x0F) + 0x10;
        int presum = (cpu->a & 0xF0) + (b & 0xF0) + al; /* uncorrected - N/V come from THIS, not the final BCD-corrected value */
        int hi_a = (int8_t)(cpu->a & 0xF0);
        int hi_b = (int8_t)(b & 0xF0);
        int signed_presum = hi_a + hi_b + al;
        int corrected = presum;
        if (corrected >= 0xA0) corrected += 0x60;
        uint8_t bin_sum = (uint8_t)(cpu->a + b + c); /* Z is "bin": plain binary addition, ignoring decimal mode entirely */
        set_flag(cpu, FLAG_N, (presum & 0x80) != 0);
        set_flag(cpu, FLAG_V, signed_presum < -128 || signed_presum > 127);
        set_flag(cpu, FLAG_Z, bin_sum == 0);
        set_flag(cpu, FLAG_C, corrected >= 0x100);
        cpu->a = (uint8_t)(corrected & 0xFF);
    } else {
        int sum = cpu->a + b + c;
        uint8_t result = (uint8_t)sum;
        set_flag(cpu, FLAG_C, sum > 0xFF);
        set_flag(cpu, FLAG_V, (~(cpu->a ^ b) & (cpu->a ^ result) & 0x80) != 0);
        set_nz(cpu, result);
        cpu->a = result;
    }
}

static void op_sbc(Cpu6502 *cpu, uint8_t b) {
    int c = (cpu->p & FLAG_C) ? 1 : 0;
    /* C/N/V/Z are always "bin" - a plain binary subtraction - even in
     * decimal mode (this is the documented NMOS quirk: only the
     * accumulator's stored result gets BCD-corrected). Implemented via
     * the standard SBC-as-inverted-operand-ADC identity, which IS
     * exactly binary subtraction: A + ~B + C = A - B + C - 1 (mod 256,
     * plus a wraparound constant that cancels out of the carry test). */
    int bin_result = cpu->a + (uint8_t)(~b) + c;
    uint8_t bin8 = (uint8_t)bin_result;
    set_flag(cpu, FLAG_C, bin_result > 0xFF);
    set_flag(cpu, FLAG_V, ((cpu->a ^ b) & (cpu->a ^ bin8) & 0x80) != 0);
    set_flag(cpu, FLAG_Z, bin8 == 0);
    set_flag(cpu, FLAG_N, (bin8 & 0x80) != 0);

    if (cpu->p & FLAG_D) {
        int al = (cpu->a & 0x0F) - (b & 0x0F) + c - 1;
        if (al < 0) al = ((al - 0x06) & 0x0F) - 0x10;
        int a2 = (cpu->a & 0xF0) - (b & 0xF0) + al;
        if (a2 < 0) a2 -= 0x60;
        cpu->a = (uint8_t)(a2 & 0xFF);
    } else {
        cpu->a = bin8;
    }
}

/* Shared by CMP/CPX/CPY - a subtraction whose only effect is flags
 * (C/N/Z; V is untouched, unlike SBC). C is set if reg >= operand
 * (i.e. no borrow), matching real 6502 semantics. */
static void op_compare(Cpu6502 *cpu, uint8_t reg, uint8_t operand) {
    uint16_t diff = (uint16_t)(reg - operand);
    set_flag(cpu, FLAG_C, reg >= operand);
    set_nz(cpu, (uint8_t)diff);
}

static uint8_t op_asl(Cpu6502 *cpu, uint8_t v) {
    set_flag(cpu, FLAG_C, (v & 0x80) != 0);
    v = (uint8_t)(v << 1);
    set_nz(cpu, v);
    return v;
}
static uint8_t op_lsr(Cpu6502 *cpu, uint8_t v) {
    set_flag(cpu, FLAG_C, (v & 0x01) != 0);
    v = (uint8_t)(v >> 1);
    set_nz(cpu, v);
    return v;
}
static uint8_t op_rol(Cpu6502 *cpu, uint8_t v) {
    int c = (cpu->p & FLAG_C) ? 1 : 0;
    set_flag(cpu, FLAG_C, (v & 0x80) != 0);
    v = (uint8_t)((v << 1) | c);
    set_nz(cpu, v);
    return v;
}
static uint8_t op_ror(Cpu6502 *cpu, uint8_t v) {
    int c = (cpu->p & FLAG_C) ? 1 : 0;
    set_flag(cpu, FLAG_C, (v & 0x01) != 0);
    v = (uint8_t)((v >> 1) | (c << 7));
    set_nz(cpu, v);
    return v;
}

/* Relative branch, shared by all 8 conditional branches - PC already
 * points past the 1-byte offset by the time this is called. Adds 1
 * cycle if the branch is taken, plus 1 more if taking it also crosses
 * a page boundary (checked on the branch's DESTINATION page, not the
 * opcode's own page). */
static int do_branch(Cpu6502 *cpu, int taken) {
    int8_t offset = (int8_t)rd(cpu, cpu->pc++);
    if (!taken) return 0;
    uint16_t old_pc = cpu->pc;
    cpu->pc = (uint16_t)(cpu->pc + offset);
    return ((old_pc & 0xFF00) != (cpu->pc & 0xFF00)) ? 2 : 1;
}

/* ===================================================================
 * Interrupt/reset sequences - BRK, NMI, and IRQ all push PC then P
 * then jump through a vector, the same 7-cycle shape; they differ
 * only in the pushed PC's value, whether B is set in the pushed P,
 * and which vector. See cpu.h's cpu_reset()/cpu_nmi() comments for
 * why reset itself is NOT just "call this with pc=$FFFC vector".
 * =================================================================== */

static void do_interrupt(Cpu6502 *cpu, uint16_t vector, int is_brk) {
    push16(cpu, cpu->pc);
    uint8_t pushed_p = (uint8_t)(cpu->p | FLAG_U);
    if (is_brk) pushed_p |= FLAG_B; else pushed_p = (uint8_t)(pushed_p & ~FLAG_B);
    push8(cpu, pushed_p);
    set_flag(cpu, FLAG_I, 1);
    cpu->pc = rd16(cpu, vector);
}

void cpu_reset(Cpu6502 *cpu) {
    cpu->sp = (uint8_t)(cpu->sp - 3);
    set_flag(cpu, FLAG_I, 1);
    cpu->p |= FLAG_U;
    cpu->pc = rd16(cpu, 0xFFFC);
    cpu->nmi_pending = 0;
}

void cpu_nmi(Cpu6502 *cpu) { cpu->nmi_pending = 1; }

int cpu_step(Cpu6502 *cpu) {
    int cycles;

    if (cpu->nmi_pending) {
        cpu->nmi_pending = 0;
        do_interrupt(cpu, 0xFFFA, 0);
        cycles = 7;
        cpu->cycles += (uint64_t)cycles;
        return cycles;
    }
    if (cpu->irq_line && !(cpu->p & FLAG_I)) {
        do_interrupt(cpu, 0xFFFE, 0);
        cycles = 7;
        cpu->cycles += (uint64_t)cycles;
        return cycles;
    }

    uint8_t op = rd(cpu, cpu->pc++);
    int crossed = 0;
    uint16_t addr;
    uint8_t v;

    switch (op) {
        /* --- ADC --- */
        case 0x69: v = rd(cpu, cpu->pc++); op_adc(cpu, v); cycles = 2; break;
        case 0x65: addr = am_zp(cpu); op_adc(cpu, rd(cpu, addr)); cycles = 3; break;
        case 0x75: addr = am_zpx(cpu); op_adc(cpu, rd(cpu, addr)); cycles = 4; break;
        case 0x6D: addr = am_abs(cpu); op_adc(cpu, rd(cpu, addr)); cycles = 4; break;
        case 0x7D: addr = am_absx(cpu, &crossed); op_adc(cpu, rd(cpu, addr)); cycles = 4 + crossed; break;
        case 0x79: addr = am_absy(cpu, &crossed); op_adc(cpu, rd(cpu, addr)); cycles = 4 + crossed; break;
        case 0x61: addr = am_indx(cpu); op_adc(cpu, rd(cpu, addr)); cycles = 6; break;
        case 0x71: addr = am_indy(cpu, &crossed); op_adc(cpu, rd(cpu, addr)); cycles = 5 + crossed; break;

        /* --- AND --- */
        case 0x29: v = rd(cpu, cpu->pc++); cpu->a &= v; set_nz(cpu, cpu->a); cycles = 2; break;
        case 0x25: addr = am_zp(cpu); cpu->a &= rd(cpu, addr); set_nz(cpu, cpu->a); cycles = 3; break;
        case 0x35: addr = am_zpx(cpu); cpu->a &= rd(cpu, addr); set_nz(cpu, cpu->a); cycles = 4; break;
        case 0x2D: addr = am_abs(cpu); cpu->a &= rd(cpu, addr); set_nz(cpu, cpu->a); cycles = 4; break;
        case 0x3D: addr = am_absx(cpu, &crossed); cpu->a &= rd(cpu, addr); set_nz(cpu, cpu->a); cycles = 4 + crossed; break;
        case 0x39: addr = am_absy(cpu, &crossed); cpu->a &= rd(cpu, addr); set_nz(cpu, cpu->a); cycles = 4 + crossed; break;
        case 0x21: addr = am_indx(cpu); cpu->a &= rd(cpu, addr); set_nz(cpu, cpu->a); cycles = 6; break;
        case 0x31: addr = am_indy(cpu, &crossed); cpu->a &= rd(cpu, addr); set_nz(cpu, cpu->a); cycles = 5 + crossed; break;

        /* --- ASL --- */
        case 0x0A: cpu->a = op_asl(cpu, cpu->a); cycles = 2; break;
        case 0x06: addr = am_zp(cpu); wr(cpu, addr, op_asl(cpu, rd(cpu, addr))); cycles = 5; break;
        case 0x16: addr = am_zpx(cpu); wr(cpu, addr, op_asl(cpu, rd(cpu, addr))); cycles = 6; break;
        case 0x0E: addr = am_abs(cpu); wr(cpu, addr, op_asl(cpu, rd(cpu, addr))); cycles = 6; break;
        case 0x1E: addr = am_absx(cpu, &crossed); wr(cpu, addr, op_asl(cpu, rd(cpu, addr))); cycles = 7; break;

        /* --- branches --- */
        case 0x90: cycles = 2 + do_branch(cpu, !(cpu->p & FLAG_C)); break;
        case 0xB0: cycles = 2 + do_branch(cpu, (cpu->p & FLAG_C) != 0); break;
        case 0xF0: cycles = 2 + do_branch(cpu, (cpu->p & FLAG_Z) != 0); break;
        case 0x30: cycles = 2 + do_branch(cpu, (cpu->p & FLAG_N) != 0); break;
        case 0xD0: cycles = 2 + do_branch(cpu, !(cpu->p & FLAG_Z)); break;
        case 0x10: cycles = 2 + do_branch(cpu, !(cpu->p & FLAG_N)); break;
        case 0x50: cycles = 2 + do_branch(cpu, !(cpu->p & FLAG_V)); break;
        case 0x70: cycles = 2 + do_branch(cpu, (cpu->p & FLAG_V) != 0); break;

        /* --- BIT --- */
        case 0x24: addr = am_zp(cpu); v = rd(cpu, addr);
            set_flag(cpu, FLAG_Z, (cpu->a & v) == 0);
            set_flag(cpu, FLAG_N, (v & 0x80) != 0);
            set_flag(cpu, FLAG_V, (v & 0x40) != 0);
            cycles = 3; break;
        case 0x2C: addr = am_abs(cpu); v = rd(cpu, addr);
            set_flag(cpu, FLAG_Z, (cpu->a & v) == 0);
            set_flag(cpu, FLAG_N, (v & 0x80) != 0);
            set_flag(cpu, FLAG_V, (v & 0x40) != 0);
            cycles = 4; break;

        /* --- BRK --- */
        case 0x00:
            cpu->pc = (uint16_t)(cpu->pc + 1); /* skip the padding/signature byte */
            do_interrupt(cpu, 0xFFFE, 1);
            cycles = 7; break;

        /* --- flag clear/set --- */
        case 0x18: set_flag(cpu, FLAG_C, 0); cycles = 2; break;
        case 0xD8: set_flag(cpu, FLAG_D, 0); cycles = 2; break;
        case 0x58: set_flag(cpu, FLAG_I, 0); cycles = 2; break;
        case 0xB8: set_flag(cpu, FLAG_V, 0); cycles = 2; break;
        case 0x38: set_flag(cpu, FLAG_C, 1); cycles = 2; break;
        case 0xF8: set_flag(cpu, FLAG_D, 1); cycles = 2; break;
        case 0x78: set_flag(cpu, FLAG_I, 1); cycles = 2; break;

        /* --- CMP --- */
        case 0xC9: v = rd(cpu, cpu->pc++); op_compare(cpu, cpu->a, v); cycles = 2; break;
        case 0xC5: addr = am_zp(cpu); op_compare(cpu, cpu->a, rd(cpu, addr)); cycles = 3; break;
        case 0xD5: addr = am_zpx(cpu); op_compare(cpu, cpu->a, rd(cpu, addr)); cycles = 4; break;
        case 0xCD: addr = am_abs(cpu); op_compare(cpu, cpu->a, rd(cpu, addr)); cycles = 4; break;
        case 0xDD: addr = am_absx(cpu, &crossed); op_compare(cpu, cpu->a, rd(cpu, addr)); cycles = 4 + crossed; break;
        case 0xD9: addr = am_absy(cpu, &crossed); op_compare(cpu, cpu->a, rd(cpu, addr)); cycles = 4 + crossed; break;
        case 0xC1: addr = am_indx(cpu); op_compare(cpu, cpu->a, rd(cpu, addr)); cycles = 6; break;
        case 0xD1: addr = am_indy(cpu, &crossed); op_compare(cpu, cpu->a, rd(cpu, addr)); cycles = 5 + crossed; break;

        /* --- CPX/CPY --- */
        case 0xE0: v = rd(cpu, cpu->pc++); op_compare(cpu, cpu->x, v); cycles = 2; break;
        case 0xE4: addr = am_zp(cpu); op_compare(cpu, cpu->x, rd(cpu, addr)); cycles = 3; break;
        case 0xEC: addr = am_abs(cpu); op_compare(cpu, cpu->x, rd(cpu, addr)); cycles = 4; break;
        case 0xC0: v = rd(cpu, cpu->pc++); op_compare(cpu, cpu->y, v); cycles = 2; break;
        case 0xC4: addr = am_zp(cpu); op_compare(cpu, cpu->y, rd(cpu, addr)); cycles = 3; break;
        case 0xCC: addr = am_abs(cpu); op_compare(cpu, cpu->y, rd(cpu, addr)); cycles = 4; break;

        /* --- DEC/DEX/DEY --- */
        case 0xC6: addr = am_zp(cpu); v = (uint8_t)(rd(cpu, addr) - 1); wr(cpu, addr, v); set_nz(cpu, v); cycles = 5; break;
        case 0xD6: addr = am_zpx(cpu); v = (uint8_t)(rd(cpu, addr) - 1); wr(cpu, addr, v); set_nz(cpu, v); cycles = 6; break;
        case 0xCE: addr = am_abs(cpu); v = (uint8_t)(rd(cpu, addr) - 1); wr(cpu, addr, v); set_nz(cpu, v); cycles = 6; break;
        case 0xDE: addr = am_absx(cpu, &crossed); v = (uint8_t)(rd(cpu, addr) - 1); wr(cpu, addr, v); set_nz(cpu, v); cycles = 7; break;
        case 0xCA: cpu->x = (uint8_t)(cpu->x - 1); set_nz(cpu, cpu->x); cycles = 2; break;
        case 0x88: cpu->y = (uint8_t)(cpu->y - 1); set_nz(cpu, cpu->y); cycles = 2; break;

        /* --- EOR --- */
        case 0x49: v = rd(cpu, cpu->pc++); cpu->a ^= v; set_nz(cpu, cpu->a); cycles = 2; break;
        case 0x45: addr = am_zp(cpu); cpu->a ^= rd(cpu, addr); set_nz(cpu, cpu->a); cycles = 3; break;
        case 0x55: addr = am_zpx(cpu); cpu->a ^= rd(cpu, addr); set_nz(cpu, cpu->a); cycles = 4; break;
        case 0x4D: addr = am_abs(cpu); cpu->a ^= rd(cpu, addr); set_nz(cpu, cpu->a); cycles = 4; break;
        case 0x5D: addr = am_absx(cpu, &crossed); cpu->a ^= rd(cpu, addr); set_nz(cpu, cpu->a); cycles = 4 + crossed; break;
        case 0x59: addr = am_absy(cpu, &crossed); cpu->a ^= rd(cpu, addr); set_nz(cpu, cpu->a); cycles = 4 + crossed; break;
        case 0x41: addr = am_indx(cpu); cpu->a ^= rd(cpu, addr); set_nz(cpu, cpu->a); cycles = 6; break;
        case 0x51: addr = am_indy(cpu, &crossed); cpu->a ^= rd(cpu, addr); set_nz(cpu, cpu->a); cycles = 5 + crossed; break;

        /* --- INC/INX/INY --- */
        case 0xE6: addr = am_zp(cpu); v = (uint8_t)(rd(cpu, addr) + 1); wr(cpu, addr, v); set_nz(cpu, v); cycles = 5; break;
        case 0xF6: addr = am_zpx(cpu); v = (uint8_t)(rd(cpu, addr) + 1); wr(cpu, addr, v); set_nz(cpu, v); cycles = 6; break;
        case 0xEE: addr = am_abs(cpu); v = (uint8_t)(rd(cpu, addr) + 1); wr(cpu, addr, v); set_nz(cpu, v); cycles = 6; break;
        case 0xFE: addr = am_absx(cpu, &crossed); v = (uint8_t)(rd(cpu, addr) + 1); wr(cpu, addr, v); set_nz(cpu, v); cycles = 7; break;
        case 0xE8: cpu->x = (uint8_t)(cpu->x + 1); set_nz(cpu, cpu->x); cycles = 2; break;
        case 0xC8: cpu->y = (uint8_t)(cpu->y + 1); set_nz(cpu, cpu->y); cycles = 2; break;

        /* --- JMP --- */
        case 0x4C: cpu->pc = am_abs(cpu); cycles = 3; break;
        case 0x6C: {
            uint16_t ptr = rd16(cpu, cpu->pc); cpu->pc = (uint16_t)(cpu->pc + 2);
            /* Famous NMOS bug: if the low byte of the pointer is $FF,
             * the high byte of the target is fetched from $xx00 (the
             * SAME page), not the next page - the indirection never
             * carries into the high byte of the pointer address. */
            uint8_t lo = rd(cpu, ptr);
            uint8_t hi = rd(cpu, (uint16_t)((ptr & 0xFF00) | ((ptr + 1) & 0xFF)));
            cpu->pc = (uint16_t)(lo | (hi << 8));
            cycles = 5; break;
        }

        /* --- JSR/RTS --- */
        case 0x20: {
            uint16_t target = rd16(cpu, cpu->pc);
            push16(cpu, (uint16_t)(cpu->pc + 1)); /* return address pushed is the LAST byte of JSR itself, not the next instruction */
            cpu->pc = target;
            cycles = 6; break;
        }
        case 0x60: cpu->pc = (uint16_t)(pop16(cpu) + 1); cycles = 6; break;

        /* --- LDA/LDX/LDY --- */
        case 0xA9: cpu->a = rd(cpu, cpu->pc++); set_nz(cpu, cpu->a); cycles = 2; break;
        case 0xA5: addr = am_zp(cpu); cpu->a = rd(cpu, addr); set_nz(cpu, cpu->a); cycles = 3; break;
        case 0xB5: addr = am_zpx(cpu); cpu->a = rd(cpu, addr); set_nz(cpu, cpu->a); cycles = 4; break;
        case 0xAD: addr = am_abs(cpu); cpu->a = rd(cpu, addr); set_nz(cpu, cpu->a); cycles = 4; break;
        case 0xBD: addr = am_absx(cpu, &crossed); cpu->a = rd(cpu, addr); set_nz(cpu, cpu->a); cycles = 4 + crossed; break;
        case 0xB9: addr = am_absy(cpu, &crossed); cpu->a = rd(cpu, addr); set_nz(cpu, cpu->a); cycles = 4 + crossed; break;
        case 0xA1: addr = am_indx(cpu); cpu->a = rd(cpu, addr); set_nz(cpu, cpu->a); cycles = 6; break;
        case 0xB1: addr = am_indy(cpu, &crossed); cpu->a = rd(cpu, addr); set_nz(cpu, cpu->a); cycles = 5 + crossed; break;

        case 0xA2: cpu->x = rd(cpu, cpu->pc++); set_nz(cpu, cpu->x); cycles = 2; break;
        case 0xA6: addr = am_zp(cpu); cpu->x = rd(cpu, addr); set_nz(cpu, cpu->x); cycles = 3; break;
        case 0xB6: addr = am_zpy(cpu); cpu->x = rd(cpu, addr); set_nz(cpu, cpu->x); cycles = 4; break;
        case 0xAE: addr = am_abs(cpu); cpu->x = rd(cpu, addr); set_nz(cpu, cpu->x); cycles = 4; break;
        case 0xBE: addr = am_absy(cpu, &crossed); cpu->x = rd(cpu, addr); set_nz(cpu, cpu->x); cycles = 4 + crossed; break;

        case 0xA0: cpu->y = rd(cpu, cpu->pc++); set_nz(cpu, cpu->y); cycles = 2; break;
        case 0xA4: addr = am_zp(cpu); cpu->y = rd(cpu, addr); set_nz(cpu, cpu->y); cycles = 3; break;
        case 0xB4: addr = am_zpx(cpu); cpu->y = rd(cpu, addr); set_nz(cpu, cpu->y); cycles = 4; break;
        case 0xAC: addr = am_abs(cpu); cpu->y = rd(cpu, addr); set_nz(cpu, cpu->y); cycles = 4; break;
        case 0xBC: addr = am_absx(cpu, &crossed); cpu->y = rd(cpu, addr); set_nz(cpu, cpu->y); cycles = 4 + crossed; break;

        /* --- LSR --- */
        case 0x4A: cpu->a = op_lsr(cpu, cpu->a); cycles = 2; break;
        case 0x46: addr = am_zp(cpu); wr(cpu, addr, op_lsr(cpu, rd(cpu, addr))); cycles = 5; break;
        case 0x56: addr = am_zpx(cpu); wr(cpu, addr, op_lsr(cpu, rd(cpu, addr))); cycles = 6; break;
        case 0x4E: addr = am_abs(cpu); wr(cpu, addr, op_lsr(cpu, rd(cpu, addr))); cycles = 6; break;
        case 0x5E: addr = am_absx(cpu, &crossed); wr(cpu, addr, op_lsr(cpu, rd(cpu, addr))); cycles = 7; break;

        /* --- NOP --- */
        case 0xEA: cycles = 2; break;

        /* --- ORA --- */
        case 0x09: v = rd(cpu, cpu->pc++); cpu->a |= v; set_nz(cpu, cpu->a); cycles = 2; break;
        case 0x05: addr = am_zp(cpu); cpu->a |= rd(cpu, addr); set_nz(cpu, cpu->a); cycles = 3; break;
        case 0x15: addr = am_zpx(cpu); cpu->a |= rd(cpu, addr); set_nz(cpu, cpu->a); cycles = 4; break;
        case 0x0D: addr = am_abs(cpu); cpu->a |= rd(cpu, addr); set_nz(cpu, cpu->a); cycles = 4; break;
        case 0x1D: addr = am_absx(cpu, &crossed); cpu->a |= rd(cpu, addr); set_nz(cpu, cpu->a); cycles = 4 + crossed; break;
        case 0x19: addr = am_absy(cpu, &crossed); cpu->a |= rd(cpu, addr); set_nz(cpu, cpu->a); cycles = 4 + crossed; break;
        case 0x01: addr = am_indx(cpu); cpu->a |= rd(cpu, addr); set_nz(cpu, cpu->a); cycles = 6; break;
        case 0x11: addr = am_indy(cpu, &crossed); cpu->a |= rd(cpu, addr); set_nz(cpu, cpu->a); cycles = 5 + crossed; break;

        /* --- stack: PHA/PHP/PLA/PLP --- */
        case 0x48: push8(cpu, cpu->a); cycles = 3; break;
        case 0x08: push8(cpu, (uint8_t)(cpu->p | FLAG_B | FLAG_U)); cycles = 3; break;
        case 0x68: cpu->a = pop8(cpu); set_nz(cpu, cpu->a); cycles = 4; break;
        case 0x28: cpu->p = (uint8_t)((pop8(cpu) | FLAG_U) & ~FLAG_B); cycles = 4; break;

        /* --- ROL/ROR --- */
        case 0x2A: cpu->a = op_rol(cpu, cpu->a); cycles = 2; break;
        case 0x26: addr = am_zp(cpu); wr(cpu, addr, op_rol(cpu, rd(cpu, addr))); cycles = 5; break;
        case 0x36: addr = am_zpx(cpu); wr(cpu, addr, op_rol(cpu, rd(cpu, addr))); cycles = 6; break;
        case 0x2E: addr = am_abs(cpu); wr(cpu, addr, op_rol(cpu, rd(cpu, addr))); cycles = 6; break;
        case 0x3E: addr = am_absx(cpu, &crossed); wr(cpu, addr, op_rol(cpu, rd(cpu, addr))); cycles = 7; break;
        case 0x6A: cpu->a = op_ror(cpu, cpu->a); cycles = 2; break;
        case 0x66: addr = am_zp(cpu); wr(cpu, addr, op_ror(cpu, rd(cpu, addr))); cycles = 5; break;
        case 0x76: addr = am_zpx(cpu); wr(cpu, addr, op_ror(cpu, rd(cpu, addr))); cycles = 6; break;
        case 0x6E: addr = am_abs(cpu); wr(cpu, addr, op_ror(cpu, rd(cpu, addr))); cycles = 6; break;
        case 0x7E: addr = am_absx(cpu, &crossed); wr(cpu, addr, op_ror(cpu, rd(cpu, addr))); cycles = 7; break;

        /* --- RTI --- */
        case 0x40:
            cpu->p = (uint8_t)((pop8(cpu) | FLAG_U) & ~FLAG_B);
            cpu->pc = pop16(cpu);
            cycles = 6; break;

        /* --- SBC --- */
        case 0xE9: v = rd(cpu, cpu->pc++); op_sbc(cpu, v); cycles = 2; break;
        case 0xE5: addr = am_zp(cpu); op_sbc(cpu, rd(cpu, addr)); cycles = 3; break;
        case 0xF5: addr = am_zpx(cpu); op_sbc(cpu, rd(cpu, addr)); cycles = 4; break;
        case 0xED: addr = am_abs(cpu); op_sbc(cpu, rd(cpu, addr)); cycles = 4; break;
        case 0xFD: addr = am_absx(cpu, &crossed); op_sbc(cpu, rd(cpu, addr)); cycles = 4 + crossed; break;
        case 0xF9: addr = am_absy(cpu, &crossed); op_sbc(cpu, rd(cpu, addr)); cycles = 4 + crossed; break;
        case 0xE1: addr = am_indx(cpu); op_sbc(cpu, rd(cpu, addr)); cycles = 6; break;
        case 0xF1: addr = am_indy(cpu, &crossed); op_sbc(cpu, rd(cpu, addr)); cycles = 5 + crossed; break;

        /* --- STA/STX/STY (always fixed cycles - a real dummy read
         * happens on indexed modes regardless of page-crossing, so
         * `crossed` is deliberately unused here) --- */
        case 0x85: addr = am_zp(cpu); wr(cpu, addr, cpu->a); cycles = 3; break;
        case 0x95: addr = am_zpx(cpu); wr(cpu, addr, cpu->a); cycles = 4; break;
        case 0x8D: addr = am_abs(cpu); wr(cpu, addr, cpu->a); cycles = 4; break;
        case 0x9D: addr = am_absx(cpu, &crossed); wr(cpu, addr, cpu->a); cycles = 5; break;
        case 0x99: addr = am_absy(cpu, &crossed); wr(cpu, addr, cpu->a); cycles = 5; break;
        case 0x81: addr = am_indx(cpu); wr(cpu, addr, cpu->a); cycles = 6; break;
        case 0x91: addr = am_indy(cpu, &crossed); wr(cpu, addr, cpu->a); cycles = 6; break;
        case 0x86: addr = am_zp(cpu); wr(cpu, addr, cpu->x); cycles = 3; break;
        case 0x96: addr = am_zpy(cpu); wr(cpu, addr, cpu->x); cycles = 4; break;
        case 0x8E: addr = am_abs(cpu); wr(cpu, addr, cpu->x); cycles = 4; break;
        case 0x84: addr = am_zp(cpu); wr(cpu, addr, cpu->y); cycles = 3; break;
        case 0x94: addr = am_zpx(cpu); wr(cpu, addr, cpu->y); cycles = 4; break;
        case 0x8C: addr = am_abs(cpu); wr(cpu, addr, cpu->y); cycles = 4; break;

        /* --- register transfers --- */
        case 0xAA: cpu->x = cpu->a; set_nz(cpu, cpu->x); cycles = 2; break;
        case 0xA8: cpu->y = cpu->a; set_nz(cpu, cpu->y); cycles = 2; break;
        case 0xBA: cpu->x = cpu->sp; set_nz(cpu, cpu->x); cycles = 2; break;
        case 0x8A: cpu->a = cpu->x; set_nz(cpu, cpu->a); cycles = 2; break;
        case 0x9A: cpu->sp = cpu->x; cycles = 2; break; /* TXS does NOT touch N/Z */
        case 0x98: cpu->a = cpu->y; set_nz(cpu, cpu->a); cycles = 2; break;

        default:
            /* An illegal/undocumented opcode - none of Dormann's test
             * program uses one, and this core doesn't implement any,
             * so reaching here means something upstream (a bad jump,
             * a desynced PC after a bug elsewhere in this file) fed
             * the CPU garbage. Treat it as a 1-byte, 2-cycle no-op
             * rather than crashing - loud enough to show up as wrong
             * behavior in a test, without taking the whole process
             * down over what a real 6502 wouldn't crash on either. */
            cycles = 2;
            break;
    }

    cpu->cycles += (uint64_t)cycles;
    return cycles;
}
