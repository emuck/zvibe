/* Copyright (c) 2025 Martin R. Raumann */
/* SPDX-License-Identifier: BSD-3-Clause */
/**
 * test_screen.c - Unit tests for the V4 ANSI screen layer
 *
 * Drives zvibe_screen through a minimal VT100 emulator (cursor addressing,
 * SGR, EL/ED, DECSTBM scroll regions, DECAWM) and asserts on the resulting
 * character grid, so split-window behaviour is checked without hardware.
 */

#include "zvibe_screen.h"
#include <stdio.h>
#include <string.h>

#include "vt100_emu.h"

/* ---- Test harness ---- */

static int tests_run, tests_passed;
#define CHECK(cond) do { tests_run++; if (cond) tests_passed++; \
    else printf("FAIL: %s (line %d)\n", #cond, __LINE__); } while (0)

static int row_is(int r, const char *text) {      /* row begins with text, rest blank */
    size_t n = strlen(text), i;
    if (n > COLS) return 0;
    if (memcmp(grid[r], text, n) != 0) return 0;
    for (i = n; i < COLS; i++) if (grid[r][i] != ' ') return 0;
    return 1;
}

static int row_attr_all(int r, int from, int to, uint8_t want) {
    int c;
    for (c = from; c < to; c++) if (attr[r][c] != want) return 0;
    return 1;
}

static zscreen scr;

/* Fresh screen with the lower cursor at the top, for tests that read top-down */
static void fresh(void) {
    em_reset();
    zscreen_init(&scr, em_put, ROWS, COLS);
    scr.clear_to_bottom = 0;
    zscreen_erase_window(&scr, -2);
}

static void fresh_raw(void) {
    em_reset();
    zscreen_init(&scr, em_put, ROWS, COLS);
}

static void test_init(void) {
    puts("== init ==");
    fresh_raw();
    CHECK(autowrap == 0);
    CHECK(cur_r == ROWS - 1 && cur_c == 0);            /* V4: lower cursor starts bottom-left */
    zscreen_puts(&scr, "hello\n");
    zscreen_flush(&scr);
    CHECK(memcmp(grid[ROWS - 2], "hello", 5) == 0);    /* scrolled up from the bottom */
    CHECK(bad_region == 0);
}

static void test_status_bar_survives_scroll(void) {
    int i;
    char buf[32];
    puts("== status bar survives lower-window scrolling ==");
    fresh();
    zscreen_split_window(&scr, 1);
    zscreen_set_window(&scr, 1);
    zscreen_set_text_style(&scr, ZSCREEN_STYLE_REVERSE);
    for (i = 0; i < COLS; i++) zscreen_putc(&scr, ' ');
    zscreen_set_cursor(&scr, 1, 2);
    zscreen_puts(&scr, "West of House");
    zscreen_set_cursor(&scr, 1, 60);
    zscreen_puts(&scr, "Score: 0");
    zscreen_set_text_style(&scr, ZSCREEN_STYLE_ROMAN);
    zscreen_set_window(&scr, 0);
    for (i = 0; i < 60; i++) {
        snprintf(buf, sizeof buf, "line %d\n", i);
        zscreen_puts(&scr, buf);
    }
    zscreen_flush(&scr);
    CHECK(memcmp(&grid[0][1], "West of House", 13) == 0);
    CHECK(memcmp(&grid[0][59], "Score: 0", 8) == 0);
    CHECK(row_attr_all(0, 0, COLS, 1));                 /* whole bar still reverse */
    CHECK(row_attr_all(1, 0, COLS, 0));                 /* lower text is not reverse */
    CHECK(memcmp(grid[ROWS - 2], "line 59", 7) == 0);   /* last line above the blank cursor row */
    CHECK(memcmp(grid[1], "line 38", 7) == 0);          /* oldest visible lower line */
    CHECK(lf_scrolls > 0);
}

static void test_wrap_exact(void) {
    char xs[76];
    int i;
    puts("== wrap breaks at word boundary ==");
    fresh();
    for (i = 0; i < 75; i++) { zscreen_putc(&scr, 'x'); xs[i] = 'x'; }
    xs[75] = 0;
    zscreen_puts(&scr, " yyyyyyyy zz");          /* 'yyyyyyyy' crosses col 80 */
    zscreen_flush(&scr);
    CHECK(row_is(0, xs));                        /* word moved whole to next row */
    CHECK(row_is(1, "yyyyyyyy zz"));
}

static void test_hard_break(void) {
    int i;
    puts("== unbroken run longer than a line ==");
    fresh();
    for (i = 0; i < 100; i++) zscreen_putc(&scr, 'q');
    zscreen_flush(&scr);
    CHECK(grid[0][COLS - 1] == 'q');
    CHECK(grid[1][19] == 'q' && grid[1][20] == ' ');
}

static void test_upper_clip_and_newline(void) {
    int i;
    puts("== upper window clipping ==");
    fresh();
    zscreen_split_window(&scr, 3);
    zscreen_set_window(&scr, 1);
    for (i = 0; i < 100; i++) zscreen_putc(&scr, 'u');      /* must clip, never wrap */
    zscreen_putc(&scr, '\n');
    zscreen_puts(&scr, "row2");
    zscreen_set_window(&scr, 0);
    zscreen_puts(&scr, "hello");
    zscreen_flush(&scr);
    CHECK(grid[0][COLS - 1] == 'u');
    CHECK(memcmp(grid[1], "row2", 4) == 0);
    CHECK(row_is(2, ""));
    CHECK(memcmp(grid[3], "hello", 5) == 0);                /* lower starts below upper window */
}

static void test_split_growth_moves_lower_cursor(void) {
    puts("== growing the upper window over the lower cursor ==");
    fresh();
    zscreen_puts(&scr, "abc\ndef\n");
    zscreen_flush(&scr);
    zscreen_split_window(&scr, 5);
    zscreen_puts(&scr, "Z");
    zscreen_flush(&scr);
    CHECK(grid[5][0] == 'Z');                               /* row 6 (1-based) = first lower row */
}

static void test_erase(void) {
    puts("== erase_window ==");
    fresh();
    zscreen_split_window(&scr, 2);
    zscreen_set_window(&scr, 1);
    zscreen_puts(&scr, "up");
    zscreen_set_window(&scr, 0);
    zscreen_puts(&scr, "low\n");
    zscreen_flush(&scr);
    zscreen_erase_window(&scr, 0);
    CHECK(memcmp(grid[0], "up", 2) == 0);                   /* upper kept */
    CHECK(row_is(2, ""));                                   /* lower cleared */
    zscreen_erase_window(&scr, 1);
    CHECK(row_is(0, ""));
    zscreen_set_window(&scr, 1);
    zscreen_puts(&scr, "again");
    zscreen_set_window(&scr, 0);
    scr.clear_to_bottom = 1;
    zscreen_erase_window(&scr, -1);                         /* unsplit + clear */
    CHECK(row_is(0, ""));
    CHECK(scr.upper_lines == 0);
    zscreen_puts(&scr, "x");
    zscreen_flush(&scr);
    CHECK(grid[ROWS - 1][0] == 'x');                        /* V4: lower cursor at bottom-left */
    scr.clear_to_bottom = 0;
    zscreen_erase_window(&scr, -2);
    zscreen_puts(&scr, "y");
    zscreen_flush(&scr);
    CHECK(grid[0][0] == 'y');
}

static void test_erase_line_and_styles(void) {
    puts("== erase_line and text styles ==");
    fresh();
    zscreen_split_window(&scr, 2);
    zscreen_set_window(&scr, 1);
    zscreen_puts(&scr, "0123456789");
    zscreen_set_cursor(&scr, 1, 5);
    zscreen_set_text_style(&scr, ZSCREEN_STYLE_REVERSE);
    zscreen_erase_line(&scr, 1);
    CHECK(row_is(0, "0123"));
    CHECK(attr[0][5] == 0);                                 /* erase must not paint reverse */
    zscreen_set_text_style(&scr, ZSCREEN_STYLE_BOLD);       /* bits accumulate */
    zscreen_set_cursor(&scr, 2, 1);
    zscreen_puts(&scr, "ab");
    CHECK(attr[1][0] == (1 | 2));
    zscreen_set_text_style(&scr, ZSCREEN_STYLE_ROMAN);
    zscreen_puts(&scr, "c");
    CHECK(attr[1][2] == 0);
    zscreen_set_cursor(&scr, 99, 99);                       /* clamped */
    zscreen_putc(&scr, 'E');
    CHECK(grid[1][COLS - 1] == 'E');
}

static void test_style_in_wrapped_text(void) {
    puts("== style survives buffering ==");
    fresh();
    zscreen_puts(&scr, "plain ");
    zscreen_set_text_style(&scr, ZSCREEN_STYLE_REVERSE);
    zscreen_puts(&scr, "REV");
    zscreen_set_text_style(&scr, ZSCREEN_STYLE_ROMAN);
    zscreen_puts(&scr, " tail\n");
    zscreen_flush(&scr);
    CHECK(attr[0][0] == 0 && attr[0][6] == 1 && attr[0][8] == 1 && attr[0][9] == 0);
}

static void test_unbuffered_and_echo(void) {
    puts("== buffer_mode off and input echo ==");
    fresh();
    zscreen_buffer_mode(&scr, 0);
    zscreen_puts(&scr, "raw");
    CHECK(memcmp(grid[0], "raw", 3) == 0);                  /* no flush needed */
    zscreen_buffer_mode(&scr, 1);
    zscreen_puts(&scr, "\n>");
    zscreen_flush(&scr);
    zscreen_echo(&scr, 'a'); zscreen_echo(&scr, 'b'); zscreen_echo(&scr, 'c');
    zscreen_echo(&scr, 0x7F);
    zscreen_echo(&scr, 'd');
    CHECK(memcmp(grid[1], ">abd", 4) == 0 && grid[1][4] == ' ');
    zscreen_echo(&scr, '\n');
    zscreen_puts(&scr, "ok");
    zscreen_flush(&scr);
    CHECK(memcmp(grid[2], "ok", 2) == 0);
}

static void test_window_switch_preserves_lower_cursor(void) {
    puts("== window switch preserves lower cursor ==");
    fresh();
    zscreen_split_window(&scr, 1);
    zscreen_puts(&scr, "Hello ");
    zscreen_set_window(&scr, 1);                            /* must flush pending wrap text */
    zscreen_puts(&scr, "bar");
    zscreen_set_window(&scr, 0);
    zscreen_puts(&scr, "world");
    zscreen_flush(&scr);
    CHECK(memcmp(grid[1], "Hello world", 11) == 0);
    CHECK(memcmp(grid[0], "bar", 3) == 0);
}

static void test_shutdown(void) {
    puts("== shutdown ==");
    fresh();
    zscreen_split_window(&scr, 2);
    zscreen_set_text_style(&scr, ZSCREEN_STYLE_REVERSE);
    zscreen_puts(&scr, "x");
    zscreen_shutdown(&scr);
    CHECK(autowrap == 1);
    CHECK(reg_top == 0 && reg_bot == ROWS - 1);
    CHECK(cur_attr == 0);
}


static void test_exact_fit_space(void) {
    char a[41], b[40];
    int i;
    puts("== a word that exactly fits stays on its line ==");
    fresh();
    for (i = 0; i < 40; i++) a[i] = 'a';
    a[40] = 0;
    for (i = 0; i < 39; i++) b[i] = 'b';
    b[39] = 0;
    zscreen_puts(&scr, a); zscreen_putc(&scr, ' '); zscreen_puts(&scr, b);   /* 80 columns exactly */
    zscreen_puts(&scr, " c");
    zscreen_flush(&scr);
    CHECK(memcmp(grid[0], a, 40) == 0 && grid[0][40] == ' ' && memcmp(&grid[0][41], b, 39) == 0);
    CHECK(row_is(1, "c"));
}

static void test_flush_midline_keeps_words_whole(void) {
    char xs[80];
    int i;
    puts("== flush then more text does not split a short word ==");
    fresh();
    for (i = 0; i < 75; i++) xs[i] = 'x';
    xs[75] = ' '; xs[76] = 0;
    zscreen_puts(&scr, xs);
    zscreen_flush(&scr);
    zscreen_puts(&scr, "abcdefgh");                  /* only 4 columns remain */
    zscreen_flush(&scr);
    CHECK(grid[0][75] == ' ' && grid[0][76] == ' ');
    CHECK(row_is(1, "abcdefgh"));
    /* but mid-word (no space before the flush) we cannot retract, so it splits */
    fresh();
    for (i = 0; i < 77; i++) zscreen_putc(&scr, 'm');
    zscreen_flush(&scr);
    zscreen_puts(&scr, "nopq");
    zscreen_flush(&scr);
    CHECK(grid[0][79] == 'p');                       /* fills the line... */
    CHECK(grid[1][0] == 'q');                        /* ...and the rest continues below */
}

static void test_upper_reselect_homes(void) {
    puts("== re-selecting the upper window homes its cursor ==");
    fresh();
    zscreen_split_window(&scr, 3);
    zscreen_set_window(&scr, 1);
    zscreen_set_cursor(&scr, 2, 5);
    zscreen_set_window(&scr, 1);
    zscreen_putc(&scr, 'X');
    CHECK(grid[0][0] == 'X');
    CHECK(grid[1][4] == ' ');
}

static void test_split_leaves_one_lower_line(void) {
    puts("== a split may leave the lower window one row ==");
    fresh();
    zscreen_split_window(&scr, ROWS - 1);
    CHECK(scr.upper_lines == ROWS - 1);              /* the requested height, not clamped to ROWS - 2 */
    CHECK(bad_region == 0);
    zscreen_set_window(&scr, 1);
    zscreen_set_cursor(&scr, ROWS - 1, 1);
    zscreen_puts(&scr, "UP");
    zscreen_set_window(&scr, 0);
    zscreen_puts(&scr, "a\nb\nc\n");
    zscreen_flush(&scr);
    CHECK(grid[ROWS - 2][0] == 'U' && grid[ROWS - 2][1] == 'P');   /* upper window untouched by lower output */
    CHECK(grid[ROWS - 1][0] == ' ');                 /* the lower row was cleared by each newline */
    zscreen_puts(&scr, "last");
    zscreen_flush(&scr);
    CHECK(grid[ROWS - 1][0] == 'l');
    CHECK(grid[ROWS - 2][0] == 'U');
    zscreen_split_window(&scr, 200);                 /* larger than the screen still keeps a lower row */
    CHECK(scr.upper_lines == ROWS - 1);
}

static void test_split_leaves_two_lower_lines(void) {
    puts("== a two-row lower window uses a valid scroll region ==");
    fresh();
    zscreen_split_window(&scr, ROWS - 2);
    CHECK(scr.upper_lines == ROWS - 2);
    CHECK(bad_region == 0);
    zscreen_puts(&scr, "a\nb\nc\n");
    zscreen_flush(&scr);
    CHECK(reg_top == ROWS - 2 && reg_bot == ROWS - 1);
}

static void test_echo_edges(void) {
    int i, acc, last_acc = 1;
    puts("== echo at the right edge ==");
    fresh_raw();
    acc = 0;
    for (i = 0; i < 85; i++) { last_acc = zscreen_echo(&scr, (char)('a' + i % 26)); acc += last_acc; }
    CHECK(acc == COLS);                              /* accepted exactly one line's worth */
    CHECK(last_acc == 0);
    CHECK(grid[ROWS - 1][COLS - 1] == (char)('a' + 79 % 26));
    CHECK(zscreen_echo(&scr, 0x7F) == 1);            /* erases column 80, not 79 */
    CHECK(grid[ROWS - 1][COLS - 1] == ' ');
    CHECK(grid[ROWS - 1][COLS - 2] == (char)('a' + 78 % 26));
    CHECK(zscreen_echo(&scr, 'z') == 1);
    CHECK(grid[ROWS - 1][COLS - 1] == 'z');
    zscreen_echo(&scr, '\n');
    CHECK(zscreen_echo(&scr, 0x7F) == 0);            /* nothing to delete at column 1 */
    zscreen_set_window(&scr, 0);
}

static void test_bce_hygiene(void) {
    puts("== reverse video never leaks into blanks (bce emulation) ==");
    fresh();
    zscreen_split_window(&scr, 1);
    zscreen_set_window(&scr, 1);
    zscreen_set_text_style(&scr, ZSCREEN_STYLE_REVERSE);
    zscreen_puts(&scr, "bar");
    zscreen_set_window(&scr, 0);
    zscreen_puts(&scr, "text\n");                    /* style is still reverse for lower text */
    {
        int i;
        for (i = 0; i < 40; i++) zscreen_puts(&scr, "scroll\n");
    }
    zscreen_flush(&scr);
    CHECK(row_attr_all(ROWS - 1, 0, COLS, 0));       /* freshly scrolled-in line is plain */
    zscreen_erase_window(&scr, 0);
    CHECK(row_attr_all(5, 0, COLS, 0));
}

static void test_dirty_init_and_bounds(void) {
    zscreen t;
    puts("== init modes and dimension validation ==");
    em_reset();
    zscreen_init(&t, em_put, 255, 200);
    CHECK(t.rows == 250 && t.cols == ZSCREEN_MAX_COLS);
    zscreen_init(&t, em_put, 0, 0);
    CHECK(t.rows == 3 && t.cols == 1);
    zscreen_erase_window(&t, -1);                    /* must terminate */
    CHECK(1);
}

static void test_get_cursor(void) {
    uint16_t r, c;
    puts("== get_cursor is relative to the current window ==");
    fresh();
    zscreen_split_window(&scr, 3);
    zscreen_set_window(&scr, 1);
    zscreen_set_cursor(&scr, 2, 7);
    zscreen_get_cursor(&scr, &r, &c);
    CHECK(r == 2 && c == 7);                                /* upper window: its own coordinates */
    zscreen_set_window(&scr, 0);
    zscreen_get_cursor(&scr, &r, &c);
    CHECK(r == 1 && c == 1);                                /* top line of the lower window (terminal row 4) */
    zscreen_puts(&scr, "abc\ndef");
    zscreen_get_cursor(&scr, &r, &c);                       /* pending text counts: flushed before reporting */
    CHECK(r == 2 && c == 4);
    zscreen_puts(&scr, "\n");
    zscreen_get_cursor(&scr, &r, &c);
    CHECK(r == 3 && c == 1);
}

/* A terminal left in odd modes by an earlier program: origin mode, insert mode, the
 * line-drawing character set, a scroll region, bold, auto-wrap on. zscreen_init must
 * put all of that right (the emulator models each of those modes). */
static void make_terminal_dirty(void) {
    static const char dirty[] = "\033[5;20r\033[?6h\033[4h\033(0\033[1;7m\033[?7h\033[10;10H";
    const char *p;
    em_reset();
    for (p = dirty; *p; p++) em_put((uint8_t)*p);
    CHECK(origin_mode && insert_mode && graphics && autowrap && reg_top == 4);   /* the setup took effect */
}

static void test_dirty_terminal(void) {
    puts("== init puts a terminal left in odd modes right ==");
    make_terminal_dirty();
    zscreen_init(&scr, em_put, ROWS, COLS);
    scr.clear_to_bottom = 0;
    zscreen_erase_window(&scr, -2);
    CHECK(!origin_mode && !insert_mode && !graphics && !autowrap);
    CHECK(reg_top == 0 && reg_bot == ROWS - 1);
    zscreen_split_window(&scr, 3);
    zscreen_set_window(&scr, 1);
    zscreen_set_cursor(&scr, 1, 1);
    zscreen_puts(&scr, "ABC");
    zscreen_set_cursor(&scr, 1, 2);
    zscreen_puts(&scr, "x");                             /* overwrites; nothing is shifted right */
    zscreen_set_window(&scr, 0);
    zscreen_puts(&scr, "plain `abc` text\n");
    zscreen_flush(&scr);
    CHECK(row_is(0, "AxC"));                             /* absolute addressing, not region-relative */
    CHECK(row_attr_all(0, 0, 3, 0));                     /* not bold, not reverse */
    CHECK(row_is(3, "plain `abc` text"));                /* real letters, not line-drawing glyphs */
    CHECK(bad_region == 0);
}

/* A restart (erase_window -1 with style and buffering reset by the interpreter) puts the screen
 * layer back to buffered, unstyled text on an unsplit screen. */
static void test_restart_resets_screen_state(void) {
    puts("== restart: buffering, style and split are reset ==");
    fresh();
    zscreen_split_window(&scr, 4);
    zscreen_buffer_mode(&scr, 0);
    zscreen_set_text_style(&scr, ZSCREEN_STYLE_REVERSE | ZSCREEN_STYLE_BOLD);
    zscreen_puts(&scr, "styled");
    zscreen_flush(&scr);
    /* what the interpreter does on restart */
    zscreen_buffer_mode(&scr, 1);
    zscreen_set_text_style(&scr, 0);
    zscreen_erase_window(&scr, -1);
    CHECK(scr.upper_lines == 0 && scr.cur_window == 0);
    CHECK(scr.buffered == 1 && scr.style == 0);
    CHECK(reg_top == 0 && reg_bot == ROWS - 1);
    scr.clear_to_bottom = 0;
    zscreen_erase_window(&scr, -2);
    zscreen_puts(&scr, "after restart: a line long enough to need a word wrap at the right edge of this eighty column screen\n");
    zscreen_flush(&scr);
    CHECK(row_attr_all(0, 0, COLS, 0));                  /* not reverse or bold */
    CHECK(grid[0][COLS - 1] == ' ' || grid[1][0] != ' ');   /* wrapped at a word, buffered */
    CHECK(memcmp(grid[0], "after restart: a line long", 25) == 0);
}

/* erase_window -2 clears both windows, keeps the split, and sets both cursors, whichever window is selected */
static void test_erase_all_keeps_split(void) {
    uint16_t r, c;
    puts("== erase_window -2 with the upper window selected ==");
    fresh();
    zscreen_split_window(&scr, 3);
    zscreen_set_window(&scr, 0);
    zscreen_puts(&scr, "lower text\n");
    zscreen_set_window(&scr, 1);
    zscreen_set_cursor(&scr, 2, 5);
    zscreen_puts(&scr, "upper");
    zscreen_erase_window(&scr, -2);
    CHECK(scr.upper_lines == 3 && scr.cur_window == 1);   /* still split, upper still selected */
    CHECK(row_is(1, "") && row_is(3, ""));
    zscreen_get_cursor(&scr, &r, &c);
    CHECK(r == 1 && c == 1);                              /* the upper cursor is home */
    zscreen_puts(&scr, "U");
    CHECK(grid[0][0] == 'U');
    zscreen_set_window(&scr, 0);
    zscreen_get_cursor(&scr, &r, &c);
    CHECK(r == 1 && c == 1);                              /* the lower cursor is at the top of its window */
    zscreen_puts(&scr, "L");
    zscreen_flush(&scr);
    CHECK(grid[3][0] == 'L');                             /* terminal row 4, under the 3-line upper window */
    CHECK(bad_region == 0);
}

int main(void) {
    test_init();
    test_status_bar_survives_scroll();
    test_wrap_exact();
    test_hard_break();
    test_upper_clip_and_newline();
    test_split_growth_moves_lower_cursor();
    test_erase();
    test_erase_line_and_styles();
    test_style_in_wrapped_text();
    test_unbuffered_and_echo();
    test_window_switch_preserves_lower_cursor();
    test_exact_fit_space();
    test_flush_midline_keeps_words_whole();
    test_upper_reselect_homes();
    test_split_leaves_two_lower_lines();
    test_split_leaves_one_lower_line();
    test_echo_edges();
    test_bce_hygiene();
    test_dirty_init_and_bounds();
    test_get_cursor();
    test_dirty_terminal();
    test_restart_resets_screen_state();
    test_erase_all_keeps_split();
    test_shutdown();
    printf("\n%d/%d checks passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
