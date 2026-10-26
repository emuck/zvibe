/* Copyright (c) 2025 Martin R. Raumann */
/* SPDX-License-Identifier: BSD-3-Clause */
/*******************************************************************************
  Screen layer exerciser - SAM E51 Curiosity Nano

  Stage 1 of the V4 feasibility work: proves the split-window screen layer
  (src/screen/zvibe_screen.c) on real hardware, with no Z-machine involved.

  Build:   make screen-demo
  Flash:   make screen-demo-program
  Connect: 115200 8N1 on the EDBG virtual COM port, any xterm-compatible
           terminal sized at least 80x24.

  The demo runs these phases, pressing RETURN to advance:
    1. Status bar + long scrolling lower window (bar must never move)
    2. Word-wrap and styles (reverse / bold / underline mixed with wrapping)
    3. Fixed-width "form" drawn in the upper window; answers are typed with
       echo/backspace in the lower window, then written into the form
       (editing directly inside the upper window is NOT exercised)
    4. Throughput burst: 300 lines while you paste known text; the demo
       reports exact RX byte count and byte-sum for comparison with the host

  On start the demo asks the terminal for its size (CSI 6n); it falls back to
  80x24 if there is no reply. Terminals narrower than 56 columns are unsupported.
*******************************************************************************/

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "board_support.h"
#include "../../../include/zvibe_screen.h"

#include "terminal_io.h"

static zscreen scr;

static void status(const char *left, const char *right) {
    int i;
    zscreen_set_window(&scr, 1);
    zscreen_set_text_style(&scr, ZSCREEN_STYLE_REVERSE);
    zscreen_set_cursor(&scr, 1, 1);
    for (i = 0; i < scr.cols; i++) zscreen_putc(&scr, ' ');
    zscreen_set_cursor(&scr, 1, 2);
    zscreen_puts(&scr, left);
    zscreen_set_cursor(&scr, 1, (uint16_t)(scr.cols - 1 - strlen(right)));
    zscreen_puts(&scr, right);
    zscreen_set_text_style(&scr, ZSCREEN_STYLE_ROMAN);
    zscreen_set_window(&scr, 0);
}

/* Read a line with echo into buf (max n-1 chars). */
static void read_line(char *buf, uint8_t n) {
    uint8_t len = 0;
    zscreen_flush(&scr);
    for (;;) {
        char c = (char)term_next_key();
        if (c == '\r') { zscreen_echo(&scr, '\n'); break; }
        if (c == 0x08 || c == 0x7F) {
            if (len && zscreen_echo(&scr, c)) len--;
        } else if (c >= 32 && c < 127 && len < n - 1) {
            if (zscreen_echo(&scr, c)) buf[len++] = c;     /* store only what the screen accepted */
        }
    }
    buf[len] = '\0';
}

static void press_return(void) {
    zscreen_puts(&scr, "\n[press RETURN] ");
    zscreen_flush(&scr);
    while (term_next_key() != '\r') {}
    zscreen_echo(&scr, '\n');
}

static void phase_scroll(void) {
    char buf[64];
    int i;
    zscreen_erase_window(&scr, -1);
    zscreen_split_window(&scr, 1);
    status("PHASE 1: scroll", "Score: 0");
    for (i = 1; i <= 60; i++) {
        snprintf(buf, sizeof buf, "Scroll line %d - the bar must stay put.\n", i);
        zscreen_puts(&scr, buf);
        if (i == 30) status("PHASE 1: scroll", "Score: 30");
    }
    status("PHASE 1: scroll", "Score: 60");
    zscreen_puts(&scr, "Done. Bar should read Score: 60 and be reverse video.\n");
    press_return();
}

static void phase_wrap(void) {
    int i;
    zscreen_erase_window(&scr, -1);
    zscreen_split_window(&scr, 1);
    status("PHASE 2: wrap + styles", "");
    for (i = 0; i < 3; i++) {
        zscreen_puts(&scr, "You are standing in an open field west of a white house, with a boarded front door. ");
        zscreen_set_text_style(&scr, ZSCREEN_STYLE_REVERSE);
        zscreen_puts(&scr, "There is a small mailbox here");
        zscreen_set_text_style(&scr, ZSCREEN_STYLE_ROMAN);
        zscreen_puts(&scr, ", ");
        zscreen_set_text_style(&scr, ZSCREEN_STYLE_BOLD);
        zscreen_puts(&scr, "and a rather bold sentence that ought to wrap cleanly at a word boundary");
        zscreen_set_text_style(&scr, ZSCREEN_STYLE_ITALIC);
        zscreen_puts(&scr, " followed by underlined text");
        zscreen_set_text_style(&scr, ZSCREEN_STYLE_ROMAN);
        zscreen_puts(&scr, ".\n\n");
    }
    zscreen_puts(&scr, "Check: no word split across lines, styles stop at their words.\n");
    press_return();
}

