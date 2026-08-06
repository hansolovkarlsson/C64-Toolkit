/*
 * cia.c - see cia.h's header comment for register layout and the
 * documented simplifications (TOD/SDR are passive storage, CNT-pin
 * counting isn't modeled).
 */

#include "cia.h"
#include <string.h>

enum {
    REG_PRA = 0x0, REG_PRB = 0x1, REG_DDRA = 0x2, REG_DDRB = 0x3,
    REG_TALO = 0x4, REG_TAHI = 0x5, REG_TBLO = 0x6, REG_TBHI = 0x7,
    REG_TOD10 = 0x8, REG_TODSEC = 0x9, REG_TODMIN = 0xA, REG_TODHR = 0xB,
    REG_SDR = 0xC, REG_ICR = 0xD, REG_CRA = 0xE, REG_CRB = 0xF
};

#define CR_START 0x01
#define CR_RUNMODE_ONESHOT 0x08
#define CR_FORCELOAD 0x10

void cia_init(Cia *cia) {
    memset(cia, 0, sizeof(*cia));
}

uint8_t cia_read(Cia *cia, uint8_t reg) {
    switch (reg & 0x0F) {
        case REG_PRA: return (uint8_t)((cia->pra & cia->ddra) | (cia->porta_in & (uint8_t)~cia->ddra));
        case REG_PRB: return (uint8_t)((cia->prb & cia->ddrb) | (cia->portb_in & (uint8_t)~cia->ddrb));
        case REG_DDRA: return cia->ddra;
        case REG_DDRB: return cia->ddrb;
        case REG_TALO: return (uint8_t)(cia->ta_counter & 0xFF);
        case REG_TAHI: return (uint8_t)(cia->ta_counter >> 8);
        case REG_TBLO: return (uint8_t)(cia->tb_counter & 0xFF);
        case REG_TBHI: return (uint8_t)(cia->tb_counter >> 8);
        case REG_TOD10: return cia->tod_10ths;
        case REG_TODSEC: return cia->tod_sec;
        case REG_TODMIN: return cia->tod_min;
        case REG_TODHR: return cia->tod_hr;
        case REG_SDR: return cia->sdr;
        case REG_ICR: {
            /* Bits 0-4: every pending source, regardless of mask. Bit
             * 7: whether an IRQ was actually being requested
             * (pending & mask). Reading always clears ALL pending
             * bits and so de-asserts the IRQ/NMI line - real 6526
             * behavior, not something software can opt out of. */
            uint8_t v = cia->icr_pending & 0x1F;
            if (cia->icr_pending & cia->icr_mask) v |= 0x80;
            cia->icr_pending = 0;
            return v;
        }
        case REG_CRA: return cia->cra;
        case REG_CRB: return cia->crb;
        default: return 0xFF; /* unreachable - reg & 0x0F covers 0x0-0xF */
    }
}

void cia_write(Cia *cia, uint8_t reg, uint8_t v) {
    switch (reg & 0x0F) {
        case REG_PRA: cia->pra = v; break;
        case REG_PRB: cia->prb = v; break;
        case REG_DDRA: cia->ddra = v; break;
        case REG_DDRB: cia->ddrb = v; break;
        /* Writing the low byte only ever updates the latch. Writing
         * the high byte updates the latch AND, if the timer is
         * currently stopped, also force-reloads the counter from it
         * immediately - lets software prime a timer's starting value
         * without it starting to count. If the timer is running, the
         * new latch only takes effect on the next natural underflow. */
        case REG_TALO: cia->ta_latch = (uint16_t)((cia->ta_latch & 0xFF00) | v); break;
        case REG_TAHI:
            cia->ta_latch = (uint16_t)((cia->ta_latch & 0x00FF) | ((uint16_t)v << 8));
            if (!(cia->cra & CR_START)) cia->ta_counter = cia->ta_latch;
            break;
        case REG_TBLO: cia->tb_latch = (uint16_t)((cia->tb_latch & 0xFF00) | v); break;
        case REG_TBHI:
            cia->tb_latch = (uint16_t)((cia->tb_latch & 0x00FF) | ((uint16_t)v << 8));
            if (!(cia->crb & CR_START)) cia->tb_counter = cia->tb_latch;
            break;
        case REG_TOD10: cia->tod_10ths = v; break;
        case REG_TODSEC: cia->tod_sec = v; break;
        case REG_TODMIN: cia->tod_min = v; break;
        case REG_TODHR: cia->tod_hr = v; break;
        case REG_SDR: cia->sdr = v; break;
        /* bit7: 1 = SET the mask bits given in bits 0-4, 0 = CLEAR
         * them. Bits not set in v are left alone either way - this is
         * a read-modify-write of the mask, not a plain assignment. */
        case REG_ICR:
            if (v & 0x80) cia->icr_mask |= (v & 0x1F);
            else cia->icr_mask &= (uint8_t)~(v & 0x1F);
            break;
        case REG_CRA:
            if (v & CR_FORCELOAD) cia->ta_counter = cia->ta_latch;
            cia->cra = v & (uint8_t)~CR_FORCELOAD; /* strobe bit never reads back set */
            break;
        case REG_CRB:
            if (v & CR_FORCELOAD) cia->tb_counter = cia->tb_latch;
            cia->crb = v & (uint8_t)~CR_FORCELOAD;
            break;
        default: break; /* unreachable - reg & 0x0F covers 0x0-0xF */
    }
}

void cia_tick(Cia *cia, int cycles) {
    for (int i = 0; i < cycles; i++) {
        int ta_underflowed = 0;

        if (cia->cra & CR_START) {
            if (cia->ta_counter == 0) {
                cia->ta_counter = cia->ta_latch;
                cia->icr_pending |= 0x01;
                ta_underflowed = 1;
                if (cia->cra & CR_RUNMODE_ONESHOT) cia->cra &= (uint8_t)~CR_START;
            } else {
                cia->ta_counter--;
            }
        }

        if (cia->crb & CR_START) {
            /* INMODE (CRB bits 5-6): 00 = phi2, 01 = CNT pin (not
             * modeled - never counts, see cia.h), 10/11 = Timer A
             * underflow (11's extra CNT-high qualifier collapses into
             * plain 10 here for the same reason). */
            uint8_t inmode = (cia->crb >> 5) & 0x03;
            int counts_this_tick = (inmode == 0x00) || ((inmode == 0x02 || inmode == 0x03) && ta_underflowed);
            if (counts_this_tick) {
                if (cia->tb_counter == 0) {
                    cia->tb_counter = cia->tb_latch;
                    cia->icr_pending |= 0x02;
                    if (cia->crb & CR_RUNMODE_ONESHOT) cia->crb &= (uint8_t)~CR_START;
                } else {
                    cia->tb_counter--;
                }
            }
        }
    }
}

int cia_irq_line(const Cia *cia) {
    return (cia->icr_pending & cia->icr_mask) != 0;
}
