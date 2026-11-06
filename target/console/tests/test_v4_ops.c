/* Copyright (c) 2025 Martin R. Raumann */
/* SPDX-License-Identifier: BSD-3-Clause */
/**
 * test_v4_ops.c - The V4 screen opcodes, output streams, read_char and scan_table
 *
 * Programs are assembled by hand from the Z-machine standard and run through the
 * public API. The screen is a FAKE that records every call, in one log together
 * with the text the story prints, so both the exact arguments and the ORDER of
 * screen calls relative to text are checked. Expected values come from the
 * standard, not from the interpreter.
 *
 * Only built with ZVIBE_ENABLE_V4=1 (the opcodes do not exist otherwise).
 *
 * Memory map of the fixtures (dynamic memory):
 *   0x0100 program   0x0200 globals (g10 = 0x200, g11 = 0x202)
 *   0x0300 get_cursor table   0x0400 / 0x0420 output-stream tables   0x0500 scan table
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>
#include "test_story.h"

#ifndef EXPECT_V4
#error "build with -DEXPECT_V4=1"
#endif
#if !ZVIBE_ENABLE_V4
#error "this test needs ZVIBE_ENABLE_V4=1"
#endif

#define STATIC_BASE 0x3C00u
#define STORY_SIZE  (32 * 1024)

static zvibeContext *ctx;
static uint8_t *ram;

static void put(unsigned at, const uint8_t *bytes, unsigned n) { memcpy(story + at, bytes, n); }
#define PUT(at, ...) do { static const uint8_t b_[] = { __VA_ARGS__ }; put((at), b_, sizeof b_); } while (0)

static unsigned global10(void) { return ((unsigned)ram[0x200] << 8) | ram[0x201]; }
static unsigned global11(void) { return ((unsigned)ram[0x202] << 8) | ram[0x203]; }

/* ---- the recording fake screen ---- */

static char log_buf[1024];
static void log_add(const char *fmt, ...) {
    size_t n = strlen(log_buf);
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(log_buf + n, sizeof log_buf - n, fmt, ap);
    va_end(ap);
}
static void log_clear(void) { log_buf[0] = 0; }

static void out(const char *text, size_t len) {                     /* the story's text output */
    size_t i;
    log_add("T:");
    for (i = 0; i < len; i++) log_add("%c", text[i] == '\n' ? '/' : text[i]);
    log_add(";");
}
static void f_split(void *u, uint16_t n)            { (void)u; log_add("split(%u);", n); }
static void f_set_window(void *u, uint16_t w)       { (void)u; log_add("window(%u);", w); }
static void f_erase_window(void *u, int16_t w)      { (void)u; log_add("erase_window(%d);", w); }
static void f_erase_line(void *u, uint16_t v)       { (void)u; log_add("erase_line(%u);", v); }
static void f_set_cursor(void *u, uint16_t r, uint16_t c) { (void)u; log_add("cursor(%u,%u);", r, c); }
static void f_get_cursor(void *u, uint16_t *r, uint16_t *c) { (void)u; *r = 5; *c = 9; log_add("get_cursor;"); }
static void f_style(void *u, uint16_t s)            { (void)u; log_add("style(%u);", s); }
static void f_buffer(void *u, uint16_t on)          { (void)u; log_add("buffer(%u);", on); }
static unsigned fake_rows = 30, fake_cols = 100;
static void f_size(void *u, uint8_t *r, uint8_t *c) { (void)u; *r = (uint8_t)fake_rows; *c = (uint8_t)fake_cols; }

static const zvibeScreenOps fake = {
    f_split, f_set_window, f_erase_window, f_erase_line, f_set_cursor, f_get_cursor,
    f_style, f_buffer, f_size, ZVIBE_SCREEN_CAP_BOLD | ZVIBE_SCREEN_CAP_FIXED
};

/* ---- harness ---- */

static void start(const zvibeScreenOps *ops) {
    ctx = zvibe_create(out);
    if (ops) zvibe_set_screen(ctx, ops, NULL);
    if (zvibe_load_story_from_memory(ctx, story, STORY_SIZE) != ZVIBE_OK) { puts("LOAD FAILED"); exit(2); }
    ram = G->memory_state.ram_buffer;
    log_clear();
}
static void end(void) { if (ctx) zvibe_destroy(ctx); ctx = NULL; }
/* A fresh story image; the caller places its program, then calls start() */
static void begin(int version) { make_story_static((uint8_t)version, STORY_SIZE, STATIC_BASE); }

