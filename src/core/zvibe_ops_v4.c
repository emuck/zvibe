/*
 * Copyright (c) 2025 Martin R. Raumann
 * SPDX-License-Identifier: BSD-3-Clause
 */
/**
 * @file zvibe_ops_v4.c
 * @brief Opcodes that exist only from version 4: screen control, output
 *        streams, single-character input and table scanning
 * @ingroup OpcodesText
 *
 * This whole file is compiled only with ZVIBE_ENABLE_V4=1; a V3-only build
 * contains none of it.
 *
 * Screen opcodes do no drawing themselves: they call the platform's screen
 * operations table (zvibe_screen_ops.h, registered with zvibe_set_screen()).
 * Text still goes out through the output callback, in order with those calls.
 *
 * Output streams (standard section 7): stream 1 is the screen, stream 3 is a
 * table in memory. While stream 3 is selected, text goes only to the table (as
 * ZSCII, a newline stored as 13) and the table's first word counts the
 * characters written.
 */

#include "zvibe.h"

#if ZVIBE_ENABLE_V4

/* -------------------- OUTPUT STREAMS -------------------- */

void z_reset_output_state(void) {
    G->output_screen_off = 0;
    G->stream3_depth = 0;
}

/** Restart: output streams reset, windows unsplit, screen cleared. */
void z_reset_ui_state(void) {
    z_reset_output_state();
    G->char_requested = 0;
    G->current_window = 0;
    G->upper_window_lines = 0;
    Z_SCREEN(buffer_mode, 1);                /* a restarted story starts buffered and unstyled (7.2) */
    Z_SCREEN(set_text_style, 0);
    Z_SCREEN(erase_window, (int16_t)-1);
}

/** Append text to the innermost selected memory table. */
static void z_stream3_write(const char *text, zSize length) {
    const zWord table = G->stream3_table[G->stream3_depth - 1];
    zSize i;

    for (i = 0; i < length; i++) {
        zmem_byte_t hi, lo;
        zWord n;
        zmem_read_byte(&G->memory_state, table, &hi);
        zmem_read_byte(&G->memory_state, (zmem_addr_t)(table + 1), &lo);
        n = (zWord)(((zWord)hi << 8) | lo);
        zmem_write_byte(&G->memory_state, (zmem_addr_t)(table + 2 + n),
                        (zmem_byte_t)(text[i] == '\n' ? 13 : text[i]));
        n++;
        zmem_write_byte(&G->memory_state, table, (zmem_byte_t)(n >> 8));
        zmem_write_byte(&G->memory_state, (zmem_addr_t)(table + 1), (zmem_byte_t)n);
    }
}

/** One ZSCII value for the memory table, unconverted: extended characters keep their codes (3.8). */
void z_stream3_zscii(zByte code) {
    const char c = (char)code;
    z_stream3_write(&c, 1);
}

/** Send game text to whichever stream is selected. */
void z_out(const char *text, zSize length) {
    if (G->stream3_depth) {                  /* the memory table receives the text, and nothing else does */
        z_stream3_write(text, length);
        return;
    }
    if (G->output_screen_off)                /* output stream 1 deselected */
        return;
    if (G->writestr)
        G->writestr(text, length);
}

/**
 * Select or deselect an output stream (opcode: output_stream)
 *
 * 1/-1 screen on/off; 3 selects a memory table (operand 2), -3 deselects it.
 * Stream 2 (transcript) and stream 4 are not supported and are ignored.
 */
void op_output_stream(void) {
    const zSWord stream = (zSWord)G->operands[0];

    switch (stream) {
    case 1:  G->output_screen_off = 0; break;
    case -1: G->output_screen_off = 1; break;
    case 3: {
        const zWord table = G->operand_count > 1 ? G->operands[1] : 0;
        if (G->stream3_depth >= ZVIBE_STREAM3_DEPTH)
            G->die("Output stream 3 selected more than %d times", ZVIBE_STREAM3_DEPTH);
        G->stream3_table[G->stream3_depth++] = table;
        zmem_write_byte(&G->memory_state, table, 0);          /* no characters yet */
        zmem_write_byte(&G->memory_state, (zmem_addr_t)(table + 1), 0);
        break;
    }
    case -3:
        if (G->stream3_depth)
            G->stream3_depth--;
        break;
    default:
        break;                               /* transcript, command-file and other streams: not supported */
    }
}

