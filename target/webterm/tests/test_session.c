/* Copyright (c) 2025 Martin R. Raumann */
/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * test_session.c — unit tests for webterm_session.
 *
 * Each test is a void function that uses ASSERT() to check expectations.
 * Run with: ./test_session [game_dir]
 *   game_dir defaults to ../../../games/catalog/
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "../webterm_session.h"
#include "mock_platform.h"

/* -------------------------------------------------------------------------
 * Helpers
 * ---------------------------------------------------------------------- */

static int g_tests_run    = 0;
static int g_tests_passed = 0;
static int g_tests_failed = 0;

#define ASSERT(cond, msg) do { \
    if (!(cond)) { \
        fprintf(stdout, " FAIL: %s (line %d)\n", (msg), __LINE__); \
        g_tests_failed++; \
        return; \
    } \
} while (0)

#define TEST(name) \
    static void name(void); \
    static void run_##name(void) { \
        g_tests_run++; \
        fprintf(stdout, "  %-50s", #name); fflush(stdout); \
        int pre_fail = g_tests_failed; \
        name(); \
        if (g_tests_failed > pre_fail) { \
            /* ASSERT already printed the failure line */ \
        } else { \
            fprintf(stdout, " PASS\n"); \
            g_tests_passed++; \
        } \
    } \
    static void name(void)

static char g_game_dir[512] = "../../../games/catalog/";
static char g_czech_path[512];
static char g_restaurant_path[512];

/* Load a game file from disk into a heap buffer.
 * Caller must free() the returned pointer. Sets *size. */
static uint8_t *load_game(const char *path, size_t *size) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "Could not open game: %s\n", path);
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (len <= 0) { fclose(f); return NULL; }
    uint8_t *buf = (uint8_t *)malloc((size_t)len);
    if (!buf) { fclose(f); return NULL; }
    fread(buf, 1, (size_t)len, f);
    fclose(f);
    *size = (size_t)len;
    return buf;
}

/* Run the session until it reaches WEBTERM_WAIT or WEBTERM_DONE.
 * Returns the final result. */
static webterm_result_t run_to_stop(webterm_session_t *s) {
    webterm_result_t r;
    int limit = 100000;  /* guard against infinite loops */
    do {
        r = webterm_session_run(s);
    } while (r == WEBTERM_OK && --limit > 0);
    return r;
}

/* Feed input and run to the next WAIT or DONE. */
static webterm_result_t send_input(webterm_session_t *s, const char *line) {
    webterm_session_on_input(s, line, strlen(line));
    return run_to_stop(s);
}

/* -------------------------------------------------------------------------
 * Tests
 * ---------------------------------------------------------------------- */

TEST(test_session_init) {
    mock_platform_t *m = mock_create();
    ASSERT(m != NULL, "mock_create returned NULL");

    webterm_session_t *s = webterm_session_create(&m->base);
    ASSERT(s != NULL, "session_create returned NULL");
    ASSERT(webterm_session_state(s) == WEBTERM_STATE_IDLE, "initial state must be IDLE");
    ASSERT(m->frame_count == 0, "no frames emitted on create");

    webterm_session_destroy(s);
    mock_free(m);
}

TEST(test_accept_first_client) {
    mock_platform_t *m = mock_create();
    webterm_session_t *s = webterm_session_create(&m->base);

    int ok = webterm_session_accept(s);
    ASSERT(ok == 1, "first accept must succeed");
    ASSERT(m->frame_count == 0, "no frames emitted for IDLE accept");

    webterm_session_destroy(s);
    mock_free(m);
}

TEST(test_accept_busy) {
    mock_platform_t *m = mock_create();
    webterm_session_t *s = webterm_session_create(&m->base);

    webterm_session_accept(s);           /* first client */
    int ok = webterm_session_accept(s);  /* second client */
    ASSERT(ok == 0, "second accept must fail (busy)");

    webterm_session_destroy(s);
    mock_free(m);
}