#define LOG_IS(s) (strcmp(log_buf, (s)) == 0)
static void expect_log(const char *want, int line) {
    tests_run++;
    if (LOG_IS(want)) tests_passed++;
    else printf("FAIL (line %d): log is\n    %s\n  wanted\n    %s\n", line, log_buf, want);
}
#define EXPECT_LOG(s) expect_log((s), __LINE__)

/* ---- tests ---- */

static void test_screen_opcodes(void) {
    puts("== screen opcodes: arguments and order relative to text ==");
    begin(4);
    /*  VAR opcodes are 0xE0 + number. Operand types: small = 01, large = 00, omitted = 11.
     *  print_char 'a' (E5 7F 61) ; set_text_style 2 (F1 7F 02) ; print_char 'b' ; split_window 3 (EA 7F 03) ;
     *  set_window 1 (EB 7F 01) ; set_cursor 3,7 (EF 5F 03 07) ; erase_line 1 (EE 7F 01) ;
     *  erase_window -1 (ED 3F FF FF) ; buffer_mode 0 (F2 7F 00) ; set_window 0 ; quit */
    PUT(0x100, 0xE5, 0x7F, 'a',  0xF1, 0x7F, 2,  0xE5, 0x7F, 'b',
               0xEA, 0x7F, 3,    0xEB, 0x7F, 1,  0xEF, 0x5F, 3, 7,  0xEE, 0x7F, 1,
               0xED, 0x3F, 0xFF, 0xFF,  0xF2, 0x7F, 0,  0xEB, 0x7F, 0,  0xBA);
    start(&fake);
    CHECK(zvibe_run(ctx) == ZVIBE_GAME_FINISHED);
    EXPECT_LOG("T:a;style(2);T:b;split(3);window(1);cursor(3,7);erase_line(1);erase_window(-1);buffer(0);window(0);");
    end();

    /* erase_window with the other values, and erase_line with a value that is not 1 */
    begin(4);
    PUT(0x100, 0xED, 0x7F, 0,  0xED, 0x7F, 1,  0xED, 0x3F, 0xFF, 0xFE,  0xEE, 0x7F, 7,  0xBA);
    start(&fake);
    CHECK(zvibe_run(ctx) == ZVIBE_GAME_FINISHED);
    EXPECT_LOG("erase_window(0);erase_window(1);erase_window(-2);erase_line(7);");
    end();

    /* in a V3 story the V4 screen table is never used, even when one is registered */
    begin(3);
    PUT(0x100, 0xEA, 0x7F, 3,  0xEB, 0x7F, 1,  0xBA);
    start(&fake);
    CHECK(zvibe_run(ctx) == ZVIBE_GAME_FINISHED);
    EXPECT_LOG("");
    end();

    /* no screen registered: every screen opcode is ignored and nothing breaks */
    begin(4);
    PUT(0x100, 0xEA, 0x7F, 3,  0xEB, 0x7F, 1,  0xEF, 0x5F, 1, 1,  0xF1, 0x7F, 4,  0xBA);
    start(NULL);
    CHECK(zvibe_run(ctx) == ZVIBE_GAME_FINISHED);
    end();
}

static void test_get_cursor(void) {
    puts("== get_cursor ==");
    begin(4);
    PUT(0x100, 0xF0, 0x3F, 0x03, 0x00,  0xBA);                 /* get_cursor 0x0300 ; quit */
    start(&fake);
    memset(ram + 0x300, 0xEE, 6);
    CHECK(zvibe_run(ctx) == ZVIBE_GAME_FINISHED);
    CHECK(ram[0x300] == 0 && ram[0x301] == 5);                 /* word 0: row */
    CHECK(ram[0x302] == 0 && ram[0x303] == 9);                 /* word 1: column */
    CHECK(ram[0x304] == 0xEE && ram[0x305] == 0xEE);           /* nothing past the two words */
    EXPECT_LOG("get_cursor;");
    end();
}

