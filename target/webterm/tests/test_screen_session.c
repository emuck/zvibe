/* Copyright (c) 2025 Martin R. Raumann */
/* SPDX-License-Identifier: BSD-3-Clause */
/**
 * test_screen_session.c - a V4 story through the webterm session, drawn by a
 *                         VT100 emulator from the "screen" frames alone
 *
 * Plays data/v4screen.z4 the way the browser would (a key, then a typed line that
 * the browser echoes itself), feeds the terminal bytes in the screen frames to the
 * console tests' VT100 emulator, and compares the finished screen with the one
 * ansi_dump makes by driving the same story straight through the screen layer.
 * Then it reconnects and checks that the replay draws the same screen.
 *
 * Usage: test_screen_session <ref.grid from ansi_dump> <v4screen.z4>
 * Only built with ZVIBE_ENABLE_V4=1.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../webterm_session.h"
#include "mock_platform.h"
#include "../../console/tests/vt100_emu.h"

#if !ZVIBE_ENABLE_V4
#error "needs ZVIBE_ENABLE_V4=1"
#endif

static int tests_run, tests_passed;
#define CHECK(cond) do { tests_run++; if (cond) tests_passed++; \
    else printf("FAIL: %s (line %d)\n", #cond, __LINE__); } while (0)

/* Feed the terminal bytes of every screen frame from index `from` on to the emulator. */
static int feed_frames(const mock_platform_t *m, int from) {
    int i, frames = 0;
    for (i = from; i < m->frame_count; i++) {
        const char *f = mock_frame(m, i);
        const char *p = strstr(f, "\"type\":\"screen\"");
        const char *d = p ? strstr(f, "\"data\":\"") : NULL;
        if (!d) continue;
        frames++;
        for (d += 8; *d && *d != '"'; d++) {
            unsigned char c = (unsigned char)*d;
            if (c == '\\') {
                d++;
                switch (*d) {
                case 'n': em_put('\n'); break;
                case 'r': em_put('\r'); break;
                case 't': em_put('\t'); break;
                case '"': em_put('"'); break;
                case '\\': em_put('\\'); break;
                case 'u': {
                    unsigned v = 0; int k;
                    for (k = 1; k <= 4; k++) { char h = d[k]; v = v * 16 + (unsigned)(h <= '9' ? h - '0' : h - 'a' + 10); }
                    em_put((uint8_t)v);
                    d += 4;
                    break; }
                default: em_put((uint8_t)*d); break;
                }
            } else {
                em_put(c);
            }
        }
    }
    return frames;
}

static webterm_result_t run_to_stop(webterm_session_t *s) {
    webterm_result_t r;
    int limit = 100000;
    do { r = webterm_session_run(s); } while (r == WEBTERM_OK && --limit > 0);
    return r;
}

/* Compare the emulator with the reference grid file; returns 1 if identical. */
static int matches_reference(const char *path) {
    FILE *f = fopen(path, "r");
    char line[COLS + 8];
    int r, c, ok = 1;
    if (!f) { printf("cannot open %s\n", path); return 0; }
    for (r = 0; r < ROWS; r++) {
        if (!fgets(line, sizeof line, f) || memcmp(line, grid[r], COLS) != 0) { printf("  text row %d differs\n", r + 1); ok = 0; }
    }
    for (r = 0; r < ROWS; r++) {
        if (!fgets(line, sizeof line, f)) { ok = 0; break; }
        for (c = 0; c < COLS; c++) if (line[c] != '0' + attr[r][c]) { printf("  attr row %d differs\n", r + 1); ok = 0; break; }
    }
    if (fgets(line, sizeof line, f)) {
        int rr = -1, cc = -1;
        sscanf(line, "cursor %d %d", &rr, &cc);
        if (rr != cur_r || cc != cur_c) { printf("  cursor %d,%d vs %d,%d\n", cur_r, cur_c, rr, cc); ok = 0; }
    } else ok = 0;
    fclose(f);
    return ok;
}