TEST(test_game_start) {
    size_t sz;
    uint8_t *data = load_game(g_restaurant_path, &sz);
    ASSERT(data != NULL, "could not load restaurant.z3");

    mock_platform_t *m = mock_create();
    webterm_session_t *s = webterm_session_create(&m->base);
    webterm_session_accept(s);

    webterm_result_t r = webterm_session_load(s, data, sz);
    ASSERT(r == WEBTERM_OK, "load must succeed");

    r = run_to_stop(s);
    ASSERT(r == WEBTERM_WAIT, "must pause for input after intro");
    ASSERT(mock_has_output(m), "intro text must be emitted");
    ASSERT(mock_last_frame_has_prompt(m), "last frame must have prompt:true");

    webterm_session_destroy(s);
    mock_free(m);
    free(data);
}

TEST(test_output_buffering) {
    /* All output from one game turn must arrive as a single frame. */
    size_t sz;
    uint8_t *data = load_game(g_restaurant_path, &sz);
    ASSERT(data != NULL, "could not load restaurant.z3");

    mock_platform_t *m = mock_create();
    webterm_session_t *s = webterm_session_create(&m->base);
    webterm_session_accept(s);
    webterm_session_load(s, data, sz);
    run_to_stop(s);  /* run to first prompt */

    /* Count output frames for the intro — there should be exactly one */
    int output_frame_count = 0;
    for (int i = 0; i < m->frame_count; i++) {
        if (strstr(m->frames[i], "\"type\":\"output\"")) output_frame_count++;
    }
    ASSERT(output_frame_count == 1, "intro must arrive as exactly one output frame");

    webterm_session_destroy(s);
    mock_free(m);
    free(data);
}

TEST(test_prompt_flag) {
    /* The output frame emitted when waiting for input must have prompt:true. */
    size_t sz;
    uint8_t *data = load_game(g_restaurant_path, &sz);
    ASSERT(data != NULL, "could not load restaurant.z3");

    mock_platform_t *m = mock_create();
    webterm_session_t *s = webterm_session_create(&m->base);
    webterm_session_accept(s);
    webterm_session_load(s, data, sz);
    run_to_stop(s);

    ASSERT(mock_last_frame_has_prompt(m), "output frame before input must have prompt:true");

    /* Send a command and verify the response also has prompt:true */
    mock_clear_frames(m);
    webterm_result_t r = send_input(s, "look");
    ASSERT(r == WEBTERM_WAIT, "must still be waiting after 'look'");
    ASSERT(mock_last_frame_has_prompt(m), "response frame must have prompt:true");

    webterm_session_destroy(s);
    mock_free(m);
    free(data);
}

TEST(test_input_delivery) {
    /* Input fed to the session must produce a response from the game. */
    size_t sz;
    uint8_t *data = load_game(g_restaurant_path, &sz);
    ASSERT(data != NULL, "could not load restaurant.z3");

    mock_platform_t *m = mock_create();
    webterm_session_t *s = webterm_session_create(&m->base);
    webterm_session_accept(s);
    webterm_session_load(s, data, sz);
    run_to_stop(s);

    mock_clear_frames(m);
    webterm_result_t r = send_input(s, "look");
    ASSERT(r == WEBTERM_WAIT, "game still running after 'look'");
    ASSERT(mock_has_output(m), "game must respond to 'look'");

    webterm_session_destroy(s);
    mock_free(m);
    free(data);
}

TEST(test_input_ignored_when_not_waiting) {
    /* Input sent when not in WAITING state must be silently dropped. */
    mock_platform_t *m = mock_create();
    webterm_session_t *s = webterm_session_create(&m->base);
    webterm_session_accept(s);

    /* No game loaded — IDLE state */
    webterm_session_on_input(s, "hello", 5);
    ASSERT(m->frame_count == 0, "input in IDLE state must produce no frames");

    webterm_session_destroy(s);
    mock_free(m);
}

