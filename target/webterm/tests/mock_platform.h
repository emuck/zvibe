/* Copyright (c) 2025 Martin R. Raumann */
/* SPDX-License-Identifier: BSD-3-Clause */
/**
 * @file mock_platform.h
 * @brief Mock webterm_platform_t for unit testing.
 *
 * Captures all outgoing JSON frames in memory and provides simple
 * in-memory save/restore storage. No networking required.
 */

#ifndef MOCK_PLATFORM_H
#define MOCK_PLATFORM_H

#include <stddef.h>
#include "../webterm_platform.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MOCK_MAX_FRAMES  512
#define MOCK_FRAME_MAX   8192
#define MOCK_SAVE_MAX    32768

typedef struct {
    webterm_platform_t base;   /* must be first */

    /* Captured outgoing frames */
    char   frames[MOCK_MAX_FRAMES][MOCK_FRAME_MAX];
    size_t frame_lens[MOCK_MAX_FRAMES];
    int    frame_count;

    /* Save/restore storage */
    unsigned char save_buf[MOCK_SAVE_MAX];
    size_t        save_len;

    /* Stats */
    int save_calls;
    int restore_calls;
} mock_platform_t;

/** Allocate and initialise a mock platform with save/restore enabled. */
mock_platform_t *mock_create(void);

/** Allocate a mock platform with save/restore callbacks set to NULL. */
mock_platform_t *mock_create_no_save(void);

/** Free a mock platform. */
void mock_free(mock_platform_t *m);

/** Clear captured frames (save storage is preserved). */
void mock_clear_frames(mock_platform_t *m);

/* -------------------------------------------------------------------------
 * Assertion helpers
 * ---------------------------------------------------------------------- */

/** Return the JSON string for frame index i (0 = oldest). NULL if out of range. */
const char *mock_frame(const mock_platform_t *m, int i);

/** Return the last captured frame, or NULL if none. */
const char *mock_last_frame(const mock_platform_t *m);

/** Return 1 if any captured frame contains the substring @p text. */
int mock_any_frame_contains(const mock_platform_t *m, const char *text);

/** Return 1 if the last captured frame contains the substring @p text. */
int mock_last_frame_contains(const mock_platform_t *m, const char *text);

/** Return 1 if any captured frame has "prompt":true. */
int mock_any_frame_has_prompt(const mock_platform_t *m);

/** Return 1 if the last captured frame has "prompt":true. */
int mock_last_frame_has_prompt(const mock_platform_t *m);

/** Return 1 if any captured frame has "type":"output". */
int mock_has_output(const mock_platform_t *m);

/** Return 1 if any captured frame has "type":"busy". */
int mock_has_busy(const mock_platform_t *m);

/** Return 1 if any captured frame has "type":"resumed". */
int mock_has_resumed(const mock_platform_t *m);

/** Return 1 if any captured frame has "type":"saved". */
int mock_has_saved(const mock_platform_t *m);

/** Return 1 if any captured frame has "type":"error". */
int mock_has_error(const mock_platform_t *m);

/** Return 1 if any captured frame has "type":"status". */
int mock_has_status(const mock_platform_t *m);

#ifdef __cplusplus
}
#endif

#endif /* MOCK_PLATFORM_H */