static void test_get_cursor_bounds(void) {
    puts("== get_cursor stays inside dynamic memory ==");
    /* the table's last two bytes fall in static memory: nothing past the dynamic area may change */
    begin(4);
    PUT(0x100, 0xF0, 0x3F, 0x3B, 0xFF,  0xBA);                 /* get_cursor 0x3BFF ; quit */
    start(&fake);
    memset(ram + STATIC_BASE, 0xEE, 8);
    CHECK(zvibe_run(ctx) == ZVIBE_GAME_FINISHED);
    CHECK(ram[STATIC_BASE - 1] == 0);                          /* the one byte that is writable: row, high */
    CHECK(ram[STATIC_BASE] == 0xEE && ram[STATIC_BASE + 1] == 0xEE && ram[STATIC_BASE + 2] == 0xEE);
    end();
}

static void test_output_streams(void) {
    puts("== output streams ==");

    /* the memory table holds ZSCII: extended characters keep their codes, null is dropped */
    begin(4);
    PUT(0x100,
        0xF3, 0x4F, 3, 0x04, 0x00,
        0xE5, 0x7F, 0x9B,                                      /* print_char 155 */
        0xE5, 0x7F, 0x00,                                      /* print_char 0: nothing */
        0xE5, 0x7F, 'a',
        0xB2, 0x14, 0xC4, 0xEC, 0xA5,                          /* print "<ZSCII escape 155>" */
        0xF3, 0x3F, 0xFF, 0xFD,
        0xBA);
    start(&fake);
    CHECK(zvibe_run(ctx) == ZVIBE_GAME_FINISHED);
    CHECK(ram[0x400] == 0 && ram[0x401] == 3);
    CHECK(ram[0x402] == 0x9B && ram[0x403] == 'a' && ram[0x404] == 0x9B);
    end();

    /* stream 3: text goes to the table (newline stored as 13), the count is the first word, and
     * nothing reaches the screen while it is selected. Selected twice, nested. */
    begin(4);
    PUT(0x100,
        0xF3, 0x4F, 3, 0x04, 0x00,                             /* output_stream 3, 0x0400 */
        0xE5, 0x7F, 'x', 0xBB,                                 /* print_char 'x' ; new_line */
        0xF3, 0x4F, 3, 0x04, 0x20,                             /* output_stream 3, 0x0420 (nested) */
        0xE5, 0x7F, 'y',
        0xF3, 0x3F, 0xFF, 0xFD,                                /* output_stream -3: back to 0x0400 */
        0xE5, 0x7F, 'z',
        0xF3, 0x3F, 0xFF, 0xFD,                                /* output_stream -3: back to the screen */
        0xE5, 0x7F, 'C',                                       /* this one is shown */
        0xBA);
    start(&fake);
    memset(ram + 0x400, 0xEE, 0x40);                           /* the interpreter must zero the counts itself */
    CHECK(zvibe_run(ctx) == ZVIBE_GAME_FINISHED);
    CHECK(ram[0x400] == 0 && ram[0x401] == 3);                 /* 'x', newline, 'z' */
    CHECK(ram[0x402] == 'x' && ram[0x403] == 13 && ram[0x404] == 'z');
    CHECK(ram[0x420] == 0 && ram[0x421] == 1 && ram[0x422] == 'y');
    EXPECT_LOG("T:C;");                                        /* only the last character was shown */
    end();

    /* stream 1 off and on */
    begin(4);
    PUT(0x100,
        0xF3, 0x3F, 0xFF, 0xFF,                                /* output_stream -1: screen off */
        0xE5, 0x7F, 'h',
        0xF3, 0x7F, 1,                                         /* output_stream 1: screen on */
        0xE5, 0x7F, 's',
        0xF3, 0x7F, 2,  0xF3, 0x7F, 4,                         /* streams 2 and 4: ignored */
        0xE5, 0x7F, 't',
        0xBA);
    start(&fake);
    CHECK(zvibe_run(ctx) == ZVIBE_GAME_FINISHED);
    EXPECT_LOG("T:s;T:t;");
    end();

    /* print_num and print to a table go through the same path as print_char */
    begin(4);
    PUT(0x100,
        0xF3, 0x4F, 3, 0x04, 0x00,                             /* output_stream 3, 0x0400 */
        0xE6, 0x7F, 42,                                        /* print_num 42 */
        0xF3, 0x3F, 0xFF, 0xFD,                                /* output_stream -3 */
        0xBA);
    start(&fake);
    CHECK(zvibe_run(ctx) == ZVIBE_GAME_FINISHED);
    CHECK(ram[0x401] == 2 && ram[0x402] == '4' && ram[0x403] == '2');
    EXPECT_LOG("");
    end();

    /* stream 3 also works in a V3 story of a dual build */
    begin(3);
    PUT(0x100, 0xF3, 0x4F, 3, 0x04, 0x00,  0xE5, 0x7F, 'v',  0xF3, 0x3F, 0xFF, 0xFD,  0xBA);
    start(NULL);
    CHECK(zvibe_run(ctx) == ZVIBE_GAME_FINISHED);
    CHECK(ram[0x401] == 1 && ram[0x402] == 'v');
    end();

    /* a stream that is not selected is not deselected twice: -3 with nothing selected is harmless */
    begin(4);
    PUT(0x100, 0xF3, 0x3F, 0xFF, 0xFD,  0xE5, 0x7F, 'k',  0xBA);
    start(&fake);
    CHECK(zvibe_run(ctx) == ZVIBE_GAME_FINISHED);
    EXPECT_LOG("T:k;");
    end();

    /* the opcodes the build does not implement are ignored, not fatal */
    begin(4);
    PUT(0x100, 0xF4, 0x7F, 1,  0xF5, 0x7F, 1,  0xE5, 0x7F, 'n',  0xBA);   /* input_stream 1 ; sound_effect 1 */
    start(&fake);
    CHECK(zvibe_run(ctx) == ZVIBE_GAME_FINISHED);
    EXPECT_LOG("T:n;");
    end();
}

