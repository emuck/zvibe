/* Copyright (c) 2025 Martin R. Raumann */
/* SPDX-License-Identifier: BSD-3-Clause */
/* terminal_io.c - see terminal_io.h */

#include <stdint.h>
#include "board_support.h"
#include "terminal_io.h"

#define RX_RING 256
static volatile uint8_t  rx_ring[RX_RING];
static volatile uint16_t rx_head, rx_tail;
volatile uint32_t term_rx_dropped;
volatile uint32_t term_rx_total;
volatile uint32_t term_rx_sum;
uint8_t term_rows = 24, term_cols = 80;

static void rx_callback(uintptr_t ctx) {
    uint8_t c = (uint8_t)SERCOM5_USART_ReadByte();
    uint16_t next = (uint16_t)((rx_head + 1) % RX_RING);
    (void)ctx;
    term_rx_total++;
    term_rx_sum += c;
    if (next == rx_tail) { term_rx_dropped++; return; }
    rx_ring[rx_head] = c;
    rx_head = next;
}

void term_start(void) {
    SERCOM5_USART_ReadCallbackRegister(rx_callback, 0);
    SERCOM5_USART_EnableRxInterrupt();
}

int term_rx_get(void) {
    uint8_t c;
    if (rx_head == rx_tail) return -1;
    c = rx_ring[rx_tail];
    rx_tail = (uint16_t)((rx_tail + 1) % RX_RING);
    return c;
}

static volatile uint8_t abort_requested;

void term_request_abort(void) { abort_requested = 1; }

int term_take_abort(void) {
    if (!abort_requested) return 0;
    abort_requested = 0;
    return 1;
}

int term_rx_wait(void) {
    int c;
    while ((c = term_rx_get()) < 0) {
        if (abort_requested) { abort_requested = 0; return TERM_ABORT; }
    }
    return c;
}

/* Put a byte back at the front of the ring (what the size probe read but was not its answer). Only the main loop
 * moves the tail, so this does not race the receive interrupt; a full ring drops the byte. */
static void rx_unget(uint8_t c) {
    uint16_t before = (uint16_t)((rx_tail + RX_RING - 1) % RX_RING);
    if (before == rx_head) return;
    rx_ring[before] = c;
    rx_tail = before;
}

void term_put_byte(uint8_t b) { SERCOM5_USART_WriteByte(b); }

/* Park the cursor far off-screen (the terminal clamps it to the corner) and read back
 * the position report ESC [ r ; c R. Whatever else arrives while waiting (typing ahead, say) is not the answer:
 * it is put back for whoever reads next. The numbers stop growing past 3 digits (no overflow from a stream of digits). */
void term_probe_size(void) {
    static const char q[] = "\033[999;999H\033[6n";
    unsigned long spin;
    int state = 0, row = 0, col = 0, c, n, kept = 0;
    uint8_t other[16];
    const char *p;
    for (p = q; *p; p++) term_put_byte((uint8_t)*p);
    for (spin = 0; spin < 20000000UL; spin++) {
        c = term_rx_get();
        if (c < 0) continue;
        if (state == 0 && c == 0x1B) state = 1;
        else if (state == 1 && c == '[') state = 2;
        else if (state == 2 && c >= '0' && c <= '9') { if (row < 1000) row = row * 10 + (c - '0'); }
        else if (state == 2 && c == ';') state = 3;
        else if (state == 3 && c >= '0' && c <= '9') { if (col < 1000) col = col * 10 + (c - '0'); }
        else if (state == 3 && c == 'R') {
            if (row >= 10 && row <= 250 && col >= 56) {
                term_rows = (uint8_t)row;
                term_cols = (uint8_t)(col > 255 ? 255 : col);     /* the screen layer caps it at 80 */
            }
            break;
        }
        else {
            if (kept < (int)sizeof other) other[kept++] = (uint8_t)c;       /* not part of the answer */
            state = 0; row = col = 0;
        }
    }
    for (n = kept - 1; n >= 0; n--) rx_unget(other[n]);       /* in the order they came */
}

/* A lone ESC eats the key after it. */
int term_next_key(void) {
    static int state = 0;      /* 0 normal, 1 after ESC, 2 inside CSI/SS3 */
    static int last_cr = 0;
    for (;;) {
        int c = term_rx_wait();
        if (c == TERM_ABORT) { state = 0; return TERM_ABORT; }
        if (state == 1) { state = (c == '[' || c == 'O') ? 2 : 0; continue; }
        if (state == 2) { if (c >= 0x40 && c <= 0x7E) state = 0; continue; }
        if (c == 0x1B) { state = 1; continue; }
        if (c == '\n' && last_cr) { last_cr = 0; continue; }
        last_cr = (c == '\r');
        return c == '\n' ? '\r' : c;
    }
}

/* Byte that follows within a few milliseconds (the tail of an escape sequence), or -1 */
static int rx_soon(void) {
    unsigned long spin;
    int c;
    for (spin = 0; spin < 2000000UL; spin++)
        if ((c = term_rx_get()) >= 0) return c;
    return -1;
}

int term_next_zscii(void) {
    static int last_cr = 0;
    static int pending = -1;       /* a byte read past the end of an Escape, to be delivered next */
    for (;;) {
        int c;
        if (pending >= 0) { c = pending; pending = -1; }
        else c = term_rx_wait();
        if (c == TERM_ABORT) return TERM_ABORT;
        if (c == '\n' && last_cr) { last_cr = 0; continue; }       /* the LF of CR+LF */
        last_cr = (c == '\r');
        if (c == '\r' || c == '\n') return 13;
        if (c == 8 || c == 127) return 8;
        if (c == 27) {
            c = rx_soon();
            if (c != '[' && c != 'O') {                           /* a lone Escape; the next key is its own */
                pending = c;
                return 27;
            }
            c = rx_soon();
            if (c == 'A') return 129;
            if (c == 'B') return 130;
            if (c == 'D') return 131;
            if (c == 'C') return 132;
            while (c >= 0 && !(c >= 0x40 && c <= 0x7E)) c = rx_soon();
            continue;
        }
        if (c >= 32 && c < 127) return c;
    }
}
