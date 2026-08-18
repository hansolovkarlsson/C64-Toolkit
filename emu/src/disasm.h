#ifndef C64EMU_DISASM_H
#define C64EMU_DISASM_H

#include <stddef.h>
#include <stdint.h>
#include "memory.h"

/* A linear, single-instruction 6502/6510 disassembler for the GTK
 * debugger view - deliberately NOT the flow-following disassembler
 * asm/single_src/c64disasm.py is. That tool distinguishes code from
 * data by following branches/jumps from a known entry point through a
 * whole static .prg file; this one just decodes whatever byte
 * sequence sits at a given address right now, the same thing a live
 * debugger's instruction view always has to do, since a paused CPU
 * can be sitting anywhere - there's no "whole program" to flow-follow
 * ahead of time, only the current PC and whatever's next.
 *
 * Reads instruction bytes through memory_read() - the same
 * bank-switched view the CPU itself sees (see cpu.h's CpuBus comment)
 * - so disassembly always matches whatever ROM/RAM/I/O is currently
 * mapped in at that address, not necessarily the underlying RAM byte.
 *
 * Only the 151 documented/legal 6502 opcodes are decoded. The table's
 * values are hand-transcribed from asm/single_src/c64asm.py's own
 * OPCODES table (the assembler's encode table) - hand-transcribed
 * rather than shared at build time since that file is Python and this
 * is C, but the values themselves are ported, not re-derived from
 * scratch. Illegal/undocumented opcodes (c64asm.py's ILLEGAL_OPCODES -
 * about 105 more real opcode bytes the NMOS 6502 actually executes)
 * are not decoded here, the same scope limit c64disasm.py documents
 * for itself - disasm_one() just shows "???" and advances by 1 byte,
 * which is honest and enough to keep a debugger view moving forward. */

/* Decodes one instruction at addr, writes a formatted line (e.g.
 * "$C000: A9 05    LDA #$05") into out, and returns the instruction's
 * length in bytes (1-3, matching what was actually read) so a caller
 * can advance addr += returned length to disassemble a forward run.
 * An unrecognized (illegal/undocumented) opcode byte formats as
 * "???" and returns 1, so a caller never needs to special-case it to
 * keep making forward progress. */
int disasm_one(Memory *mem, uint16_t addr, char *out, size_t out_size);

#endif