static void test_read_char(void) {
    puts("== read_char ==");
    begin(4);
    PUT(0x100, 0xF6, 0x7F, 1, 0x10,  0xBA);                    /* read_char 1 -> g10 ; quit */
    start(&fake);
    CHECK(zvibe_input_char(ctx, 'x') == ZVIBE_ERROR);          /* nothing asked for yet */
    CHECK(zvibe_run(ctx) == ZVIBE_WAIT_FOR_CHAR);
    CHECK(zvibe_input(ctx, "text") == ZVIBE_ERROR);            /* a line is not what was asked for */
    CHECK(zvibe_input_char(ctx, 'q') == ZVIBE_OK);
    CHECK(global10() == 'q');
    CHECK(zvibe_input_char(ctx, 'z') == ZVIBE_ERROR);          /* answered once only */
    CHECK(zvibe_run(ctx) == ZVIBE_GAME_FINISHED);
    end();

    /* timer operands (time, routine) are accepted and ignored; the result is the key, 13 for Return */
    begin(4);
    PUT(0x100, 0xF6, 0x57, 1, 5, 0, 0x10,  0xBA);              /* read_char 1, 5, 0 -> g10 */
    start(&fake);
    CHECK(zvibe_run(ctx) == ZVIBE_WAIT_FOR_CHAR);
    CHECK(zvibe_input_char(ctx, 13) == ZVIBE_OK);
    CHECK(global10() == 13);
    CHECK(zvibe_run(ctx) == ZVIBE_GAME_FINISHED);
    end();

    /* a word-sized key code (a cursor key, 129) is stored whole */
    begin(4);
    PUT(0x100, 0xF6, 0x7F, 1, 0x11,  0xBA);
    start(&fake);
    CHECK(zvibe_run(ctx) == ZVIBE_WAIT_FOR_CHAR);
    CHECK(zvibe_input_char(ctx, 129) == ZVIBE_OK);
    CHECK(global11() == 129);
    end();
}

/* scan_table x table len [form] -> store ?branch. The branch skips a 3-byte "store g11 = 1", so
 * g11 == 1 means the branch was NOT taken. */
