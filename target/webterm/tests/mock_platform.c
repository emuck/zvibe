/* Copyright (c) 2025 Martin R. Raumann */
/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * mock_platform.c — in-memory webterm_platform_t for testing.
 */

#include "mock_platform.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* -------------------------------------------------------------------------
 * Platform callbacks
 * ---------------------------------------------------------------------- */

static void mock_send(webterm_platform_t *p, const char *json, size_t len) {
    mock_platform_t *m = (mock_platform_t *)p;
    if (m->frame_count >= MOCK_MAX_FRAMES) return;

    size_t copy = len < MOCK_FRAME_MAX - 1 ? len : MOCK_FRAME_MAX - 1;
    memcpy(m->frames[m->frame_count], json, copy);
    m->frames[m->frame_count][copy] = '\0';
    m->frame_lens[m->frame_count] = copy;
    m->frame_count++;
}

static int mock_save(webterm_platform_t *p, const void *data, size_t len) {
    mock_platform_t *m = (mock_platform_t *)p;
    m->save_calls++;
    size_t copy = len < MOCK_SAVE_MAX ? len : MOCK_SAVE_MAX;
    memcpy(m->save_buf, data, copy);
    m->save_len = copy;
    return 1;
}

static size_t mock_restore(webterm_platform_t *p, void *buf, size_t max) {
    mock_platform_t *m = (mock_platform_t *)p;
    m->restore_calls++;
    if (m->save_len == 0) return 0;
    size_t copy = m->save_len < max ? m->save_len : max;
    memcpy(buf, m->save_buf, copy);
    return copy;
}

/* -------------------------------------------------------------------------
 * Lifecycle
 * ---------------------------------------------------------------------- */

mock_platform_t *mock_create(void) {
    mock_platform_t *m = (mock_platform_t *)calloc(1, sizeof(mock_platform_t));
    if (!m) return NULL;
    m->base.send    = mock_send;
    m->base.save    = mock_save;
    m->base.restore = mock_restore;
    return m;
}

mock_platform_t *mock_create_no_save(void) {
    mock_platform_t *m = mock_create();
    if (!m) return NULL;
    m->base.save    = NULL;
    m->base.restore = NULL;
    return m;
}

void mock_free(mock_platform_t *m) {
    free(m);
}

void mock_clear_frames(mock_platform_t *m) {
    m->frame_count = 0;
}

/* -------------------------------------------------------------------------
 * Assertion helpers
 * ---------------------------------------------------------------------- */

const char *mock_frame(const mock_platform_t *m, int i) {
    if (i < 0 || i >= m->frame_count) return NULL;
    return m->frames[i];
}

const char *mock_last_frame(const mock_platform_t *m) {
    if (m->frame_count == 0) return NULL;
    return m->frames[m->frame_count - 1];
}

int mock_any_frame_contains(const mock_platform_t *m, const char *text) {
    for (int i = 0; i < m->frame_count; i++) {
        if (strstr(m->frames[i], text)) return 1;
    }
    return 0;
}

int mock_last_frame_contains(const mock_platform_t *m, const char *text) {
    const char *f = mock_last_frame(m);
    return f && strstr(f, text) ? 1 : 0;
}

int mock_any_frame_has_prompt(const mock_platform_t *m) {
    return mock_any_frame_contains(m, "\"prompt\":true");
}

int mock_last_frame_has_prompt(const mock_platform_t *m) {
    return mock_last_frame_contains(m, "\"prompt\":true");
}

int mock_has_output(const mock_platform_t *m) {
    return mock_any_frame_contains(m, "\"type\":\"output\"");
}

int mock_has_busy(const mock_platform_t *m) {
    return mock_any_frame_contains(m, "\"type\":\"busy\"");
}

int mock_has_resumed(const mock_platform_t *m) {
    return mock_any_frame_contains(m, "\"type\":\"resumed\"");
}

int mock_has_saved(const mock_platform_t *m) {
    return mock_any_frame_contains(m, "\"type\":\"saved\"");
}

int mock_has_error(const mock_platform_t *m) {
    return mock_any_frame_contains(m, "\"type\":\"error\"");
}

int mock_has_status(const mock_platform_t *m) {
    return mock_any_frame_contains(m, "\"type\":\"status\"");
}
