/* Copyright (c) 2025 Martin R. Raumann */
/* SPDX-License-Identifier: BSD-3-Clause */
/*******************************************************************************
  terminal_io.h - Serial terminal input for the screen layer on the SAM E51

  Interrupt-driven receive into a ring (keystrokes survive blocking transmit),
  key decoding, and the terminal size probe. Shared by the screen demo and the
  V4 game firmware. Call term_start() once after SYS_Initialize().
*******************************************************************************/
#ifndef TERMINAL_IO_H
#define TERMINAL_IO_H

#include <stdint.h>

extern volatile uint32_t term_rx_dropped;   /* software ring overflow */
extern volatile uint32_t term_rx_total;     /* every byte the receive interrupt saw */
extern volatile uint32_t term_rx_sum;       /* running sum of those bytes */
extern uint8_t term_rows, term_cols;        /* set by term_probe_size(); 24x80 until then */

/** Start the receive interrupt */
void term_start(void);
/** Next received byte, or -1 if none is waiting */
int term_rx_get(void);
/** Next received byte, waiting for it */
int term_rx_wait(void);
/** Transmit one byte (blocking) */
void term_put_byte(uint8_t b);
/** Ask the terminal for its size (CSI 6n); keeps 24x80 if nothing sensible answers */
void term_probe_size(void);
/** Next keystroke with CR, LF and CR+LF all delivered as one '\r'; escape sequences swallowed */
int term_next_key(void);
/**
 * Next keystroke as the ZSCII code a story sees: 13 Return, 8 Delete, 27 Escape,
 * 129-132 cursor up/down/left/right. Other escape sequences are swallowed.
 */
int term_next_zscii(void);

#endif