static void run_scan(const uint8_t *prog, unsigned n, unsigned want_result, int want_taken, int line) {
    begin(4);
    put(0x100, prog, n);
    start(&fake);
    ram[0x500] = 0x00; ram[0x501] = 10;                        /* word table: 10, 20, 30 */
    ram[0x502] = 0x00; ram[0x503] = 20;
    ram[0x504] = 0x00; ram[0x505] = 30;
    ram[0x510] = 7; ram[0x511] = 8; ram[0x512] = 9;            /* byte table: 7, 8, 9 */
    ram[0x202] = 0; ram[0x203] = 0;
    CHECK(zvibe_run(ctx) == ZVIBE_GAME_FINISHED);
    if (global10() != want_result || (global11() == 0) != (want_taken != 0))
        printf("  scan case at line %d: result %u (want %u), branch %s (want %s)\n", line, global10(), want_result,
               global11() == 0 ? "taken" : "not taken", want_taken ? "taken" : "not taken");
    CHECK(global10() == want_result);
    CHECK((global11() == 0) == (want_taken != 0));
    end();
}

static void test_scan_table(void) {
    puts("== scan_table ==");
    /* F7 = scan_table; types small, large, small, omitted = 01 00 01 11 = 0x47; then the store
     * variable (g10) and the branch byte C5 (on true, short, offset 5: skips the next 3 bytes), then
     * "store g11 = 1" (0D 11 01), then quit. */
    static const uint8_t found_second[] = { 0xF7, 0x47, 20, 0x05, 0x00, 3,  0x10, 0xC5,  0x0D, 0x11, 0x01,  0xBA };
    static const uint8_t found_first[]  = { 0xF7, 0x47, 10, 0x05, 0x00, 3,  0x10, 0xC5,  0x0D, 0x11, 0x01,  0xBA };
    static const uint8_t found_last[]   = { 0xF7, 0x47, 30, 0x05, 0x00, 3,  0x10, 0xC5,  0x0D, 0x11, 0x01,  0xBA };
    static const uint8_t missing[]      = { 0xF7, 0x47, 99, 0x05, 0x00, 3,  0x10, 0xC5,  0x0D, 0x11, 0x01,  0xBA };
    static const uint8_t past_the_end[] = { 0xF7, 0x47, 30, 0x05, 0x00, 2,  0x10, 0xC5,  0x0D, 0x11, 0x01,  0xBA };   /* only 2 entries */
    static const uint8_t zero_len[]     = { 0xF7, 0x47, 10, 0x05, 0x00, 0,  0x10, 0xC5,  0x0D, 0x11, 0x01,  0xBA };
    /* byte entries: form 0x01 = bytes, 1 byte each; types small, large, small, small = 0x45 */
    static const uint8_t byte_found[]   = { 0xF7, 0x45, 8, 0x05, 0x10, 3, 0x01,  0x10, 0xC5,  0x0D, 0x11, 0x01,  0xBA };
    /* the key 0x0108 (264) has low byte 8, which IS in the byte table, but a byte entry can never equal 264.
     * types: large, large, small, small = 00 00 01 01 = 0x05 */
    static const uint8_t byte_big_x[]   = { 0xF7, 0x05, 0x01, 0x08, 0x05, 0x10, 3, 0x01,  0x10, 0xC5,  0x0D, 0x11, 0x01,  0xBA };
    /* word entries 4 bytes apart (form 0x84): only the FIRST word of each entry is compared. The table
     * words are 10, 20, 30 at 0x500, 0x502, 0x504, so the entries start at 0x500 and 0x504. */
    static const uint8_t wide_found[]   = { 0xF7, 0x45, 30, 0x05, 0x00, 2, 0x84,  0x10, 0xC5,  0x0D, 0x11, 0x01,  0xBA };
    static const uint8_t wide_skipped[] = { 0xF7, 0x45, 20, 0x05, 0x00, 2, 0x84,  0x10, 0xC5,  0x0D, 0x11, 0x01,  0xBA };

    run_scan(found_second, sizeof found_second, 0x502, 1, __LINE__);
    run_scan(found_first,  sizeof found_first,  0x500, 1, __LINE__);
    run_scan(found_last,   sizeof found_last,   0x504, 1, __LINE__);
    run_scan(missing,      sizeof missing,      0,     0, __LINE__);
    run_scan(past_the_end, sizeof past_the_end, 0,     0, __LINE__);   /* 30 is the third entry; len is 2 */
    run_scan(zero_len,     sizeof zero_len,     0,     0, __LINE__);
    run_scan(byte_found,   sizeof byte_found,   0x511, 1, __LINE__);
    run_scan(byte_big_x,   sizeof byte_big_x,   0,     0, __LINE__);
    run_scan(wide_found,   sizeof wide_found,   0x504, 1, __LINE__);
    /* a match at address 0 is a match: byte form, x = 4 (the version byte), table 0, len 1 */
    {
        static const uint8_t at_zero[] = { 0xF7, 0x55, 4, 0, 1, 0x01,  0x10, 0xC5,  0x0D, 0x11, 0x01,  0xBA };
        run_scan(at_zero, sizeof at_zero, 0, 1, __LINE__);
    }
    run_scan(wide_skipped, sizeof wide_skipped, 0,     0, __LINE__);   /* 20 is the second half of entry 1 */
}

