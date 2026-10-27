/* Copyright (c) 2025 Martin R. Raumann */
/* SPDX-License-Identifier: BSD-3-Clause */
/**
 * @file zvibe_screen.c
 * @brief ANSI/VT100 screen layer for Z-machine V4 split-window output
 *
 * Layout on the terminal:
 *
 *   rows 1 .. upper_lines          upper window (fixed, never scrolls)
 *   rows upper_lines+1 .. rows     lower window (DECSTBM scroll region)
 *
 * Because the scroll region excludes the upper window, newlines in the lower
 * window scroll only the lower window. Auto-wrap is disabled so that writing
 * the last column never moves the cursor; wrapping is done here.
 */

#include "zvibe_screen.h"

#define ESC 0x1B

static void put_str(zscreen *s, const char *str) {
    while (*str) s->put((uint8_t)*str++);
}

static void put_num(zscreen *s, uint8_t n) {
    if (n >= 100) s->put((uint8_t)('0' + n / 100));
    if (n >= 10)  s->put((uint8_t)('0' + (n / 10) % 10));
    s->put((uint8_t)('0' + n % 10));
}

/* ESC [ row ; col H */
static void term_goto(zscreen *s, uint8_t row, uint8_t col) {
    s->put(ESC); s->put('[');
    put_num(s, row); s->put(';'); put_num(s, col);
    s->put('H');
}

/* Bring the terminal's SGR state in line with the Z style bits */
static void sync_style(zscreen *s, uint8_t want) {
    if (s->hw_style == want) return;
    put_str(s, "\033[0");
    if (want & ZSCREEN_STYLE_BOLD)    put_str(s, ";1");
    if (want & ZSCREEN_STYLE_ITALIC)  put_str(s, ";4");   /* underline: widest support */
    if (want & ZSCREEN_STYLE_REVERSE) put_str(s, ";7");
    s->put('m');
    s->hw_style = want;
}

/* DECSTBM homes the cursor, so callers must reposition afterwards */
static void term_set_region(zscreen *s) {
    if (s->upper_lines == 0 || s->rows - s->upper_lines < 2) {
        put_str(s, "\033[r");           /* a one-row lower window cannot be a DECSTBM region */
    } else {
        s->put(ESC); s->put('[');
        put_num(s, (uint8_t)(s->upper_lines + 1)); s->put(';');
        put_num(s, s->rows); s->put('r');
    }
}

/* Put the terminal cursor where the current window expects it */
static void goto_current(zscreen *s) {
    if (s->cur_window == 1) term_goto(s, s->upper_row, s->upper_col);
    else                    term_goto(s, s->lower_row, s->lower_col);
}

static void lower_newline(zscreen *s) {
    sync_style(s, 0);              /* keep reverse out of scrolled-in blank lines */
    if (s->rows - s->upper_lines < 2) {
        /* One lower row and no scroll region: scrolling it is clearing it, and a
         * real newline would scroll the upper window away. */
        term_goto(s, s->lower_row, 1);
        put_str(s, "\033[K");
        s->lower_col = 1;
        s->at_boundary = 1;
        return;
    }
    s->put('\r'); s->put('\n');    /* at the region's last row this scrolls */
    if (s->lower_row < s->rows) s->lower_row++;
    s->lower_col = 1;
    s->at_boundary = 1;
}

/* Send n pending characters from the wrap buffer */
static void emit_pending(zscreen *s, uint8_t n) {
    uint8_t i;
    for (i = 0; i < n; i++) {
        sync_style(s, s->line_style[i]);
        s->put((uint8_t)s->line[i]);
    }
    if (n) s->at_boundary = (s->line[n - 1] == ' ');
    s->lower_col = (uint8_t)(s->lower_col + n);
}

/* Drop the first n pending characters */
static void shift_pending(zscreen *s, uint8_t n) {
    uint8_t i;
    for (i = n; i < s->line_len; i++) {
        s->line[i - n] = s->line[i];
        s->line_style[i - n] = s->line_style[i];
    }
    s->line_len = (uint8_t)(s->line_len - n);
}

static void flush_lower(zscreen *s) {
    emit_pending(s, s->line_len);
    s->line_len = 0;
}

/* The pending line is full and a non-space character is arriving. Make room. */
static void wrap_lower(zscreen *s) {
    int i;
    int brk = -1;
    for (i = (int)s->line_len - 1; i >= 0; i--) {
        if (s->line[i] == ' ') { brk = i; break; }
    }
    if (brk >= 0) {
        emit_pending(s, (uint8_t)brk);
        shift_pending(s, (uint8_t)(brk + 1));    /* the space is consumed by the break */
        lower_newline(s);
    } else if (s->lower_col > 1 && s->at_boundary) {
        lower_newline(s);                        /* word starts here: move it whole */
    } else {
        flush_lower(s);                          /* overlong word, or already mid-word on the terminal */
        lower_newline(s);
    }
}