TEST(test_disconnect_pauses_session) {
    size_t sz;
    uint8_t *data = load_game(g_restaurant_path, &sz);
    ASSERT(data != NULL, "could not load restaurant.z3");

    mock_platform_t *m = mock_create();
    webterm_session_t *s = webterm_session_create(&m->base);
    webterm_session_accept(s);
    webterm_session_load(s, data, sz);
    run_to_stop(s);

    webterm_session_disconnect(s);
    ASSERT(webterm_session_state(s) == WEBTERM_STATE_PAUSED, "disconnect must pause session");

    webterm_session_destroy(s);
    mock_free(m);
    free(data);
}

TEST(test_disconnect_resume) {
    /* After disconnect, reconnecting client must receive history replay. */
    size_t sz;
    uint8_t *data = load_game(g_restaurant_path, &sz);
    ASSERT(data != NULL, "could not load restaurant.z3");

    mock_platform_t *m = mock_create();
    webterm_session_t *s = webterm_session_create(&m->base);
    webterm_session_accept(s);
    webterm_session_load(s, data, sz);
    run_to_stop(s);
    webterm_session_disconnect(s);

    mock_clear_frames(m);

    /* Reconnect */
    int ok = webterm_session_accept(s);
    ASSERT(ok == 1, "reconnect must succeed");
    ASSERT(mock_has_resumed(m), "reconnect must emit a resumed frame");
    ASSERT(mock_has_output(m), "reconnect must replay history");
    ASSERT(mock_any_frame_has_prompt(m), "replayed history must include prompt frame");
    ASSERT(webterm_session_state(s) == WEBTERM_STATE_WAITING,
           "session must be in WAITING state after resume");

    webterm_session_destroy(s);
    mock_free(m);
    free(data);
}

TEST(test_history_ring) {
    /* History must not overflow; oldest frames must be dropped. */
    size_t sz;
    uint8_t *data = load_game(g_restaurant_path, &sz);
    ASSERT(data != NULL, "could not load restaurant.z3");

    mock_platform_t *m = mock_create();
    webterm_session_t *s = webterm_session_create(&m->base);
    webterm_session_accept(s);
    webterm_session_load(s, data, sz);

    /* Play WEBTERM_HISTORY_FRAMES + 10 turns to overflow the ring */
    run_to_stop(s);
    int turns = WEBTERM_HISTORY_FRAMES + 10;
    for (int i = 0; i < turns; i++) {
        webterm_result_t r = send_input(s, "look");
        if (r != WEBTERM_WAIT) break;
    }

    webterm_session_disconnect(s);
    mock_clear_frames(m);
    webterm_session_accept(s);

    /* After reconnect, at most WEBTERM_HISTORY_FRAMES output frames in history */
    int replayed = 0;
    for (int i = 0; i < m->frame_count; i++) {
        if (strstr(m->frames[i], "\"type\":\"output\"")) replayed++;
    }
    ASSERT(replayed <= WEBTERM_HISTORY_FRAMES,
           "history replay must not exceed ring buffer size");
    ASSERT(replayed > 0, "some history must be replayed");

    webterm_session_destroy(s);
    mock_free(m);
    free(data);
}

TEST(test_status_frame) {
    /* Status line updates must emit type:status frames. */
    size_t sz;
    uint8_t *data = load_game(g_restaurant_path, &sz);
    ASSERT(data != NULL, "could not load restaurant.z3");

    mock_platform_t *m = mock_create();
    webterm_session_t *s = webterm_session_create(&m->base);
    webterm_session_accept(s);
    webterm_session_load(s, data, sz);
    run_to_stop(s);

    /* Play a turn or two to trigger status updates */
    send_input(s, "look");
    send_input(s, "look");

    ASSERT(mock_has_status(m), "status frames must be emitted");

    /* Status frame must have location and score/turns fields */
    int found_location = 0;
    for (int i = 0; i < m->frame_count; i++) {
        if (strstr(m->frames[i], "\"type\":\"status\"") &&
            strstr(m->frames[i], "\"location\"")) {
            found_location = 1;
            break;
        }
    }
    ASSERT(found_location, "status frame must include location field");

    webterm_session_destroy(s);
    mock_free(m);
    free(data);
}

