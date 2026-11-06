/* Copyright (c) 2025 Martin R. Raumann */
/* SPDX-License-Identifier: BSD-3-Clause */
/**
 * test_restart_screen.c - restart with the real ANSI screen layer
 *
 * A hand-assembled V4 story switches buffering off, picks reverse+bold, splits the
 * screen and prints, then restarts. After zvibe_restart_completed() the screen
 * layer must be back to buffered, unstyled text on an unsplit screen (Standard 7.2
 * and the restart opcode), which is checked on the VT100 emulator's grid and on the
 * escape sequences the layer sent. The story is only run up to the restart request;
 * what follows is plain text written the way the frontend writes it.
 *
 * Only built with ZVIBE_ENABLE_V4=1.
 */

#include <stdio.h>
#include "zvibe_screen.h"
#include "vt100_emu.h"
#include "test_story.h"

#ifndef EXPECT_V4
#error "build with -DEXPECT_V4=1"
#endif

static zscreen scr;

static void out(const char *text, size_t len) {
    size_t i;
    for (i = 0; i < len; i++) zscreen_putc(&scr, text[i]);
}

static int row_starts(int r, const char *text) { return memcmp(grid[r], text, strlen(text)) == 0; }

int main(void) {
    static const uint8_t prog[] = {
        0xEA, 0x7F, 0x04,                         /* split_window 4 */
        0xF2, 0x7F, 0x00,                         /* buffer_mode 0 */
        0xF1, 0x7F, 0x03,                         /* set_text_style 3 (reverse + bold) */
        0xE5, 0x7F, 'x',                          /* print_char 'x' */
        0xB7,                                     /* restart */
    };
    zvibeContext *ctx;
    int i;
    const char *text = "after restart a line long enough that it has to wrap at a word near the right-hand edge of the screen";

    setvbuf(stdout, NULL, _IONBF, 0);
    puts("restart with the real screen layer");

    make_story_static(4, 32 * 1024, 0x3C00);
    memcpy(story + 0x100, prog, sizeof prog);

    em_reset();
    zscreen_init(&scr, em_put, ROWS, COLS);
    ctx = zvibe_create(out);
    zvibe_set_screen(ctx, &zscreen_ops, &scr);
    CHECK(zvibe_load_story_from_memory(ctx, story, 32 * 1024) == ZVIBE_OK);

    CHECK(zvibe_run(ctx) == ZVIBE_RESTART_REQUESTED);
    zscreen_flush(&scr);
    CHECK(scr.upper_lines == 4 && scr.buffered == 0 && scr.style == 3);   /* the story's state, before the restart */

    CHECK(zvibe_restart_completed(ctx) == ZVIBE_OK);
    zscreen_flush(&scr);
    CHECK(scr.upper_lines == 0 && scr.cur_window == 0);
    CHECK(scr.buffered == 1);
    CHECK(scr.style == 0);
    CHECK(reg_top == 0 && reg_bot == ROWS - 1);                          /* the scroll region is gone */
    CHECK(bad_region == 0);

    /* text written now is plain and word-wrapped */
    scr.clear_to_bottom = 0;
    zscreen_erase_window(&scr, -2);
    out(text, strlen(text));
    zscreen_flush(&scr);
    for (i = 0; i < COLS; i++) CHECK(attr[0][i] == 0);
    CHECK(row_starts(0, "after restart a line long enough that it has to wrap at a word near the"));
    CHECK(row_starts(1, "right-hand edge of the screen"));                 /* wrapped at a word: buffering is on */

    zvibe_destroy(ctx);
    printf("\n%d/%d checks passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
