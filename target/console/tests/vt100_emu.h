/* Copyright (c) 2025 Martin R. Raumann */
/* SPDX-License-Identifier: BSD-3-Clause */
/**
 * vt100_emu.h - A minimal VT100 terminal emulator for tests
 *
 * Models cursor addressing (CUP), scroll regions (DECSTBM, rejecting a region
 * of fewer than two lines like a real terminal), SGR rendition, erase (EL, ED),
 * auto-wrap (DECAWM) and background-colour-erase blanks, and keeps the screen as
 * a character grid with a rendition per cell. Feed it bytes with em_put().
 * Header-only; every test program including it gets its own copy of the state.
 */

#ifndef VT100_EMU_H
#define VT100_EMU_H

#include <stdint.h>
#include <string.h>

#define ROWS 24
#define COLS 80

/* ---- Minimal VT100 emulator ---- */

static char     grid[ROWS][COLS];
static uint8_t  attr[ROWS][COLS];       /* 1=reverse 2=bold 4=underline */
static int      cur_r, cur_c;           /* 0-based */
static int      reg_top, reg_bot;       /* 0-based inclusive */
static int      autowrap;
static uint8_t  cur_attr;
static int      st;                     /* 0 ground, 1 ESC, 2 CSI */
static int      params[4], nparams;
static int      private_mark;
static int      lf_scrolls;             /* scrolls performed */
static int      bytes_out;
static int      bad_region;

static void em_reset(void) {
    memset(grid, ' ', sizeof grid);
    memset(attr, 0, sizeof attr);
    cur_r = cur_c = 0; reg_top = 0; reg_bot = ROWS - 1;
    autowrap = 1; cur_attr = 0; st = 0; lf_scrolls = 0; bytes_out = 0; bad_region = 0;
}

static void em_scroll(void) {
    int r;
    for (r = reg_top; r < reg_bot; r++) {
        memcpy(grid[r], grid[r + 1], COLS);
        memcpy(attr[r], attr[r + 1], COLS);
    }
    memset(grid[reg_bot], ' ', COLS);
    memset(attr[reg_bot], cur_attr, COLS);       /* bce: blanks inherit the current rendition */
    lf_scrolls++;
}

static void em_csi(char f) {
    int p0 = nparams > 0 ? params[0] : 0;
    int p1 = nparams > 1 ? params[1] : 0;
    int i, c;
    if (private_mark) {
        if (p0 == 7) autowrap = (f == 'h');
        return;
    }
    switch (f) {
    case 'H':
        cur_r = (p0 ? p0 : 1) - 1; cur_c = (p1 ? p1 : 1) - 1;
        if (cur_r >= ROWS) cur_r = ROWS - 1;
        if (cur_c >= COLS) cur_c = COLS - 1;
        break;
    case 'r':
        if (nparams == 0) { reg_top = 0; reg_bot = ROWS - 1; }
        else if (p0 >= 1 && p1 > p0 && p1 <= ROWS) { reg_top = p0 - 1; reg_bot = p1 - 1; }
        else { bad_region++; break; }            /* real terminals ignore top >= bottom */
        cur_r = cur_c = 0;                       /* DECSTBM homes the cursor */
        break;
    case 'K':
        for (c = (p0 == 0 ? cur_c : 0); c < COLS; c++) { grid[cur_r][c] = ' '; attr[cur_r][c] = cur_attr; }
        break;
    case 'J':
        if (p0 == 2) { memset(grid, ' ', sizeof grid); memset(attr, cur_attr, sizeof attr); }
        break;
    case 'm':
        if (nparams == 0) cur_attr = 0;
        for (i = 0; i < nparams; i++) {
            switch (params[i]) {
            case 0: cur_attr = 0; break;
            case 1: cur_attr |= 2; break;
            case 4: cur_attr |= 4; break;
            case 7: cur_attr |= 1; break;
            }
        }
        break;
    }
}

static void em_put(uint8_t b) {
    bytes_out++;
    if (st == 1) { if (b == '[') { st = 2; nparams = 0; params[0] = 0; private_mark = 0; } else st = 0; return; }
    if (st == 2) {
        if (b == '?') { private_mark = 1; return; }
        if (b >= '0' && b <= '9') { if (nparams == 0) nparams = 1; params[nparams - 1] = params[nparams - 1] * 10 + (b - '0'); return; }
        if (b == ';') { if (nparams == 0) nparams = 1; if (nparams < 4) params[nparams++] = 0; return; }
        em_csi((char)b); st = 0; return;
    }
    switch (b) {
    case 0x1B: st = 1; return;
    case '\r': cur_c = 0; return;
    case '\b': if (cur_c > 0) cur_c--; return;
    case '\n':
        if (cur_r == reg_bot) em_scroll(); else if (cur_r < ROWS - 1) cur_r++;
        return;
    }
    if (cur_c >= COLS) {                         /* only reachable with autowrap on */
        cur_c = 0;
        if (cur_r == reg_bot) em_scroll(); else if (cur_r < ROWS - 1) cur_r++;
    }
    grid[cur_r][cur_c] = (char)b;
    attr[cur_r][cur_c] = cur_attr;
    if (cur_c < COLS - 1 || autowrap) cur_c++;   /* autowrap off: pin to last column */
}

#endif /* VT100_EMU_H */
