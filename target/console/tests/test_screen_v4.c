/* Copyright (c) 2025 Martin R. Raumann */
/* SPDX-License-Identifier: BSD-3-Clause */
/**
 * test_screen_v4.c - A real V4 story, the real interpreter, the real ANSI screen
 *                    layer, and a VT100 emulator
 *
 * data/v4screen.z4 (source: data/v4screen.inf) splits the screen, draws a reverse
 * video status bar, writes bold and positioned text in the upper window, scrolls
 * 30+ lines in the lower window with word wrapping, captures text with output
 * stream 3, reads a key and a line, queries the cursor, scans a table and updates
 * the status bar. This test plays the part of the frontend (echoing typed input,
 * feeding the key and the line) and checks the final screen cell by cell.
 *
 * The expected screen is written out here from the story's source and the rules
 * of the Z-machine screen model, not copied from the interpreter's output; the
 * same story gives the same text under frotz.
 *
 * Only built with ZVIBE_ENABLE_V4=1.
 */

#include <stdio.h>
#include <string.h>
#include "zvibe_api.h"
#include "zvibe_screen.h"
#include "vt100_emu.h"

#ifndef EXPECT_V4
#error "build with -DEXPECT_V4=1"
#endif
#if !ZVIBE_ENABLE_V4
#error "this test needs ZVIBE_ENABLE_V4=1"
#endif

static int tests_run, tests_passed;
#define CHECK(cond) do { tests_run++; if (cond) tests_passed++; \
    else printf("FAIL: %s (line %d)\n", #cond, __LINE__); } while (0)

static zscreen scr;

static void out(const char *text, size_t len) {
    size_t i;
    for (i = 0; i < len; i++) zscreen_putc(&scr, text[i]);
}

/* A row of the emulated screen as text with trailing blanks removed */
static void row_text(int r, char *buf) {
    int n = COLS;
    memcpy(buf, grid[r], COLS);
    while (n > 0 && buf[n - 1] == ' ') n--;
    buf[n] = 0;
}

static int row_equals(int r, const char *want) {
    char buf[COLS + 1];
    row_text(r, buf);
    if (strcmp(buf, want) != 0) printf("  row %d is '%s', wanted '%s'\n", r + 1, buf, want);
    return strcmp(buf, want) == 0;
}

int main(void) {
    zvibeContext *ctx;
    zvibeResult r;
    int steps = 0, asked_key = 0, asked_line = 0;
    char want[COLS + 1];
    int i;

    setvbuf(stdout, NULL, _IONBF, 0);
    puts("v4 screen: real story, real ANSI layer, VT100 emulator");

    em_reset();
    zscreen_init(&scr, em_put, ROWS, COLS);

    ctx = zvibe_create(out);
    CHECK(ctx != NULL);
    CHECK(zvibe_set_screen(ctx, &zscreen_ops, &scr) == ZVIBE_OK);
    CHECK(zvibe_load_story(ctx, "data/v4screen.z4") == ZVIBE_OK);

    for (;;) {
        r = zvibe_run(ctx);
        if (r == ZVIBE_WAIT_FOR_CHAR) {
            asked_key++;
            CHECK(zvibe_input_char(ctx, 'q') == ZVIBE_OK);          /* the player presses q */
        } else if (r == ZVIBE_WAIT_FOR_INPUT) {
            const char *line = "hello world";
            asked_line++;
            for (i = 0; line[i]; i++) CHECK(zscreen_echo(&scr, line[i]) == 1);   /* the frontend echoes typing */
            zscreen_echo(&scr, '\n');
            CHECK(zvibe_input(ctx, line) == ZVIBE_OK);
        } else if (r == ZVIBE_GAME_FINISHED) {
            break;
        } else {
            printf("unexpected result %d\n", (int)r);
            break;
        }
        if (++steps > 20) { puts("runaway"); break; }
    }
    zscreen_flush(&scr);
    CHECK(r == ZVIBE_GAME_FINISHED);
    CHECK(asked_key == 1 && asked_line == 1);

    /* ---- the upper window: rows 1-3 ---- */
    /* Row 1: a reverse-video bar across columns 1-79 with "ZVIBE V4" from column 2 and the final score at 60 */
    for (i = 0; i < 79; i++) CHECK(attr[0][i] == 1);               /* reverse everywhere in the bar... */
    CHECK(attr[0][79] == 0 && grid[0][79] == ' ');                 /* ...but not in column 80 */
    memset(want, ' ', COLS); want[COLS] = 0;
    memcpy(want + 1, "ZVIBE V4", 8);
    memcpy(want + 59, "Score: 8", 8);
    want[79] = 0;
    {
        char buf[COLS + 1]; int n;
        memcpy(buf, grid[0], COLS); n = 79; buf[n] = 0;
        CHECK(memcmp(buf, want, 79) == 0);
    }
    /* Row 2: plain text from column 1. Row 3: "bold" at column 5, in bold, and nothing after column 20 */
    CHECK(row_equals(1, "Row two"));
    CHECK(row_equals(2, "    bold   tail-to-"));                 /* the rest of the row, from column 20, was erased */
    for (i = 4; i < 8; i++) CHECK(attr[2][i] == 2);                /* bold */
    for (i = 0; i < 4; i++) CHECK(attr[2][i] == 0);
    for (i = 0; i < 7; i++) CHECK(attr[1][i] == 0);

    /* ---- the lower window: rows 4-24, scrolled under the upper window ---- */
    {
        /* every line the story printed there, in order (the wrapped paragraph is two lines) */
        static const char *tail[] = {
            "A longer paragraph of text that has to be wrapped at a word boundary by the",
            "interpreter because it is wider than the screen.",
            "size 24x80",
            "len=8 [captured]",
            "press a key: key=113",
            "> hello world",
            "got: hello world",
            "cursor 21,1",
            "scan: found",
            "done"
        };
        char lines[40][100];
        int n = 0, first;
        for (i = 1; i <= 30; i++) snprintf(lines[n++], sizeof lines[0], "Lower line %d", i);
        for (i = 0; i < 10; i++) snprintf(lines[n++], sizeof lines[0], "%s", tail[i]);
        CHECK(n == 40);
        /* 21 lower-window rows; the last is the empty line the cursor sits on after "done" */
        first = n - 20;                                             /* index of the first line still visible */
        for (i = 0; i < 20; i++) CHECK(row_equals(3 + i, lines[first + i]));
        CHECK(row_equals(23, ""));
        CHECK(attr[23][0] == 0);
    }

    /* the lower window never picked up reverse or bold from the upper one */
    for (i = 3; i < ROWS; i++) { int c; for (c = 0; c < COLS; c++) if (attr[i][c] != 0) { CHECK(0); i = ROWS; break; } }

    /* the terminal was driven correctly throughout: a valid scroll region, final state sane */
    CHECK(bad_region == 0);
    CHECK(reg_top == 3 && reg_bot == ROWS - 1);                     /* scroll region = the lower window */
    CHECK(cur_attr == 0);

    zvibe_destroy(ctx);
    printf("\n%d/%d checks passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