int main(int argc, char **argv) {
    mock_platform_t *m = mock_create();
    webterm_session_t *s;
    FILE *sf;
    uint8_t *story;
    long size;
    int seen, i;
    const char *line = "hello world";

    if (argc < 3) { fprintf(stderr, "usage: test_screen_session ref.grid v4screen.z4\n"); return 2; }
    puts("webterm V4 screen: session frames drawn by a VT100 emulator");

    sf = fopen(argv[2], "rb");
    if (!sf) { printf("cannot open %s\n", argv[2]); return 2; }
    fseek(sf, 0, SEEK_END); size = ftell(sf); fseek(sf, 0, SEEK_SET);
    story = malloc((size_t)size);
    if (fread(story, 1, (size_t)size, sf) != (size_t)size) return 2;
    fclose(sf);

    s = webterm_session_create(&m->base);
    webterm_session_accept(s);
    em_reset();
    CHECK(webterm_session_load(s, story, (size_t)size) == WEBTERM_OK);

    /* the client is told the screen's size before any bytes */
    CHECK(mock_frame(m, 0) != NULL && strstr(mock_frame(m, 0), "\"reset\":true") != NULL);
    CHECK(strstr(mock_frame(m, 0), "\"rows\":24,\"cols\":80") != NULL);

    CHECK(run_to_stop(s) == WEBTERM_WAIT);
    CHECK(mock_last_frame_contains(m, "\"type\":\"screen\""));
    CHECK(mock_last_frame_contains(m, "\"key\":true"));          /* the story reads a key first */
    CHECK(!mock_any_frame_contains(m, "\"type\":\"output\""));   /* nothing is sent as plain text */
    seen = 0;
    CHECK(feed_frames(m, seen) > 0);
    seen = m->frame_count;

    webterm_session_on_char(s, 'q');
    CHECK(run_to_stop(s) == WEBTERM_WAIT);
    CHECK(mock_last_frame_contains(m, "\"prompt\":true"));       /* then a line */
    feed_frames(m, seen);
    seen = m->frame_count;

    /* a client that reconnects at the line prompt replays the same screen from nothing */
    {
        static char grid0[ROWS][COLS];
        static uint8_t attr0[ROWS][COLS];
        int r0 = cur_r, c0 = cur_c;
        memcpy(grid0, grid, sizeof grid0);
        memcpy(attr0, attr, sizeof attr0);
        webterm_session_disconnect(s);
        mock_clear_frames(m);
        CHECK(webterm_session_accept(s) == 1);
        CHECK(mock_frame(m, 0) && strstr(mock_frame(m, 0), "\"type\":\"resumed\""));
        CHECK(mock_any_frame_contains(m, "\"reset\":true"));   /* the client starts from a blank screen */
        em_reset();
        feed_frames(m, 0);
        CHECK(memcmp(grid0, grid, sizeof grid0) == 0);
        CHECK(memcmp(attr0, attr, sizeof attr0) == 0);
        CHECK(r0 == cur_r && c0 == cur_c);
        CHECK(mock_last_frame_contains(m, "\"prompt\":true"));
        seen = m->frame_count;
    }

    /* the browser echoes what is typed; the server sends only what follows */
    for (i = 0; line[i]; i++) { em_put(0x1B); em_put('['); em_put('0'); em_put('m'); em_put((uint8_t)line[i]); }
    webterm_session_on_input(s, line, strlen(line));
    CHECK(run_to_stop(s) == WEBTERM_DONE);
    {
        const char *first = mock_frame(m, seen);
        const char *d = first ? strstr(first, "\"data\":\"") : NULL;
        CHECK(d && strncmp(d + 8, "\\r\\n", 4) == 0);     /* the first thing sent is the newline */
    }
    feed_frames(m, seen);

    CHECK(matches_reference(argv[1]));

    webterm_session_destroy(s);
    mock_free(m);
    free(story);

    printf("\n%d/%d checks passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