static void test_header_from_screen(void) {
    puts("== header reports the registered screen ==");
    begin(4);
    PUT(0x100, 0xBA);
    story[1] = 0xFF;
    start(&fake);
    CHECK(ram[0x20] == 30 && ram[0x21] == 100);                       /* rows, columns from the screen */
    CHECK((ram[1] & (1 << 2)) != 0);                                  /* bold available */
    CHECK((ram[1] & (1 << 3)) == 0);                                  /* italic not offered by this fake */
    CHECK((ram[1] & (1 << 4)) != 0);                                  /* fixed-space available */
    CHECK((ram[1] & (1 << 7)) == 0);                                  /* timed input is never claimed */
    CHECK(ram[0x32] == 1 && ram[0x33] == 1);                          /* standard 1.1 */

    fake_rows = 50; fake_cols = 132;                                  /* the terminal was resized: refresh */
    CHECK(zvibe_set_screen(ctx, &fake, NULL) == ZVIBE_OK);
    CHECK(ram[0x20] == 50 && ram[0x21] == 132);

    CHECK(zvibe_set_screen(ctx, NULL, NULL) == ZVIBE_OK);             /* no screen: defaults, no capabilities */
    CHECK(ram[0x20] == ZVIBE_SCREEN_ROWS && ram[0x21] == ZVIBE_SCREEN_COLS);
    CHECK((ram[1] & ((1 << 2) | (1 << 3) | (1 << 4))) == 0);
    fake_rows = 30; fake_cols = 100;
    end();
}

static void test_restart(void) {
    puts("== restart resets the interface state ==");
    begin(4);
    PUT(0x100, 0xF3, 0x3F, 0xFF, 0xFF,                               /* screen off */
               0xF3, 0x4F, 3, 0x04, 0x00,                            /* stream 3 selected */
               0xEA, 0x7F, 4,  0xEB, 0x7F, 1,  0xBA);                /* split 4, window 1 */
    start(&fake);
    CHECK(zvibe_run(ctx) == ZVIBE_GAME_FINISHED);
    CHECK(G->stream3_depth == 1 && G->output_screen_off == 1);
    CHECK(G->current_window == 1 && G->upper_window_lines == 4);
    log_clear();
    G->restart_requested = 1;
    CHECK(zvibe_restart_completed(ctx) == ZVIBE_OK);
    CHECK(G->stream3_depth == 0 && G->output_screen_off == 0);        /* streams back to the screen */
    CHECK(G->current_window == 0 && G->upper_window_lines == 0);      /* unsplit */
    EXPECT_LOG("buffer(1);style(0);erase_window(-1);");               /* buffered, unstyled, cleared */
    end();
}

static void test_dual_v3_unchanged(void) {
    puts("== a V3 story in a dual build still sees no V4 screen traffic ==");
    begin(3);
    PUT(0x100, 0xE5, 0x7F, 'a',  0xEA, 0x7F, 3,  0xE5, 0x7F, 'b',  0xBA);
    start(&fake);
    CHECK(zvibe_run(ctx) == ZVIBE_GAME_FINISHED);
    EXPECT_LOG("T:a;T:b;");
    end();
}

