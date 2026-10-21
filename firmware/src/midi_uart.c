/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) - from Melodee's midi_uart.c, adapted for Zen-Lite
 * Portions Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* MIDI IN on the TRS jack: PH8 -> input channel 1 -> UART1 RX, 31250 baud, RX DMA into a 128-byte ring, polled
 * from the TIMER5 ISR (no UART IRQ), the same context as usb_poll. Channel messages go into the same ring as
 * USB-MIDI (as USB-MIDI packets: cable 0, CIN = status >> 4), so the app cannot tell the two apart; clock, start,
 * continue and stop are also counted in usb_clk, as the USB ones are. Other realtime, system common and SysEx are
 * ignored (no SysEx -> UBOOT from the jack).
 *
 * The ring is read by content, not by the DMA count: every slot the parser has not yet been given holds UM_EMPTY,
 * and the reader takes bytes until it meets one. (Melodee found the count can miss a byte on an FM-1 while the
 * byte itself is in the ring; following the content cannot drift.) The count still tells an overrun. */
#include "fm1_uart.h"

#define UM_RING 128u
#define UM_EMPTY 0xFDu                          /* an undefined realtime byte: never sent, marks unread slots */
static volatile uint8_t um_ring[UM_RING] __attribute__((aligned(16)));
static struct {
    uint32_t rd;
    volatile uint32_t bytes, drops, msgs;
    uint8_t st, need, got, d0, sysex;
} um;

static void uart_midi_init(void)                /* before TIMER5 starts (PORTH read-modify-write) */
{
    uint32_t i;
    for (i = 0; i < UM_RING; i++) um_ring[i] = UM_EMPTY;
    fm1_uart1_midi_init(um_ring, UM_RING);
}

static void um_byte(uint32_t b)
{
    if (b >= 0xF8u) {                           /* realtime: keeps running status */
        if (b == 0xF8u) usb_clk.ticks++;
        else if (b == 0xFAu) usb_clk.starts++;
        else if (b == 0xFBu) usb_clk.conts++;
        else if (b == 0xFCu) usb_clk.stops++;
        else return;
        if (midi_enqueue(0xFu | b << 8)) um.msgs++; else um.drops++;
        return;
    }
    if (b & 0x80u) {
        um.sysex = b == 0xF0u;
        um.st = b < 0xF0u ? (uint8_t)b : 0;     /* system common / SysEx cancel running status */
        um.need = (b & 0xE0u) == 0xC0u ? 1u : 2u;   /* Cx Dx: one data byte, the rest two */
        um.got = 0;
        return;
    }
    if (um.sysex || !um.st) return;
    if (um.need == 2u && !um.got) { um.d0 = (uint8_t)b; um.got = 1; return; }
    {
        uint32_t d1 = um.need == 2u ? um.d0 : b, d2 = um.need == 2u ? b : 0u;
        um.got = 0;
        if (midi_enqueue((um.st >> 4) | (uint32_t)um.st << 8 | d1 << 16 | d2 << 24)) um.msgs++; else um.drops++;
    }
}

static void uart_midi_poll(void)                /* TIMER5 ISR */
{
    uint32_t n = fm1_uart1_rx_take(), k;
    if (n > UM_RING) {                          /* the DMA went round past unread bytes: wait for a new status */
        um.drops += n - UM_RING;
        um.rd = (um.rd + n - UM_RING) & (UM_RING - 1u);
        um.st = um.got = um.sysex = 0;
    }
    for (k = 0; k < UM_RING; k++) {
        uint32_t b = um_ring[um.rd];
        if (b == UM_EMPTY) {
            if (um_ring[(um.rd + 1u) & (UM_RING - 1u)] == UM_EMPTY) break;
            b = um_ring[um.rd];                 /* the DMA writes in order: this one has landed too */
        }
        um_ring[um.rd] = UM_EMPTY;
        um.rd = (um.rd + 1u) & (UM_RING - 1u);
        um.bytes++;
        um_byte(b);
    }
}
