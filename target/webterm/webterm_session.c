/*
 * Copyright (c) 2025 Martin R. Raumann
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * webterm_session.c — platform-agnostic Z-machine web terminal session.
 *
 * See webterm_session.h for the public API and threading model.
 */

#include "webterm_session.h"
#include "zvibe_api.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* -------------------------------------------------------------------------
 * Compile-time constants (override before including this file if needed)
 * ---------------------------------------------------------------------- */

/** Raw output accumulation buffer (bytes). Sized for the longest
 *  Z-machine v3 turn output; truncation is silent (pathological case). */
#ifndef WEBTERM_OUT_BUF_SIZE
#define WEBTERM_OUT_BUF_SIZE 4096
#endif

/** JSON frame buffer. Must hold the JSON-encoded version of WEBTERM_OUT_BUF_SIZE
 *  plus frame overhead (~50 bytes). 2× the raw size covers worst-case escaping. */
#ifndef WEBTERM_JSON_BUF_SIZE
#define WEBTERM_JSON_BUF_SIZE (WEBTERM_OUT_BUF_SIZE * 2 + 128)
#endif

/** Scratch buffer for save data extraction.
 *  Z-machine v3 saves are 2-14KB for known games; 24KB gives comfortable headroom. */
#ifndef WEBTERM_SAVE_BUF_MAX
#define WEBTERM_SAVE_BUF_MAX 24576
#endif

/* -------------------------------------------------------------------------
 * Session struct
 * ---------------------------------------------------------------------- */

struct webterm_session_t {
    webterm_platform_t *platform;
    zvibeContext       *zvibe;
    webterm_state_t     state;
    int                 client_connected;
    int                 input_pending;   /* zvibe_input() was called, ready to run */

    /* Raw output accumulation — flushed as one JSON frame on each WAIT */
    char   out_buf[WEBTERM_OUT_BUF_SIZE];
    size_t out_len;

    /* Scratch buffer for save/restore data */
    uint8_t save_buf[WEBTERM_SAVE_BUF_MAX];

    /* Output history ring buffer.
     * Stores raw text per turn (not JSON) so history entries are compact
     * and never truncate the prompt flag. JSON encoding happens on replay. */
    char   history_text[WEBTERM_HISTORY_FRAMES][WEBTERM_HISTORY_TEXT_MAX];
    size_t history_text_lens[WEBTERM_HISTORY_FRAMES];
    int    history_prompt[WEBTERM_HISTORY_FRAMES];  /* 1 = frame had prompt:true */
    int    history_head;   /* index of the oldest stored entry */
    int    history_count;  /* number of entries currently stored */

    /* Reusable JSON encoding buffer (avoids per-call stack allocation) */
    char json_buf[WEBTERM_JSON_BUF_SIZE];
};

/* Single global session pointer used by the output callback.
 * zvibe itself is single-instance (global g_ctx), so this matches. */
static webterm_session_t *g_session = NULL;

/* -------------------------------------------------------------------------
 * Stub input function
 *
 * zvibe_create() requires a non-NULL input_func. We use the
 * zvibe_run() → ZVIBE_WAIT_FOR_INPUT → zvibe_input() pattern exclusively,
 * so this stub should never be called. It returns 0 (empty input) as a
 * safe fallback.
 * ---------------------------------------------------------------------- */
static size_t stub_input(char *buf, size_t max) {
    (void)buf; (void)max;
    return 0;
}

/* -------------------------------------------------------------------------
 * JSON helpers
 * ---------------------------------------------------------------------- */

/**
 * Write JSON-escaped content of @p in (length @p in_len) into @p out.
 * Does NOT write surrounding quotes. Returns number of bytes written.
 * Stops early if output would exceed @p out_max - 7 (largest escape: \uXXXX).
 */
static size_t json_escape(const char *in, size_t in_len,
                           char *out, size_t out_max) {
    size_t pos = 0;
    for (size_t i = 0; i < in_len; i++) {
        if (pos + 7 >= out_max) break;
        unsigned char c = (unsigned char)in[i];
        switch (c) {
            case '"':  out[pos++] = '\\'; out[pos++] = '"';  break;
            case '\\': out[pos++] = '\\'; out[pos++] = '\\'; break;
            case '\n': out[pos++] = '\\'; out[pos++] = 'n';  break;
            case '\r': out[pos++] = '\\'; out[pos++] = 'r';  break;
            case '\t': out[pos++] = '\\'; out[pos++] = 't';  break;
            default:
                if (c < 0x20) {
                    pos += (size_t)snprintf(out + pos, out_max - pos,
                                            "\\u%04x", (unsigned)c);
                } else {
                    out[pos++] = (char)c;
                }
        }
    }
    return pos;
}