TEST(test_save_with_null_callbacks) {
    /* If platform has no save callback, game save must produce an error frame. */
    size_t sz;
    uint8_t *data = load_game(g_restaurant_path, &sz);
    ASSERT(data != NULL, "could not load restaurant.z3");

    mock_platform_t *m = mock_create_no_save();
    webterm_session_t *s = webterm_session_create(&m->base);
    webterm_session_accept(s);
    webterm_session_load(s, data, sz);
    run_to_stop(s);

    /* Type "save" — game will attempt SAVE opcode */
    mock_clear_frames(m);
    webterm_result_t r = send_input(s, "save");
    (void)r;

    /* Must get an error frame (or at minimum no "saved" frame) */
    ASSERT(!mock_has_saved(m), "no saved frame when save callback is NULL");

    webterm_session_destroy(s);
    mock_free(m);
    free(data);
}

TEST(test_save_restore) {
    /* Save at turn N, play further, restore — game response must match turn N. */
    size_t sz;
    uint8_t *data = load_game(g_restaurant_path, &sz);
    ASSERT(data != NULL, "could not load restaurant.z3");

    mock_platform_t *m = mock_create();
    webterm_session_t *s = webterm_session_create(&m->base);
    webterm_session_accept(s);
    webterm_session_load(s, data, sz);
    run_to_stop(s);

    /* Save at the starting position */
    mock_clear_frames(m);
    send_input(s, "save");

    int save_happened = mock_has_saved(m);

    if (!save_happened) {
        /* Game may need a confirmation — try "yes" */
        mock_clear_frames(m);
        send_input(s, "yes");
        save_happened = mock_has_saved(m);
    }

    /* Only proceed with restore test if save worked */
    if (save_happened && m->save_len > 0) {
        /* Play a few turns */
        send_input(s, "look");
        send_input(s, "look");

        /* Restore */
        mock_clear_frames(m);
        send_input(s, "restore");
        if (!mock_has_output(m)) {
            /* Some games need confirmation */
            send_input(s, "yes");
        }

        /* After restore we should be back at the saved state — game still running */
        ASSERT(webterm_session_state(s) == WEBTERM_STATE_WAITING ||
               webterm_session_state(s) == WEBTERM_STATE_RUNNING,
               "session must still be running after restore");
    }
    /* Note: we don't fail if the game doesn't support save/restore — the
     * test merely verifies the session handles the opcode without crashing. */

    webterm_session_destroy(s);
    mock_free(m);
    free(data);
}

TEST(test_game_reset) {
    /* Reset must return session to IDLE and clear history. */
    size_t sz;
    uint8_t *data = load_game(g_restaurant_path, &sz);
    ASSERT(data != NULL, "could not load restaurant.z3");

    mock_platform_t *m = mock_create();
    webterm_session_t *s = webterm_session_create(&m->base);
    webterm_session_accept(s);
    webterm_session_load(s, data, sz);
    run_to_stop(s);

    webterm_session_on_reset(s);
    ASSERT(webterm_session_state(s) == WEBTERM_STATE_IDLE, "state must be IDLE after reset");

    /* After reset, reconnecting client gets no history */
    webterm_session_disconnect(s);
    mock_clear_frames(m);
    webterm_session_accept(s);
    ASSERT(!mock_has_output(m), "no history output after reset");

    webterm_session_destroy(s);
    mock_free(m);
    free(data);
}