/* A small assembler for building test programs at 0x100 */
static unsigned pc;
static void emit(const uint8_t *b, unsigned n) { put(pc, b, n); pc += n; }
#define EMIT(...) do { static const uint8_t e_[] = { __VA_ARGS__ }; emit(e_, sizeof e_); } while (0)
static void select_table(unsigned table) {                           /* output_stream 3, table */
    uint8_t b[5] = { 0xF3, 0x4F, 3, (uint8_t)(table >> 8), (uint8_t)table };
    emit(b, 5);
}
static void print_char(char c) { uint8_t b[3] = { 0xE5, 0x7F, (uint8_t)c }; emit(b, 3); }

static void test_stream3_limits(void) {
    unsigned i;
    puts("== output stream 3: nesting limit, resuming, long tables, stream 1 ==");

    /* sixteen nested captures work, and each table gets the text printed while it was innermost:
     * its own letter when selected, then the letter printed after the next one closed */
    begin(4);
    pc = 0x100;
    for (i = 0; i < 16; i++) { select_table(0x600 + 0x10 * i); print_char((char)('A' + i)); }
    for (i = 16; i > 0; i--) { EMIT(0xF3, 0x3F, 0xFF, 0xFD); print_char((char)('a' + i - 1)); }
    EMIT(0xBA);
    start(&fake);
    CHECK(zvibe_run(ctx) == ZVIBE_GAME_FINISHED);
    for (i = 0; i < 16; i++) {
        unsigned t = 0x600 + 0x10 * i;
        unsigned count = ((unsigned)ram[t] << 8) | ram[t + 1];
        CHECK(ram[t + 2] == 'A' + i);
        if (i < 15) { CHECK(count == 2); CHECK(ram[t + 3] == 'a' + i + 1); }
        else        { CHECK(count == 1); }
    }
    EXPECT_LOG("T:a;");                                      /* what was printed after the outermost closed */
    end();

    /* the seventeenth selection is fatal: the interpreter aborts (checked in a child process) */
    {
        pid_t child;
        int status = 0;
        begin(4);
        pc = 0x100;
        for (i = 0; i < 17; i++) select_table(0x600 + 0x10 * i);
        EMIT(0xBA);
        fflush(stdout);
        child = fork();
        if (child == 0) {
            freopen("/dev/null", "w", stderr);                 /* the diagnostic is expected */
            start(&fake);
            zvibe_run(ctx);
            _exit(0);                                          /* reached only if nothing was fatal */
        }
        waitpid(child, &status, 0);
        CHECK(WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT);
        /* ...and sixteen are not */
        begin(4);
        pc = 0x100;
        for (i = 0; i < 16; i++) select_table(0x600 + 0x10 * i);
        EMIT(0xBA);
        start(&fake);
        CHECK(zvibe_run(ctx) == ZVIBE_GAME_FINISHED);
        end();
    }

    /* closing and reselecting re-zeroes the count (nesting level back to 15, then 16 again) */
    begin(4);
    pc = 0x100;
    select_table(0x600); print_char('x'); EMIT(0xF3, 0x3F, 0xFF, 0xFD);
    select_table(0x600); EMIT(0xF3, 0x3F, 0xFF, 0xFD);       /* same table, nothing printed: empty */
    EMIT(0xBA);
    start(&fake);
    CHECK(zvibe_run(ctx) == ZVIBE_GAME_FINISHED);
    CHECK(ram[0x600] == 0 && ram[0x601] == 0);
    end();

    /* a table longer than 255 characters: the count crosses into the high byte (301 = 0x012D) */
    begin(4);
    pc = 0x100;
    select_table(0x400);
    EMIT(0xCD, 0x4F, 0x12, 0x01, 0x2C);                      /* store g2 = 300 */
    EMIT(0xE5, 0x7F, 'a',  0x04, 0x12, 0x00, 0x3F, 0xFA);    /* loop: print_char 'a' ; dec_chk g2 0 ?~loop */
    EMIT(0xF3, 0x3F, 0xFF, 0xFD, 0xBA);
    start(&fake);
    CHECK(zvibe_run(ctx) == ZVIBE_GAME_FINISHED);
    CHECK(ram[0x400] == 0x01 && ram[0x401] == 0x2D);
    CHECK(ram[0x402] == 'a' && ram[0x402 + 300] == 'a' && ram[0x402 + 301] != 'a');
    end();

    /* stream 1 deselected before a capture stays deselected after it; deselecting during one counts too */
    begin(4);
    pc = 0x100;
    EMIT(0xF3, 0x3F, 0xFF, 0xFF);                            /* screen off */
    select_table(0x400); print_char('c');
    EMIT(0xF3, 0x3F, 0xFF, 0xFD);
    print_char('1');                                         /* still off: nothing shown */
    EMIT(0xF3, 0x7F, 1);                                     /* screen on */
    select_table(0x400);
    EMIT(0xF3, 0x3F, 0xFF, 0xFF);                            /* screen off during the capture */
    EMIT(0xF3, 0x3F, 0xFF, 0xFD);
    print_char('2');                                         /* off: nothing shown */
    EMIT(0xF3, 0x7F, 1);
    print_char('3');
    EMIT(0xBA);
    start(&fake);
    CHECK(zvibe_run(ctx) == ZVIBE_GAME_FINISHED);
    EXPECT_LOG("T:3;");
    end();
}

