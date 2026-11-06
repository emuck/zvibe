/* Copyright (c) 2025 Martin R. Raumann */
/* SPDX-License-Identifier: BSD-3-Clause */
/**
 * @file zvibe_screen_ops.h
 * @brief What a platform must provide for the Z-machine V4 screen model
 *
 * A V4 story draws its own status line in an upper window, positions the cursor,
 * changes text style and erases windows. The interpreter turns those opcodes into
 * calls on this table; the platform draws. The ready-made ANSI/VT100
 * implementation is zscreen_ops in src/screen/zvibe_screen.c.
 *
 * Text itself still goes through the output callback given to zvibe_create(), in
 * order with these calls, so a platform that draws needs only forward the text to
 * its screen layer.
 *
 * Every member is optional: a NULL function is ignored. Used only when the
 * library is built with ZVIBE_ENABLE_V4=1; register with zvibe_set_screen().
 */

#ifndef ZVIBE_SCREEN_OPS_H
#define ZVIBE_SCREEN_OPS_H

#include <stdint.h>
#include <stddef.h>

/** Text styles the platform can show, for the header's capability bits */
#define ZVIBE_SCREEN_CAP_BOLD    0x01
#define ZVIBE_SCREEN_CAP_ITALIC  0x02
#define ZVIBE_SCREEN_CAP_FIXED   0x04   /**< fixed-pitch text is available (any terminal) */

typedef struct zvibeScreenOps {
    /** split_window: make the upper window `lines` lines tall (0 = unsplit) */
    void (*split_window)(void *user, uint16_t lines);
    /** set_window: 0 selects the lower window, 1 the upper */
    void (*set_window)(void *user, uint16_t window);
    /** erase_window: 0 lower, 1 upper, -1 unsplit and clear all, -2 clear all keeping the split */
    void (*erase_window)(void *user, int16_t window);
    /** erase_line: 1 erases from the cursor to the end of the line; other values are ignored */
    void (*erase_line)(void *user, uint16_t value);
    /** set_cursor: 1-based row and column in the upper window */
    void (*set_cursor)(void *user, uint16_t row, uint16_t col);
    /** get_cursor: the cursor position in the current window, 1-based, relative to that window */
    void (*get_cursor)(void *user, uint16_t *row, uint16_t *col);
    /** set_text_style: 0 roman, otherwise an OR of 1 reverse, 2 bold, 4 italic, 8 fixed */
    void (*set_text_style)(void *user, uint16_t style);
    /** buffer_mode: 1 word-wrap the lower window, 0 do not */
    void (*buffer_mode)(void *user, uint16_t on);
    /** screen size, written to the story header (rows 0x20, columns 0x21) */
    void (*get_size)(void *user, uint8_t *rows, uint8_t *cols);
    /** ZVIBE_SCREEN_CAP_* bits for the styles this screen can show */
    uint8_t capabilities;
} zvibeScreenOps;

#endif /* ZVIBE_SCREEN_OPS_H */