static void phase_form(void) {
    char dad[24], city[24];
    int r;
    zscreen_erase_window(&scr, -1);
    zscreen_split_window(&scr, 9);
    zscreen_set_window(&scr, 1);
    zscreen_erase_window(&scr, 1);
    zscreen_set_text_style(&scr, ZSCREEN_STYLE_REVERSE);
    zscreen_set_cursor(&scr, 1, 1);
    zscreen_puts(&scr, "  FORM 27B/6: APPLICATION FOR PERMISSION TO APPLY                              ");
    zscreen_set_text_style(&scr, ZSCREEN_STYLE_ROMAN);
    zscreen_set_cursor(&scr, 3, 2);  zscreen_puts(&scr, "+--------------------------------------------------+");
    for (r = 4; r <= 7; r++) {
        zscreen_set_cursor(&scr, (uint16_t)r, 2);  zscreen_putc(&scr, '|');
        zscreen_set_cursor(&scr, (uint16_t)r, 53); zscreen_putc(&scr, '|');
    }
    zscreen_set_cursor(&scr, 8, 2);  zscreen_puts(&scr, "+--------------------------------------------------+");
    zscreen_set_cursor(&scr, 5, 4);  zscreen_puts(&scr, "Father's name:");
    zscreen_set_cursor(&scr, 6, 4);  zscreen_puts(&scr, "Favorite city:");
    zscreen_set_window(&scr, 0);

    zscreen_puts(&scr, "Type with backspace; the form must not be disturbed.\n");
    zscreen_puts(&scr, "Father's name? > ");
    read_line(dad, sizeof dad);
    zscreen_puts(&scr, "Favorite city? > ");
    read_line(city, sizeof city);

    /* Fill in the form fields (upper window), then return to the lower cursor. */
    zscreen_set_window(&scr, 1);
    zscreen_set_text_style(&scr, ZSCREEN_STYLE_BOLD);
    zscreen_set_cursor(&scr, 5, 19); zscreen_puts(&scr, dad);
    zscreen_set_cursor(&scr, 6, 19); zscreen_puts(&scr, city);
    zscreen_set_text_style(&scr, ZSCREEN_STYLE_ROMAN);
    zscreen_set_window(&scr, 0);

    zscreen_puts(&scr, "\nYour father, ");
    zscreen_puts(&scr, dad);
    zscreen_puts(&scr, ", is hereby required to relocate to ");
    zscreen_puts(&scr, city);
    zscreen_puts(&scr, " in triplicate. Form 27B/6 has been filed under 'Other'.\n");
    press_return();
}

static void phase_burst(void) {
    char buf[128];
    int i;
    zscreen_erase_window(&scr, -1);
    zscreen_split_window(&scr, 1);
    status("PHASE 4: burst - type while this runs", "");
    uint32_t err_before = SERCOM5_USART_ErrorCount;
    term_rx_dropped = 0;
    term_rx_total = 0;
    term_rx_sum = 0;
    zscreen_puts(&scr, "Paste or type now; totals are shown at the end.\n");
    for (i = 1; i <= 300; i++) {
        snprintf(buf, sizeof buf,
                 "%03d the quick brown fox jumps over the lazy dog 0123456789 abcdefghijklmnopqrstuv", i);
        zscreen_puts(&scr, buf);
        zscreen_putc(&scr, '\n');
    }
    snprintf(buf, sizeof buf, "\nBurst done. RX bytes: %lu  byte-sum: %lu\n",
             (unsigned long)term_rx_total, (unsigned long)term_rx_sum);
    zscreen_puts(&scr, buf);
    snprintf(buf, sizeof buf, "Ring overflow: %lu  UART hw errors in burst: %lu  (both expect 0)\n",
             (unsigned long)term_rx_dropped, (unsigned long)(SERCOM5_USART_ErrorCount - err_before));
    zscreen_puts(&scr, buf);
    snprintf(buf, sizeof buf, "UART hw errors since boot: %lu  (a port open/close can glitch the line)\n",
             (unsigned long)SERCOM5_USART_ErrorCount);
    zscreen_puts(&scr, buf);
    zscreen_puts(&scr, "Compare bytes/sum with what you sent (host: sum of ord() over the pasted text).\n");
    press_return();
}

int main(void) {
    SYS_Initialize(NULL);
    term_start();
    term_probe_size();
    zscreen_init(&scr, term_put_byte, term_rows, term_cols);
    for (;;) {
        phase_scroll();
        phase_wrap();
        phase_form();
        phase_burst();
    }
}
