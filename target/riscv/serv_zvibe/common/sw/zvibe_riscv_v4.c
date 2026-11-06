/* Copyright (c) 2025 Martin R. Raumann */
/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * zvibe_riscv_v4.c - version 4 story player for the RISC-V (SERV) firmware.
 * See zvibe_riscv_v4.h. Follows the SAM E51 V4 player (target/same51/src/zvibe_same51.c,
 * terminal_io.c); the board supplies only rv_put_byte(), rv_rx_get() and rv_idle().
 */

#include "zvibe_riscv_v4.h"

#if ZVIBE_ENABLE_V4

#include <string.h>
#include "zvibe_api.h"
#include "zvibe_screen.h"
#include "flash_save.h"
#include "save_delta.h"

/* ---- terminal input ------------------------------------------------------- */

static uint8_t term_rows = 24, term_cols = 80;

static int rx_wait(void) {
    int c;
    while ((c = rv_rx_get()) < 0) rv_idle();
    return c;
}

/* A byte that arrives within `ms` milliseconds (the tail of an escape sequence), or -1 */
static int rx_within(unsigned long ms) {
    unsigned long spin, n = ms * RV_SPIN_PER_MS;
    int c;
    for (spin = 0; spin < n; spin++)
        if ((c = rv_rx_get()) >= 0) return c;
    return -1;
}

/* Park the cursor far off-screen (the terminal clamps it to the corner) and read back the
 * position report ESC [ r ; c R. A terminal that does not answer leaves 24 x 80. */
static void probe_size(void) {
    static const char q[] = "\033[999;999H\033[6n";
    int state = 0, row = 0, col = 0, c;
    const char *p;
    unsigned long idle = 0;
    for (p = q; *p; p++) rv_put_byte((uint8_t)*p);
    while (idle < 500UL * RV_SPIN_PER_MS) {                 /* about half a second without a byte */
        c = rv_rx_get();
        if (c < 0) { idle++; rv_idle(); continue; }
        idle = 0;
        if (state == 0 && c == 0x1B) state = 1;
        else if (state == 1 && c == '[') state = 2;
        else if (state == 2 && c >= '0' && c <= '9') row = row * 10 + (c - '0');
        else if (state == 2 && c == ';') state = 3;
        else if (state == 3 && c >= '0' && c <= '9') col = col * 10 + (c - '0');
        else if (state == 3 && c == 'R') {
            if (row >= 10 && row <= 250 && col >= 56) {
                term_rows = (uint8_t)row;
                term_cols = (uint8_t)(col > 255 ? 255 : col);      /* the screen layer caps it at 80 */
            }
            return;
        }
        else { state = 0; row = col = 0; }
    }
}

/* A key for a line: CR (LF after CR is dropped), BS/DEL, printable; escape sequences are skipped.
 * A lone ESC eats the key after it. */
static int next_key(void) {
    static int state = 0;      /* 0 normal, 1 after ESC, 2 inside CSI/SS3 */
    static int last_cr = 0;
    for (;;) {
        int c = rx_wait();
        if (state == 1) { state = (c == '[' || c == 'O') ? 2 : 0; continue; }
        if (state == 2) { if (c >= 0x40 && c <= 0x7E) state = 0; continue; }
        if (c == 0x1B) { state = 1; continue; }
        if (c == '\n' && last_cr) { last_cr = 0; continue; }
        last_cr = (c == '\r');
        return c == '\n' ? '\r' : c;
    }
}

/* A key for read_char, as ZSCII: Return, Delete, Escape, the arrows, printable ASCII */
static int next_zscii(void) {
    static int last_cr = 0;
    static int pending = -1;       /* a byte read past the end of an Escape, to be delivered next */
    for (;;) {
        int c;
        if (pending >= 0) { c = pending; pending = -1; }
        else c = rx_wait();
        if (c == '\n' && last_cr) { last_cr = 0; continue; }       /* the LF of CR+LF */
        last_cr = (c == '\r');
        if (c == '\r' || c == '\n') return 13;
        if (c == 8 || c == 127) return 8;
        if (c == 27) {
            c = rx_within(4);
            if (c != '[' && c != 'O') {                           /* a lone Escape; the next key is its own */
                pending = c;
                return 27;
            }
            c = rx_within(4);
            if (c == 'A') return 129;
            if (c == 'B') return 130;
            if (c == 'D') return 131;
            if (c == 'C') return 132;
            while (c >= 0 && !(c >= 0x40 && c <= 0x7E)) c = rx_within(4);
            continue;
        }
        if (c >= 32 && c < 127) return c;
    }
}