static int lower_full(const zscreen *s) {
    return (unsigned)(s->lower_col - 1) + s->line_len >= s->cols;
}

static void lower_putc(zscreen *s, char c) {
    if (c == '\n') {
        flush_lower(s);
        lower_newline(s);
        return;
    }
    if (!s->buffered) {
        if (s->lower_col > s->cols) lower_newline(s);
        sync_style(s, s->style);
        s->put((uint8_t)c);
        s->lower_col++;
        s->at_boundary = (c == ' ');
        return;
    }
    if (c == ' ' && lower_full(s)) {
        /* Line is exactly full: the space is consumed by the break, so the last
         * word keeps its place. */
        flush_lower(s);
        lower_newline(s);
        return;
    }
    while (lower_full(s)) wrap_lower(s);
    s->line[s->line_len] = c;
    s->line_style[s->line_len] = s->style;
    s->line_len++;
}

static void upper_putc(zscreen *s, char c) {
    if (c == '\n') {
        if (s->upper_row < s->upper_lines) s->upper_row++;
        s->upper_col = 1;
        term_goto(s, s->upper_row, s->upper_col);
        return;
    }
    if (s->upper_col > s->cols) return;          /* clip at the right edge */
    sync_style(s, s->style);
    s->put((uint8_t)c);
    s->upper_col++;
}

static void clear_rows(zscreen *s, uint8_t first, uint8_t last) {
    int r;
    sync_style(s, 0);
    for (r = first; r <= last; r++) {
        term_goto(s, (uint8_t)r, 1);
        put_str(s, "\033[2K");
    }
}

void zscreen_init(zscreen *s, zscreen_put_fn put, uint8_t rows, uint8_t cols) {
    s->put = put;
    s->rows = rows < 3 ? 3 : (rows > 250 ? 250 : rows);
    s->cols = cols < 1 ? 1 : (cols > ZSCREEN_MAX_COLS ? ZSCREEN_MAX_COLS : cols);
    s->upper_lines = 0;
    s->cur_window = 0;
    s->buffered = 1;
    s->style = 0;
    s->hw_style = 0;
    s->clear_to_bottom = 1;
    s->lower_row = s->rows;                /* V4: lower window grows up from the bottom */
    s->lower_col = 1;
    s->upper_row = 1;
    s->upper_col = 1;
    s->line_len = 0;
    s->at_boundary = 1;
    /* Establish every mode we depend on: ASCII charset, replace (not insert)
     * mode, absolute cursor origin, auto-wrap off, visible cursor, no attrs. */
    put_str(s, "\033[0m\033(B\033[4l\033[?6l\033[?7l\033[?25h");
    put_str(s, "\033[r\033[2J");
    term_goto(s, s->lower_row, 1);
}

void zscreen_shutdown(zscreen *s) {
    zscreen_flush(s);
    put_str(s, "\033[0m\033[?7h\033[r");
    term_goto(s, s->rows, 1);
    put_str(s, "\r\n");
}

void zscreen_flush(zscreen *s) {
    if (s->cur_window == 0) flush_lower(s);
}

void zscreen_split_window(zscreen *s, uint8_t lines) {
    zscreen_flush(s);
    if (lines > s->rows - 1) lines = (uint8_t)(s->rows - 1);   /* the lower window keeps a row */
    s->upper_lines = lines;
    term_set_region(s);
    if (s->lower_row < lines + 1) {              /* lower cursor swallowed by the new upper window */
        s->lower_row = (uint8_t)(lines + 1);
        s->lower_col = 1;
    }
    if (s->upper_row > lines) s->upper_row = lines ? lines : 1;
    goto_current(s);
}

void zscreen_set_window(zscreen *s, uint8_t window) {
    window = window ? 1 : 0;
    if (window == s->cur_window && window == 0) return;
    zscreen_flush(s);
    s->cur_window = window;
    if (window == 1) {
        s->upper_row = 1;                        /* every selection of upper homes its cursor (8.7.2) */
        s->upper_col = 1;
    }
    goto_current(s);
}

void zscreen_erase_window(zscreen *s, int16_t window) {
    zscreen_flush(s);
    if (window == -1) {
        s->upper_lines = 0;
        s->cur_window = 0;
        term_set_region(s);
        window = -2;
    }
    if (window == -2) {
        clear_rows(s, 1, s->rows);
        s->upper_row = s->upper_col = 1;
        s->lower_row = s->clear_to_bottom ? s->rows : (uint8_t)(s->upper_lines + 1);
        s->lower_col = 1;
    } else if (window == 1) {
        if (s->upper_lines) clear_rows(s, 1, s->upper_lines);
        s->upper_row = s->upper_col = 1;
    } else if (window == 0) {
        if (s->upper_lines < s->rows) clear_rows(s, (uint8_t)(s->upper_lines + 1), s->rows);
        s->lower_row = s->clear_to_bottom ? s->rows : (uint8_t)(s->upper_lines + 1);
        s->lower_col = 1;
    }
    goto_current(s);
}