/** Select the input stream (opcode: input_stream). Only the keyboard is supported. */
void op_input_stream(void) {
}

/** Play a sound effect (opcode: sound_effect). There is no sound; ignored. */
void op_sound_effect(void) {
}

/* -------------------- SCREEN OPERATIONS -------------------- */

void op_erase_window(void) {
    Z_SCREEN(erase_window, (int16_t)G->operands[0]);
}

void op_erase_line(void) {
    Z_SCREEN(erase_line, G->operands[0]);
}

void op_set_cursor(void) {
    Z_SCREEN(set_cursor, G->operands[0], G->operands[1]);
}

/** Store the cursor row and column as two words at the given table (opcode: get_cursor) */
void op_get_cursor(void) {
    uint16_t row = 1, col = 1;
    const zWord table = G->operands[0];
    zSize i;
    zmem_byte_t out[4];

    if (G->screen && G->screen->get_cursor)
        G->screen->get_cursor(G->screen_user, &row, &col);
    out[0] = (zmem_byte_t)(row >> 8); out[1] = (zmem_byte_t)row;
    out[2] = (zmem_byte_t)(col >> 8); out[3] = (zmem_byte_t)col;
    for (i = 0; i < 4; i++)                  /* each byte is checked: the table may end at dynamic memory */
        if (zmem_write_byte(&G->memory_state, (zmem_addr_t)(table + i), out[i]) != ZMEM_SUCCESS)
            return;
}

void op_set_text_style(void) {
    Z_SCREEN(set_text_style, G->operands[0]);
}

void op_buffer_mode(void) {
    Z_SCREEN(buffer_mode, G->operands[0]);
}

/* -------------------- SINGLE-CHARACTER INPUT -------------------- */

/**
 * Read one character from the keyboard (opcode: read_char)
 *
 * The first operand is always 1 (the keyboard). The optional time and routine
 * operands (timed input) are ignored: the header does not advertise timers.
 * Like read, this hands control back to the API layer; zvibe_input_char()
 * delivers the character and the interpreter stores it.
 */
void op_read_char(void) {
    G->char_requested = 1;
    G->saved_logical_pc = G->logical_pc;     /* the store byte, read when the character arrives */
}

/** The character has arrived: resume at the store byte and store it. */
void z_process_char(zWord zscii) {
    zByte *store;

    G->char_requested = 0;
    G->logical_pc = G->saved_logical_pc;
    store = z_var_addr(z_read_pc_byte(), 1);
    Z_WRITE16(store, zscii);
}

/* -------------------- TABLE SCANNING -------------------- */

/**
 * Look for a value in a table (opcode: scan_table x table len [form] -> result, branch)
 *
 * The table holds `len` entries. `form` (default 0x82) has bit 7 set for word
 * entries, clear for byte entries; its low 7 bits are each entry's length in
 * bytes, and only the first byte or word of an entry is compared. The result is
 * the address of the matching entry, or 0, and the instruction branches if found.
 */
void op_scan_table(void) {
    const zWord x = G->operands[0];
    const zWord table = G->operands[1];
    const zWord len = G->operands[2];
    const zByte form = (G->operand_count > 3) ? (zByte)G->operands[3] : 0x82;
    const zWord entry_len = (zWord)(form & 0x7F);
    const int words = (form & 0x80) != 0;
    zByte *store = z_var_addr(z_read_pc_byte(), 1);
    zWord found = 0;
    int matched = 0;
    zWord i;

    for (i = 0; i < len; i++) {
        const zWord addr = (zWord)(table + i * entry_len);
        zmem_byte_t b0, b1;
        zWord value;
        if (zmem_read_byte(&G->memory_state, addr, &b0) != ZMEM_SUCCESS)
            break;
        if (words) {
            if (zmem_read_byte(&G->memory_state, (zmem_addr_t)(addr + 1), &b1) != ZMEM_SUCCESS)
                break;
            value = (zWord)(((zWord)b0 << 8) | b1);
        } else {
            value = b0;                              /* never equal to x when x > 255 */
        }
        if (value == x) { found = addr; matched = 1; break; }
    }

    Z_WRITE16(store, found);
    z_do_branch(matched);                    /* a match at address 0 is still a match */
}

#endif /* ZVIBE_ENABLE_V4 */