/* ---- the screen ------------------------------------------------------------- */

static zscreen scr;

static void v4_output(const char *text, size_t length) {
    size_t i;
    for (i = 0; i < length; i++) zscreen_putc(&scr, text[i]);
}

static void v4_puts(const char *s) {
    while (*s) zscreen_putc(&scr, *s++);
}

/* ---- saves -------------------------------------------------------------------
 * A V4 story's full save (dynamic memory plus execution state) is as large as its dynamic
 * memory, so it is packed against the story in flash: only the runs of bytes the game has
 * changed are kept (save_delta.h), typically a few KB. A packed save leaves out every byte
 * that equals the flashed story, including the story's own release, serial and checksum, so
 * the save starts with the identity of the story it was made for, and ends with a CRC-32 of
 * everything before it (flash_save.c's header CRC is not checked on read).
 *
 *   [identity 10 bytes][packed save][CRC-32 of both, little-endian]
 */
#define V4_BLOB_MAX    (ZVIBE_MEMORY_BUFFER_SIZE + 2 * ZVIBE_STACK_WORDS + 64)
#define V4_ID_LEN      10
#define V4_CRC_LEN     4
#define V4_ENVELOPE    (RV_V4_PACKED_MAX + V4_ID_LEN + V4_CRC_LEN)

static uint8_t v4_blob[V4_BLOB_MAX];        /* the core's save data, whole */
static uint8_t v4_packed[V4_ENVELOPE];      /* what goes to (or comes from) flash */

size_t rv_v4_save_buffers_bytes(void) { return sizeof v4_blob + sizeof v4_packed; }

static const uint8_t *v4_story;

/* The story's dynamic memory size: the packing base is the first that many bytes of the story */
static size_t v4_base_len(void) {
    return ((size_t)v4_story[14] << 8) | v4_story[15];
}

/* release 0x02, serial 0x12-0x17, checksum 0x1C */
static void v4_story_id(uint8_t id[V4_ID_LEN]) {
    id[0] = v4_story[2];  id[1] = v4_story[3];
    memcpy(id + 2, v4_story + 0x12, 6);
    id[8] = v4_story[0x1C];  id[9] = v4_story[0x1D];
}

static void put_le32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

static uint32_t get_le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int v4_save(zvibeContext *ctx) {
    size_t size = zvibe_get_save_size(ctx);
    int ok = 0;
    if (!flash_save_has_slots()) {
        v4_puts("\n[Save not available: no save slots]\n");
    } else if (size > 0 && size <= sizeof v4_blob &&
               zvibe_get_save_data(ctx, v4_blob, sizeof v4_blob) == size) {
        size_t packed = delta_pack(v4_packed + V4_ID_LEN, RV_V4_PACKED_MAX, v4_blob, size,
                                   v4_story, v4_base_len());
        if (packed == 0) {
            v4_puts("\n[Save failed: the changes do not fit a save slot]\n");
        } else {
            size_t n = V4_ID_LEN + packed;
            v4_story_id(v4_packed);
            put_le32(v4_packed + n, save_crc32(v4_packed, n));
            ok = flash_save_write(v4_packed, n + V4_CRC_LEN) == SAVE_OK;
            if (!ok) v4_puts("\n[Save failed: flash write error]\n");
        }
    }
    return ok;
}