static void test_scan_table_more(void) {
    puts("== scan_table: inverted branch, stack store, static table ==");

    /* branch on false (0x45 = on false, short, offset 5): found does not branch, missing does */
    static const uint8_t inv_found[]   = { 0xF7, 0x47, 20, 0x05, 0x00, 3,  0x10, 0x45,  0x0D, 0x11, 0x01,  0xBA };
    static const uint8_t inv_missing[] = { 0xF7, 0x47, 99, 0x05, 0x00, 3,  0x10, 0x45,  0x0D, 0x11, 0x01,  0xBA };
    run_scan(inv_found,   sizeof inv_found,   0x502, 0, __LINE__);
    run_scan(inv_missing, sizeof inv_missing, 0,     1, __LINE__);

    /* the result goes to the stack when the store variable is 0 (branch offset 2 = fall through) */
    begin(4);
    PUT(0x100, 0xF7, 0x47, 30, 0x05, 0x00, 3,  0x00, 0xC2,  0xE9, 0x7F, 0x10,  0xBA);   /* ... -> (sp) ; pull g10 */
    start(&fake);
    ram[0x500] = 0; ram[0x501] = 10; ram[0x502] = 0; ram[0x503] = 20; ram[0x504] = 0; ram[0x505] = 30;
    CHECK(zvibe_run(ctx) == ZVIBE_GAME_FINISHED);
    CHECK(global10() == 0x504);
    end();

    /* a table in static memory is read, never written */
    begin(4);
    PUT(0x3C10, 0x00, 7, 0x00, 8, 0x00, 9);
    PUT(0x100, 0xF7, 0x07, 0x00, 8, 0x3C, 0x10, 3,  0x10, 0xC2,  0xBA);                 /* scan 8 in 0x3C10, len 3 */
    start(&fake);
    CHECK(zvibe_run(ctx) == ZVIBE_GAME_FINISHED);
    CHECK(global10() == 0x3C12);
    end();
}

/* A routine header byte above 127 (a corrupt story, or 200 locals) once made the signed loop
 * counter wrap, so the locals were pushed without end and ran off the stack. The call must now
 * complete, within the stack. */
static void test_many_locals(void) {
    puts("== a routine with 200 locals ==");
    begin(4);
    PUT(0x100, 0x98, 0xC0, 0x10,  0xBA);                      /* call_1s 0xC0 (=0x300 / 4) -> g10 ; quit */
    story[0x300] = 200;                                       /* 200 locals, all default 0 */
    story[0x300 + 1 + 400] = 0xB0;                            /* rtrue */
    start(&fake);
    alarm(20);                                                /* the old bug looped forever */
    CHECK(zvibe_run(ctx) == ZVIBE_GAME_FINISHED);
    alarm(0);
    CHECK(global10() == 1);
    CHECK(G->stack_ptr == G->stack_mem);                      /* the frame was popped again */
    end();
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("v4 opcodes: ZVIBE_ENABLE_V4=%d\n", ZVIBE_ENABLE_V4);
    test_screen_opcodes();
    test_get_cursor();
    test_get_cursor_bounds();
    test_output_streams();
    test_read_char();
    test_scan_table();
    test_stream3_limits();
    test_scan_table_more();
    test_many_locals();
    test_header_from_screen();
    test_restart();
    test_dual_v3_unchanged();
    printf("\n%d/%d checks passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