/* -------------------------------------------------------------------------
 * Frame emission
 * ---------------------------------------------------------------------- */

/**
 * Push raw output text into the history ring buffer.
 * Stores unencoded text so entries are compact and the prompt flag is
 * never lost due to JSON frame truncation. JSON encoding happens on replay.
 * If the ring is full, the oldest entry is dropped.
 */
static void history_push(webterm_session_t *s,
                          const char *text, size_t len, int prompt) {
    int slot = (s->history_head + s->history_count) % WEBTERM_HISTORY_FRAMES;
    if (s->history_count < WEBTERM_HISTORY_FRAMES) {
        s->history_count++;
    } else {
        /* Ring full — drop oldest by advancing head */
        s->history_head = (s->history_head + 1) % WEBTERM_HISTORY_FRAMES;
    }
    size_t copy = len < WEBTERM_HISTORY_TEXT_MAX - 1 ? len : WEBTERM_HISTORY_TEXT_MAX - 1;
    memcpy(s->history_text[slot], text, copy);
    s->history_text[slot][copy] = '\0';
    s->history_text_lens[slot] = copy;
    s->history_prompt[slot] = prompt;
}

/**
 * Encode a history entry as a JSON output frame and send to the client.
 * Used only during history replay on reconnect.
 */
static void send_history_entry(webterm_session_t *s, int idx) {
    char *buf = s->json_buf;
    size_t max = WEBTERM_JSON_BUF_SIZE;
    size_t pos = 0;

    const char *prefix = "{\"type\":\"output\",\"text\":\"";
    size_t plen = strlen(prefix);
    memcpy(buf + pos, prefix, plen);
    pos += plen;

    pos += json_escape(s->history_text[idx], s->history_text_lens[idx],
                       buf + pos, max - pos - 32);

    if (s->history_prompt[idx]) {
        const char *suffix = "\",\"prompt\":true}";
        size_t slen = strlen(suffix);
        memcpy(buf + pos, suffix, slen);
        pos += slen;
    } else {
        buf[pos++] = '"';
        buf[pos++] = '}';
    }

    s->platform->send(s->platform, buf, pos);
}

/**
 * Build the output JSON frame, send it to the client, and record raw text
 * in history. Use for output frames that a reconnecting client should see.
 */
static void send_output_frame(webterm_session_t *s, const char *json, size_t len,
                               const char *raw_text, size_t raw_len, int prompt) {
    history_push(s, raw_text, raw_len, prompt);
    if (s->client_connected) {
        s->platform->send(s->platform, json, len);
    }
}

/**
 * Send a JSON frame directly without storing in history.
 * Use for ephemeral frames: status, saved confirmation, errors, busy, resumed.
 */
static void send_ephemeral(webterm_session_t *s, const char *json) {
    if (s->client_connected) {
        s->platform->send(s->platform, json, strlen(json));
    }
}

/**
 * Flush the accumulated output buffer as a single JSON output frame.
 * If @p prompt is non-zero, appends "prompt":true.
 * Clears the output buffer regardless of whether anything was sent.
 */
static void flush_output(webterm_session_t *s, int prompt) {
    char *buf = s->json_buf;
    size_t max = WEBTERM_JSON_BUF_SIZE;
    size_t pos = 0;

    /* Frame prefix */
    const char *prefix = "{\"type\":\"output\",\"text\":\"";
    size_t plen = strlen(prefix);
    memcpy(buf + pos, prefix, plen);
    pos += plen;

    /* Escaped text body */
    pos += json_escape(s->out_buf, s->out_len, buf + pos, max - pos - 32);

    /* Frame suffix */
    if (prompt) {
        const char *suffix = "\",\"prompt\":true}";
        size_t slen = strlen(suffix);
        memcpy(buf + pos, suffix, slen);
        pos += slen;
    } else {
        buf[pos++] = '"';
        buf[pos++] = '}';
    }

    send_output_frame(s, buf, pos, s->out_buf, s->out_len, prompt);
    s->out_len = 0;
}

/* -------------------------------------------------------------------------
 * zvibe callbacks
 * ---------------------------------------------------------------------- */

static void output_cb(const char *text, size_t len) {
    webterm_session_t *s = g_session;
    if (!s) return;

    size_t space = WEBTERM_OUT_BUF_SIZE - s->out_len;
    size_t copy  = len < space ? len : space;
    memcpy(s->out_buf + s->out_len, text, copy);
    s->out_len += copy;
    /* Silent truncation on overflow — pathological for v3 games */
}