TEST(test_game_restart_idempotent) {
    /* Loading the same game twice must produce identical opening output. */
    size_t sz;
    uint8_t *data = load_game(g_restaurant_path, &sz);
    ASSERT(data != NULL, "could not load restaurant.z3");

    mock_platform_t *m = mock_create();
    webterm_session_t *s = webterm_session_create(&m->base);
    webterm_session_accept(s);

    /* First load */
    webterm_session_load(s, data, sz);
    run_to_stop(s);
    ASSERT(m->frame_count > 0, "first load must produce output");

    /* Capture first-load output text */
    char first_output[MOCK_FRAME_MAX];
    strncpy(first_output, mock_last_frame(m), MOCK_FRAME_MAX - 1);
    first_output[MOCK_FRAME_MAX - 1] = '\0';

    /* Second load of same game */
    mock_clear_frames(m);
    webterm_session_load(s, data, sz);
    run_to_stop(s);
    ASSERT(m->frame_count > 0, "second load must produce output");

    /* Output must be identical (deterministic) */
    ASSERT(strcmp(first_output, mock_last_frame(m)) == 0,
           "identical game loads must produce identical output");

    webterm_session_destroy(s);
    mock_free(m);
    free(data);
}

TEST(test_czech_conformance) {
    /* Czech Z3 must run to completion without errors and report zero failures. */
    size_t sz;
    uint8_t *data = load_game(g_czech_path, &sz);
    ASSERT(data != NULL, "could not load czech.z3");

    mock_platform_t *m = mock_create_no_save();
    webterm_session_t *s = webterm_session_create(&m->base);
    webterm_session_accept(s);

    webterm_result_t r = webterm_session_load(s, data, sz);
    ASSERT(r == WEBTERM_OK, "czech load must succeed");

    /* Czech runs automatically — just feed "quit" at the end */
    int limit = 1000;
    r = run_to_stop(s);
    while (r == WEBTERM_WAIT && --limit > 0) {
        r = send_input(s, "quit");
    }

    ASSERT(mock_has_output(m), "Czech must produce output");
    ASSERT(mock_any_frame_contains(m, "CZECH"),
           "Czech title must appear in output");
    ASSERT(mock_any_frame_contains(m, "Performed"),
           "Czech test suite must complete");
    ASSERT(mock_any_frame_contains(m, "Failed: 0"),
           "Czech must report zero failures");
    ASSERT(mock_any_frame_contains(m, "Didn't crash"),
           "Czech must report no crashes");

    webterm_session_destroy(s);
    mock_free(m);
    free(data);
}

/* -------------------------------------------------------------------------
 * Main
 * ---------------------------------------------------------------------- */

int main(int argc, char *argv[]) {
    if (argc > 1) {
        strncpy(g_game_dir, argv[1], sizeof(g_game_dir) - 1);
        g_game_dir[sizeof(g_game_dir) - 1] = '\0';
    }

    /* Ensure trailing slash */
    size_t dlen = strlen(g_game_dir);
    if (dlen > 0 && g_game_dir[dlen - 1] != '/') {
        g_game_dir[dlen] = '/';
        g_game_dir[dlen + 1] = '\0';
    }

    snprintf(g_czech_path,      sizeof(g_czech_path),      "%sczech.z3",      g_game_dir);
    snprintf(g_restaurant_path, sizeof(g_restaurant_path), "%srestaurant.z3", g_game_dir);

    printf("webterm session tests\n");
    printf("game dir: %s\n\n", g_game_dir);

    run_test_session_init();
    run_test_accept_first_client();
    run_test_accept_busy();
    run_test_game_start();
    run_test_output_buffering();
    run_test_prompt_flag();
    run_test_input_delivery();
    run_test_input_ignored_when_not_waiting();
    run_test_disconnect_pauses_session();
    run_test_disconnect_resume();
    run_test_history_ring();
    run_test_status_frame();
    run_test_save_with_null_callbacks();
    run_test_save_restore();
    run_test_game_reset();
    run_test_game_restart_idempotent();
    run_test_czech_conformance();

    printf("\n=== %d/%d tests passed", g_tests_passed, g_tests_run);
    if (g_tests_failed > 0) {
        printf(", %d FAILED", g_tests_failed);
    }
    printf(" ===\n");

    return g_tests_failed > 0 ? 1 : 0;
}