void zscreen_erase_line(zscreen *s, uint16_t value) {
    if (value != 1) return;
    zscreen_flush(s);
    sync_style(s, 0);
    put_str(s, "\033[K");
}

void zscreen_set_cursor(zscreen *s, uint16_t row, uint16_t col) {
    if (s->cur_window != 1 || s->upper_lines == 0) return;
    if (row < 1) row = 1;
    if (row > s->upper_lines) row = s->upper_lines;
    if (col < 1) col = 1;
    if (col > s->cols) col = s->cols;
    s->upper_row = (uint8_t)row;
    s->upper_col = (uint8_t)col;
    term_goto(s, s->upper_row, s->upper_col);
}

void zscreen_set_text_style(zscreen *s, uint16_t style) {
    if (style == 0) s->style = 0;
    else            s->style = (uint8_t)(s->style | (style & 0x0F));
}

void zscreen_get_cursor(zscreen *s, uint16_t *row, uint16_t *col) {
    zscreen_flush(s);
    if (s->cur_window == 1) { *row = s->upper_row; *col = s->upper_col; }
    else                    { *row = (uint16_t)(s->lower_row - s->upper_lines); *col = s->lower_col; }
}

void zscreen_buffer_mode(zscreen *s, uint16_t on) {
    if (s->cur_window == 0 && !on) flush_lower(s);
    s->buffered = on ? 1 : 0;
}

void zscreen_putc(zscreen *s, char c) {
    if (c != '\n' && (c < 32 || c > 126)) c = '?';
    if (s->cur_window == 1) upper_putc(s, c);
    else                    lower_putc(s, c);
}

void zscreen_puts(zscreen *s, const char *str) {
    while (*str) zscreen_putc(s, *str++);
}

int zscreen_echo(zscreen *s, char c) {
    if (s->cur_window != 0) return 0;
    zscreen_flush(s);
    if (c == '\r' || c == '\n') {
        lower_newline(s);
        return 1;
    }
    if (c == 0x08 || c == 0x7F) {
        if (s->lower_col <= 1) return 0;
        s->lower_col--;
        sync_style(s, 0);
        term_goto(s, s->lower_row, s->lower_col);   /* explicit: \b is unreliable at the pinned last column */
        s->put(' ');
        term_goto(s, s->lower_row, s->lower_col);
        s->at_boundary = 0;
        return 1;
    }
    if (c >= 32 && c <= 126 && s->lower_col <= s->cols) {
        sync_style(s, 0);
        s->put((uint8_t)c);
        s->lower_col++;
        s->at_boundary = (c == ' ');
        return 1;
    }
    return 0;
}

/* ---- The screen as the interpreter's screen-operations table ---- */

static void ops_split_window(void *u, uint16_t lines) { zscreen_split_window((zscreen *)u, lines > 255 ? 255 : (uint8_t)lines); }
static void ops_set_window(void *u, uint16_t w)       { zscreen_set_window((zscreen *)u, (uint8_t)w); }
static void ops_erase_window(void *u, int16_t w)      { zscreen_erase_window((zscreen *)u, w); }
static void ops_erase_line(void *u, uint16_t v)       { zscreen_erase_line((zscreen *)u, v); }
static void ops_set_cursor(void *u, uint16_t r, uint16_t c) { zscreen_set_cursor((zscreen *)u, r, c); }
static void ops_get_cursor(void *u, uint16_t *r, uint16_t *c) { zscreen_get_cursor((zscreen *)u, r, c); }
static void ops_set_text_style(void *u, uint16_t st)  { zscreen_set_text_style((zscreen *)u, st); }
static void ops_buffer_mode(void *u, uint16_t on)     { zscreen_buffer_mode((zscreen *)u, on); }
static void ops_get_size(void *u, uint8_t *rows, uint8_t *cols) {
    *rows = ((zscreen *)u)->rows;
    *cols = ((zscreen *)u)->cols;
}

const zvibeScreenOps zscreen_ops = {
    ops_split_window, ops_set_window, ops_erase_window, ops_erase_line,
    ops_set_cursor, ops_get_cursor, ops_set_text_style, ops_buffer_mode, ops_get_size,
    ZVIBE_SCREEN_CAP_BOLD | ZVIBE_SCREEN_CAP_ITALIC | ZVIBE_SCREEN_CAP_FIXED
};
