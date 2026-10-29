/* Copyright (c) 2025 Martin R. Raumann */
/* SPDX-License-Identifier: BSD-3-Clause */
/**
 * ansi_dump.c - produce terminal output from the real ANSI screen layer, plus
 *               what the C reference emulator (the console tests' vt100_emu.h)
 *               shows after it, so zterm.js can be checked against it.
 *
 *   ansi_dump story <story.z4> <prefix>   play a V4 story: press 'q' at read_char,
 *                                         type "hello world" at read
 *   ansi_dump fuzz <seed> <prefix>        a few hundred random screen operations
 *
 * Writes <prefix>.ansi (the bytes) and <prefix>.grid (24 text rows, 24 rows of
 * attribute digits, then "cursor row col" 0-based).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "zvibe_api.h"
#include "zvibe_screen.h"
#include "../../console/tests/vt100_emu.h"

#if !ZVIBE_ENABLE_V4
#error "needs ZVIBE_ENABLE_V4=1"
#endif

static zscreen scr;
static FILE *ansi;

static void put(uint8_t b) {
    fputc(b, ansi);
    em_put(b);
}

static void text_out(const char *text, size_t len) {
    size_t i;
    for (i = 0; i < len; i++) zscreen_putc(&scr, text[i]);
}

static unsigned rng_state;
static unsigned rnd(unsigned n) {
    rng_state = rng_state * 1103515245u + 12345u;
    return ((rng_state >> 16) & 0x7fff) % n;
}

static void dump(const char *prefix) {
    char path[512];
    FILE *f;
    int r, c;
    snprintf(path, sizeof path, "%s.grid", prefix);
    f = fopen(path, "w");
    for (r = 0; r < ROWS; r++) { fwrite(grid[r], 1, COLS, f); fputc('\n', f); }
    for (r = 0; r < ROWS; r++) {
        for (c = 0; c < COLS; c++) fputc('0' + attr[r][c], f);
        fputc('\n', f);
    }
    fprintf(f, "cursor %d %d\n", cur_r, cur_c);
    fclose(f);
}

static int play_story(const char *story) {
    zvibeContext *ctx = zvibe_create(text_out);
    int steps = 0, i;
    zvibeResult r;
    zvibe_set_screen(ctx, &zscreen_ops, &scr);
    if (zvibe_load_story(ctx, story) != ZVIBE_OK) return 1;
    for (;;) {
        r = zvibe_run(ctx);
        if (r == ZVIBE_WAIT_FOR_CHAR) {
            zvibe_input_char(ctx, 'q');
        } else if (r == ZVIBE_WAIT_FOR_INPUT) {
            const char *line = "hello world";
            for (i = 0; line[i]; i++) zscreen_echo(&scr, line[i]);
            zscreen_echo(&scr, '\n');
            zvibe_input(ctx, line);
        } else break;
        if (++steps > 20) break;
    }
    zscreen_flush(&scr);
    zvibe_destroy(ctx);
    return 0;
}

static void fuzz(unsigned seed) {
    static const char *words[] = { "alpha", "beta gamma", "delta epsilon zeta eta theta", "x",
        "a considerably longer run of words that is bound to wrap around the line at some point",
        "\n", "\n\n", "tail" };
    int i, n = 300;
    rng_state = seed;
    for (i = 0; i < n; i++) {
        switch (rnd(12)) {
        case 0: case 1: case 2: case 3: {
            const char *w = words[rnd(8)];
            while (*w) zscreen_putc(&scr, *w++);
            break; }
        case 4: zscreen_split_window(&scr, (uint8_t)rnd(ROWS + 3)); break;
        case 5: zscreen_set_window(&scr, (uint8_t)rnd(2)); break;
        case 6: zscreen_set_cursor(&scr, (uint16_t)(1 + rnd(ROWS)), (uint16_t)(1 + rnd(COLS + 2))); break;
        case 7: zscreen_erase_window(&scr, (int16_t)((int)rnd(4) - 2)); break;
        case 8: zscreen_erase_line(&scr, 1); break;
        case 9: zscreen_set_text_style(&scr, (uint16_t)rnd(16)); break;
        case 10: zscreen_buffer_mode(&scr, (uint16_t)rnd(2)); break;
        case 11: { int k, m = (int)rnd(12); for (k = 0; k < m; k++) zscreen_echo(&scr, (char)(rnd(5) == 0 ? 8 : 'a' + rnd(26))); break; }
        }
    }
    zscreen_flush(&scr);
}

int main(int argc, char **argv) {
    char path[512];
    int rc = 0;
    if (argc < 4) { fprintf(stderr, "usage: ansi_dump story|fuzz <story|seed> <prefix>\n"); return 2; }
    snprintf(path, sizeof path, "%s.ansi", argv[3]);
    ansi = fopen(path, "wb");
    em_reset();
    zscreen_init(&scr, put, ROWS, COLS);
    if (strcmp(argv[1], "story") == 0) rc = play_story(argv[2]);
    else fuzz((unsigned)atoi(argv[2]));
    fclose(ansi);
    dump(argv[3]);
    return rc;
}
