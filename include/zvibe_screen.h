/* Copyright (c) 2025 Martin R. Raumann */
/* SPDX-License-Identifier: BSD-3-Clause */
/**
 * @file zvibe_screen.h
 * @brief ANSI/VT100 screen layer for Z-machine V4 split-window output
 *
 * Maps the V4 screen model (fixed-width upper window, word-wrapped lower
 * window, cursor addressing, text styles) onto a serial terminal. The
 * terminal retains the screen contents, so no frame buffer is kept here; the
 * only RAM cost is one wrap line (ZSCREEN_MAX_COLS bytes plus a parallel
 * style array) and a handful of cursor fields.
 *
 * Terminal requirements: VT100 with DECSTBM scroll regions, SGR 0/1/4/7, and
 * DECAWM (auto-wrap) switching. Any xterm-compatible emulator qualifies.
 *
 * The layer is platform-agnostic: all output goes through one byte callback.
 */

#ifndef ZVIBE_SCREEN_H
#define ZVIBE_SCREEN_H

#include <stdint.h>

#ifndef ZSCREEN_MAX_COLS
#define ZSCREEN_MAX_COLS 80
#endif

/** Z-machine text style bits (set_text_style operand, spec 8.7.1) */
#define ZSCREEN_STYLE_ROMAN   0
#define ZSCREEN_STYLE_REVERSE 1
#define ZSCREEN_STYLE_BOLD    2
#define ZSCREEN_STYLE_ITALIC  4
#define ZSCREEN_STYLE_FIXED   8

/** Byte output callback (blocking is fine) */
typedef void (*zscreen_put_fn)(uint8_t ch);

/**
 * @brief Screen state. Treat as opaque; fields are public only so the
 *        caller can allocate it statically.
 */
typedef struct {
    zscreen_put_fn put;
    uint8_t rows;             /**< Terminal height in lines */
    uint8_t cols;             /**< Terminal width in columns (<= ZSCREEN_MAX_COLS) */
    uint8_t upper_lines;      /**< Height of upper window (0 = not split) */
    uint8_t cur_window;       /**< 0 = lower, 1 = upper */
    uint8_t buffered;         /**< Lower window word-wrap on/off (buffer_mode) */
    uint8_t style;            /**< Requested style bits */
    uint8_t hw_style;         /**< Style last sent to the terminal */
    uint8_t clear_to_bottom;  /**< After erasing the lower window, park the cursor
                                   at its bottom-left (V4 convention, spec 8.7.3) */
    uint8_t lower_row;        /**< Lower window cursor, 1-based terminal coords */
    uint8_t lower_col;
    uint8_t upper_row;        /**< Upper window cursor, 1-based */
    uint8_t upper_col;
    uint8_t line_len;         /**< Pending (unflushed) characters in wrap buffer */
    uint8_t at_boundary;      /**< Last character on the terminal line was a space (or line is empty):
                                   a pending word may be moved whole to the next line */
    char    line[ZSCREEN_MAX_COLS];
    uint8_t line_style[ZSCREEN_MAX_COLS];
} zscreen;

/**
 * Reset the terminal and initialise state. Clears the screen.
 * rows is clamped to 3..250 and cols to 1..ZSCREEN_MAX_COLS. The lower window
 * cursor starts at the bottom-left (V4 convention), so output scrolls up.
 */
void zscreen_init(zscreen *s, zscreen_put_fn put, uint8_t rows, uint8_t cols);

/** Restore terminal defaults (attributes, scroll region, auto-wrap). */
void zscreen_shutdown(zscreen *s);

/* ---- Opcode-level operations ---- */

/** The lower window always keeps at least 2 lines (lines is clamped to rows-2) */
void zscreen_split_window(zscreen *s, uint8_t lines);
void zscreen_set_window(zscreen *s, uint8_t window);
/** window: 0 lower, 1 upper, -1 unsplit + clear all, -2 clear all keep split */
void zscreen_erase_window(zscreen *s, int16_t window);
/** value 1 erases from the cursor to end of line; other values are ignored */
void zscreen_erase_line(zscreen *s, uint16_t value);
/** Upper window only; coordinates are 1-based and clamped to the window */
void zscreen_set_cursor(zscreen *s, uint16_t row, uint16_t col);
/** 0 roman, otherwise OR of ZSCREEN_STYLE_* bits */
void zscreen_set_text_style(zscreen *s, uint16_t style);
/** Lower window only: 1 = word-wrap, 0 = raw */
void zscreen_buffer_mode(zscreen *s, uint16_t on);

/* ---- Text output ---- */

/** Print one ASCII character to the current window ('\n' ends the line). */
void zscreen_putc(zscreen *s, char c);
void zscreen_puts(zscreen *s, const char *str);
/** Push any pending wrapped text to the terminal (call before blocking). */
void zscreen_flush(zscreen *s);

/* ---- Line input support ---- */

/**
 * Echo one typed character in the lower window, keeping the cursor
 * tracking in sync. Handles backspace (0x08/0x7F) and newline.
 *
 * Input does not wrap: typing stops at the right edge.
 * @return 1 if the character was accepted (echoed, or a valid edit),
 *         0 if rejected (edge of line, nothing to delete, wrong window,
 *         non-printable). Callers must only store accepted characters.
 */
int zscreen_echo(zscreen *s, char c);

#endif /* ZVIBE_SCREEN_H */