#if ZVIBE_ENABLE_STATUS_LINE
static void status_cb(const zvibeStatus *st) {
    webterm_session_t *s = g_session;
    if (!s || !s->client_connected) return;

    char buf[256];
    size_t len;

    if (st->is_time) {
        len = (size_t)snprintf(buf, sizeof(buf),
            "{\"type\":\"status\",\"location\":\"%s\","
            "\"hours\":%d,\"minutes\":%d}",
            st->location, st->hours, st->minutes);
    } else {
        len = (size_t)snprintf(buf, sizeof(buf),
            "{\"type\":\"status\",\"location\":\"%s\","
            "\"score\":%d,\"turns\":%d}",
            st->location, st->score, st->turns);
    }

    /* Status is ephemeral — not stored in history */
    s->platform->send(s->platform, buf, len);
}
#endif

/* -------------------------------------------------------------------------
 * Internal zvibe context initialisation
 * ---------------------------------------------------------------------- */

static zvibeContext *create_zvibe(void) {
    zvibeContext *ctx = zvibe_create(output_cb, stub_input);
    if (!ctx) return NULL;

#if ZVIBE_ENABLE_STATUS_LINE
    zvibe_set_status_callback(ctx, status_cb);
#endif

    /* Fixed seed for deterministic test output.
     * Platforms wanting randomness should call zvibe_set_random_seed()
     * after webterm_session_create(). */
    zvibe_set_random_seed(ctx, 1);
    return ctx;
}

/* -------------------------------------------------------------------------
 * Lifecycle
 * ---------------------------------------------------------------------- */

webterm_session_t *webterm_session_create(webterm_platform_t *platform) {
    if (!platform || !platform->send) return NULL;

    webterm_session_t *s = (webterm_session_t *)calloc(1, sizeof(*s));
    if (!s) return NULL;

    s->platform = platform;
    s->state    = WEBTERM_STATE_IDLE;

    s->zvibe = create_zvibe();
    if (!s->zvibe) {
        free(s);
        return NULL;
    }

    g_session = s;
    return s;
}

void webterm_session_destroy(webterm_session_t *s) {
    if (!s) return;
    if (g_session == s) g_session = NULL;
    zvibe_destroy(s->zvibe);
    free(s);
}

void webterm_session_set_random_seed(webterm_session_t *s, int seed) {
    if (s && s->zvibe) zvibe_set_random_seed(s->zvibe, seed);
}

/* -------------------------------------------------------------------------
 * Connection management
 * ---------------------------------------------------------------------- */

int webterm_session_accept(webterm_session_t *s) {
    if (!s) return 0;

    if (s->client_connected) {
        /* Another client is active — send busy frame before caller closes */
        s->platform->send(s->platform,
            "{\"type\":\"busy\"}", sizeof("{\"type\":\"busy\"}") - 1);
        return 0;
    }

    s->client_connected = 1;

    if (s->state == WEBTERM_STATE_PAUSED ||
        s->state == WEBTERM_STATE_WAITING) {

        /* Signal reconnection */
        int prompt = (s->state == WEBTERM_STATE_WAITING);
        const char *hdr = prompt
            ? "{\"type\":\"resumed\",\"prompt\":true}"
            : "{\"type\":\"resumed\"}";
        s->platform->send(s->platform, hdr, strlen(hdr));

        /* Replay output history oldest-first, re-encoding each entry as JSON */
        for (int i = 0; i < s->history_count; i++) {
            int idx = (s->history_head + i) % WEBTERM_HISTORY_FRAMES;
            send_history_entry(s, idx);
        }

        /* Restore running state for the reconnected client */
        if (s->state == WEBTERM_STATE_PAUSED) {
            s->state = WEBTERM_STATE_WAITING;
        }
    }

    return 1;
}

void webterm_session_disconnect(webterm_session_t *s) {
    if (!s) return;
    s->client_connected = 0;
    if (s->state == WEBTERM_STATE_RUNNING ||
        s->state == WEBTERM_STATE_WAITING) {
        s->state = WEBTERM_STATE_PAUSED;
    }
}

/* -------------------------------------------------------------------------
 * Game loading
 * ---------------------------------------------------------------------- */

webterm_result_t webterm_session_load(webterm_session_t *s,
                                      const uint8_t *data, size_t len) {
    if (!s || !data || len == 0) return WEBTERM_ERROR;

    /* Re-initialise zvibe for the new game */
    zvibe_destroy(s->zvibe);
    s->zvibe = create_zvibe();
    if (!s->zvibe) return WEBTERM_ERROR;

    /* Reset session state */
    s->out_len       = 0;
    s->history_head  = 0;
    s->history_count = 0;
    s->input_pending = 0;
    memset(s->history_prompt, 0, sizeof(s->history_prompt));

    g_session = s;

    if (zvibe_load_story_from_memory(s->zvibe, data, len) != ZVIBE_OK) {
        return WEBTERM_ERROR;
    }

    s->state = WEBTERM_STATE_RUNNING;
    return WEBTERM_OK;
}