static int v4_restore(zvibeContext *ctx) {
    int ok = 0;
    int got;
    if (!flash_save_has_slots()) {
        v4_puts("\n[Restore not available: no save slots]\n");
        return 0;
    }
    got = flash_save_read(v4_packed, sizeof v4_packed);
    if (got == SAVE_ERR_NOT_FOUND) {
        v4_puts("\n[No saved game found]\n");
    } else if (got <= V4_ID_LEN + V4_CRC_LEN) {
        v4_puts("\n[Restore failed: the save is damaged]\n");
    } else {
        size_t n = (size_t)got - V4_CRC_LEN;
        uint8_t id[V4_ID_LEN];
        v4_story_id(id);
        if (get_le32(v4_packed + n) != save_crc32(v4_packed, n)) {
            v4_puts("\n[Restore failed: the save is damaged]\n");
        } else if (memcmp(v4_packed, id, V4_ID_LEN) != 0) {
            v4_puts("\n[That save is from a different story or release]\n");
        } else {
            size_t size = delta_unpack(v4_blob, sizeof v4_blob, v4_packed + V4_ID_LEN, n - V4_ID_LEN,
                                       v4_story, v4_base_len());
            ok = size > 0 && zvibe_restore_data(ctx, v4_blob, size) == ZVIBE_OK;
            if (!ok) v4_puts("\n[Restore failed]\n");
        }
    }
    return ok;
}

/* ---- the player -------------------------------------------------------------- */

#define V4_LINE_SIZE 80

/* Read a command line with echo and backspace; the screen layer says what fits on the row,
 * and the story's text buffer bounds it too. */
static void v4_read_line(zvibeContext *ctx, char *buf, size_t size) {
    size_t len = 0;
    size_t limit = zvibe_get_input_limit(ctx);
    if (limit == 0 || limit >= size) limit = size - 1;
    zscreen_flush(&scr);
    for (;;) {
        int k = next_key();
        if (k == '\r') { zscreen_echo(&scr, '\n'); break; }
        if (k == 8 || k == 127) {
            if (len && zscreen_echo(&scr, 8)) len--;
        } else if (k >= 32 && k < 127 && len < limit) {
            if (zscreen_echo(&scr, (char)k)) buf[len++] = (char)k;
        }
    }
    buf[len] = '\0';
}

int rv_v4_is_v4_story(const uint8_t *story, size_t size) {
    return story && size > 0x40 && story[0] == 4;
}

rv_v4_result rv_v4_play(const uint8_t *story, size_t size) {
    char line[V4_LINE_SIZE];
    rv_v4_result out = RV_V4_FINISHED;
    zvibeContext *ctx;

    v4_story = story;

    probe_size();                                      /* the terminal may have been opened or resized since boot */
    zscreen_init(&scr, rv_put_byte, term_rows, term_cols);   /* clears the terminal */
    ctx = zvibe_create(v4_output);
    if (!ctx) { zscreen_shutdown(&scr); return RV_V4_ERROR; }
    zvibe_set_screen(ctx, &zscreen_ops, &scr);
    if (zvibe_load_story_from_memory(ctx, story, size) != ZVIBE_OK) {
        zvibe_destroy(ctx);
        zscreen_shutdown(&scr);
        return RV_V4_ERROR;
    }

    for (;;) {
        zvibeResult result = zvibe_run(ctx);
        zscreen_flush(&scr);
        if (result == ZVIBE_WAIT_FOR_INPUT) {
            v4_read_line(ctx, line, sizeof line);
            if (strcmp(line, "~reset~") == 0) { out = RV_V4_RESET; break; }   /* test scripts */
            if (zvibe_input(ctx, line) != ZVIBE_OK) { out = RV_V4_ERROR; break; }
        } else if (result == ZVIBE_WAIT_FOR_CHAR) {
            zvibe_input_char(ctx, (uint16_t)next_zscii());
        } else if (result == ZVIBE_SAVE_REQUESTED) {
            zvibe_save_completed(ctx, v4_save(ctx));
        } else if (result == ZVIBE_RESTORE_REQUESTED) {
            zvibe_restore_completed(ctx, v4_restore(ctx));
        } else if (result == ZVIBE_RESTART_REQUESTED) {
            zvibe_restart_completed(ctx);
        } else if (result == ZVIBE_GAME_FINISHED) {
            v4_puts("\n[Game finished]\n");
            break;
        } else {
            v4_puts("\n[Game error]\n");
            out = RV_V4_ERROR;
            break;
        }
    }
    zscreen_flush(&scr);
    zvibe_destroy(ctx);
    zscreen_shutdown(&scr);
    return out;
}

#endif /* ZVIBE_ENABLE_V4 */