/* -------------------------------------------------------------------------
 * Run loop
 * ---------------------------------------------------------------------- */

webterm_result_t webterm_session_run(webterm_session_t *s) {
    if (!s) return WEBTERM_ERROR;

    switch (s->state) {
        case WEBTERM_STATE_IDLE:
        case WEBTERM_STATE_DONE:
            return WEBTERM_DONE;

        case WEBTERM_STATE_PAUSED:
            return WEBTERM_DONE;  /* caller should not run a paused session */

        case WEBTERM_STATE_WAITING:
            if (!s->input_pending) return WEBTERM_WAIT;
            s->input_pending = 0;
            s->state = WEBTERM_STATE_RUNNING;
            break;

        case WEBTERM_STATE_RUNNING:
            break;
    }

    g_session = s;
    zvibeResult r = zvibe_run(s->zvibe);

    switch (r) {
        case ZVIBE_WAIT_FOR_INPUT:
            flush_output(s, 1 /* prompt */);
            s->state = WEBTERM_STATE_WAITING;
            return WEBTERM_WAIT;

        case ZVIBE_GAME_FINISHED:
            flush_output(s, 0);
            s->state = WEBTERM_STATE_DONE;
            return WEBTERM_DONE;

#if ZVIBE_ENABLE_SAVE_RESTORE
        case ZVIBE_SAVE_REQUESTED: {
            int ok = 0;
            if (s->platform->save) {
                size_t save_size = zvibe_get_save_size(s->zvibe);
                if (save_size > 0 && save_size <= WEBTERM_SAVE_BUF_MAX) {
                    size_t written = zvibe_get_save_data(s->zvibe,
                                                         s->save_buf, save_size);
                    if (written > 0) {
                        ok = s->platform->save(s->platform, s->save_buf, written);
                    }
                }
            }
            zvibe_save_completed(s->zvibe, ok);
            send_ephemeral(s, ok
                ? "{\"type\":\"saved\"}"
                : "{\"type\":\"error\",\"message\":\"Save failed\"}");
            return WEBTERM_OK;
        }

        case ZVIBE_RESTORE_REQUESTED: {
            int ok = 0;
            if (s->platform->restore) {
                size_t bytes = s->platform->restore(s->platform,
                                                     s->save_buf, WEBTERM_SAVE_BUF_MAX);
                if (bytes > 0) {
                    ok = (zvibe_restore_data(s->zvibe, s->save_buf, bytes) == ZVIBE_OK);
                }
            }
            zvibe_restore_completed(s->zvibe, ok);
            if (!ok) {
                send_ephemeral(s,
                    "{\"type\":\"error\",\"message\":\"No save found\"}");
            }
            return WEBTERM_OK;
        }

        case ZVIBE_RESTART_REQUESTED:
            s->out_len = 0;  /* discard buffered output before restart */
            zvibe_restart_completed(s->zvibe);
            return WEBTERM_OK;
#endif

        case ZVIBE_OK:
            return WEBTERM_OK;

        case ZVIBE_ERROR:
        default:
            s->state = WEBTERM_STATE_DONE;
            return WEBTERM_ERROR;
    }
}

/* -------------------------------------------------------------------------
 * Input events
 * ---------------------------------------------------------------------- */

void webterm_session_on_input(webterm_session_t *s,
                               const char *text, size_t len) {
    if (!s || !text) return;
    if (s->state != WEBTERM_STATE_WAITING) return;

    if (len > WEBTERM_INPUT_MAX) len = WEBTERM_INPUT_MAX;

    char buf[WEBTERM_INPUT_MAX + 1];
    memcpy(buf, text, len);
    buf[len] = '\0';

    zvibe_input(s->zvibe, buf);
    s->input_pending = 1;
    s->state = WEBTERM_STATE_RUNNING;
}

void webterm_session_on_save(webterm_session_t *s) {
    /* Inject "save" as player input — the game triggers ZVIBE_SAVE_REQUESTED */
    webterm_session_on_input(s, "save", 4);
}

void webterm_session_on_restore(webterm_session_t *s) {
    webterm_session_on_input(s, "restore", 7);
}

void webterm_session_on_reset(webterm_session_t *s) {
    if (!s) return;

    s->out_len       = 0;
    s->history_head  = 0;
    s->history_count = 0;
    s->input_pending = 0;
    s->state         = WEBTERM_STATE_IDLE;

    zvibe_destroy(s->zvibe);
    s->zvibe = create_zvibe();
    g_session = s;
}

/* -------------------------------------------------------------------------
 * Diagnostics
 * ---------------------------------------------------------------------- */

webterm_state_t webterm_session_state(const webterm_session_t *s) {
    return s ? s->state : WEBTERM_STATE_IDLE;
}
