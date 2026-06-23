/* Copyright (c) 2025 Martin R. Raumann */
/* SPDX-License-Identifier: BSD-3-Clause */
/**
 * @file webterm_session.h
 * @brief Platform-agnostic Z-machine web terminal session.
 *
 * A session owns one zvibe context and mediates between the Z-machine's
 * callback-driven I/O and the WebSocket JSON protocol defined in PROTOCOL.md.
 *
 * ## Usage
 *
 * One session per device. Create it once at startup:
 *
 * @code
 * webterm_session_t *s = webterm_session_create(&my_platform);
 * @endcode
 *
 * When a WebSocket client connects:
 *
 * @code
 * if (!webterm_session_accept(s)) {
 *     // session busy — send {"type":"busy"} and close the connection
 * }
 * @endcode
 *
 * When frames arrive from the client, dispatch to the appropriate function.
 * When the client disconnects:
 *
 * @code
 * webterm_session_disconnect(s);
 * @endcode
 *
 * The session is then paused. The next client to connect and call
 * webterm_session_accept() will receive a "resumed" frame with history.
 *
 * ## Threading
 *
 * The session is not thread-safe. All calls must be serialised by the
 * platform (e.g. from a single FreeRTOS task, or under a mutex).
 * The session never blocks indefinitely — if input is needed and none is
 * queued, webterm_session_run() returns WEBTERM_WAIT.
 */

#ifndef WEBTERM_SESSION_H
#define WEBTERM_SESSION_H

#include <stddef.h>
#include <stdint.h>
#include "webterm_platform.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Maximum length of a single line of player input (bytes, excluding NUL). */
#ifndef WEBTERM_INPUT_MAX
#define WEBTERM_INPUT_MAX 256
#endif

/**
 * Number of output turns kept in the history ring buffer.
 * Used to replay recent context when a client reconnects.
 */
#ifndef WEBTERM_HISTORY_FRAMES
#define WEBTERM_HISTORY_FRAMES 50
#endif

/**
 * Maximum raw text bytes stored per history entry.
 * History stores raw Z-machine output text, not JSON frames.
 * JSON encoding happens on replay, so this is the unescaped byte limit.
 * 2048 bytes covers the longest typical Z-machine v3 turn response.
 */
#ifndef WEBTERM_HISTORY_TEXT_MAX
#define WEBTERM_HISTORY_TEXT_MAX 2048
#endif

/** Opaque session type. */
typedef struct webterm_session_t webterm_session_t;

/**
 * @brief Return codes from webterm_session_run().
 */
typedef enum {
    WEBTERM_OK      = 0,  /**< Step completed normally, call again.          */
    WEBTERM_WAIT    = 1,  /**< Waiting for input — do not call run() again
                               until webterm_session_on_input() is called.   */
    WEBTERM_DONE    = 2,  /**< Game finished or session reset.               */
    WEBTERM_ERROR   = 3,  /**< Fatal error — session should be destroyed.    */
} webterm_result_t;

/* -------------------------------------------------------------------------
 * Lifecycle
 * ---------------------------------------------------------------------- */

/**
 * @brief Create a new session.
 *
 * Allocates and initialises a session bound to @p platform. No game is
 * loaded; the session begins in IDLE state.
 *
 * @param platform  Platform implementation. Must remain valid for the
 *                  lifetime of the session.
 * @return          New session, or NULL on allocation failure.
 */
webterm_session_t *webterm_session_create(webterm_platform_t *platform);

/**
 * @brief Destroy a session and free all resources.
 *
 * Safe to call from any session state. Any connected client will stop
 * receiving output but is not explicitly disconnected — the platform
 * handles connection lifecycle.
 */
void webterm_session_destroy(webterm_session_t *s);

/* -------------------------------------------------------------------------
 * Connection management
 * ---------------------------------------------------------------------- */

/**
 * @brief Attempt to accept a new WebSocket client.
 *
 * Call this when a new WebSocket connection is established.
 *
 * - If no client is currently active, the connection is accepted. If a game
 *   is paused, a "resumed" frame with history is sent immediately.
 *   If the session is IDLE, a "games" list request should follow from the
 *   client (the platform handles that — the session does not know about the
 *   game catalogue).
 *
 * - If a client is already active, returns 0. The caller should send
 *   @c {"type":"busy"} and close the incoming connection.
 *
 * @return 1 if accepted, 0 if busy.
 */
int webterm_session_accept(webterm_session_t *s);

/**
 * @brief Notify the session that the current client has disconnected.
 *
 * Transitions a RUNNING session to PAUSED. IDLE sessions remain IDLE.
 * Game state is preserved. The session is ready to accept the next client.
 */
void webterm_session_disconnect(webterm_session_t *s);

/* -------------------------------------------------------------------------
 * Game loading
 * ---------------------------------------------------------------------- */

/**
 * @brief Load a Z-machine story file and start a new game.
 *
 * Abandons any game currently in progress. The zvibe context is
 * reinitialised with the new story. The opening text is emitted via the
 * platform's send callback.
 *
 * @param s         The session.
 * @param data      Story file bytes. The session does not take ownership —
 *                  the caller must keep this buffer valid for the duration
 *                  of the game. (For ROM / flash XIP targets, this is a
 *                  direct pointer into storage.)
 * @param len       Length of @p data in bytes.
 * @return          WEBTERM_OK on success, WEBTERM_ERROR if the story cannot
 *                  be loaded (bad header, unsupported version, etc.).
 */
webterm_result_t webterm_session_load(webterm_session_t *s,
                                      const uint8_t *data, size_t len);

/* -------------------------------------------------------------------------
 * Running the interpreter
 * ---------------------------------------------------------------------- */

/**
 * @brief Advance the Z-machine by one step.
 *
 * Executes opcodes until the Z-machine yields — either because it is
 * waiting for player input (WEBTERM_WAIT) or the game has ended
 * (WEBTERM_DONE).
 *
 * The platform's run loop should call this in a tight loop until WAIT or
 * DONE, then yield back to the scheduler:
 *
 * @code
 * webterm_result_t r;
 * do { r = webterm_session_run(s); } while (r == WEBTERM_OK);
 * @endcode
 *
 * WEBTERM_OK is returned between opcode bursts to allow the caller to check
 * for incoming messages or yield CPU if needed. The platform controls the
 * granularity of this yield.
 *
 * Do not call run() again after WEBTERM_WAIT until webterm_session_on_input()
 * has been called.
 *
 * @return WEBTERM_OK, WEBTERM_WAIT, WEBTERM_DONE, or WEBTERM_ERROR.
 */
webterm_result_t webterm_session_run(webterm_session_t *s);

/* -------------------------------------------------------------------------
 * Input events (call from WebSocket frame handler)
 * ---------------------------------------------------------------------- */

/**
 * @brief Deliver a line of player input to the session.
 *
 * Call this when a @c {"type":"input"} frame arrives. The input is queued
 * and will be consumed on the next call to webterm_session_run().
 *
 * Input longer than WEBTERM_INPUT_MAX is silently truncated.
 * Input delivered when no game is loaded is silently ignored.
 *
 * @param s     The session.
 * @param text  Player's input text (need not be null-terminated).
 * @param len   Length of @p text in bytes.
 */
void webterm_session_on_input(webterm_session_t *s,
                               const char *text, size_t len);

/**
 * @brief Request a save operation.
 *
 * Call this when a @c {"type":"save"} frame arrives. If the platform's
 * save callback is NULL, an error frame is sent immediately.
 */
void webterm_session_on_save(webterm_session_t *s);

/**
 * @brief Request a restore operation.
 *
 * Call this when a @c {"type":"restore"} frame arrives. If the platform's
 * restore callback is NULL or no save data exists, an error frame is sent.
 */
void webterm_session_on_restore(webterm_session_t *s);

/**
 * @brief Reset the session to IDLE state.
 *
 * Call this when a @c {"type":"reset"} frame arrives. Abandons the current
 * game. The client should present the game picker after receiving this call's
 * effects (the session sends no explicit "reset confirmed" frame — the
 * absence of further game output is the signal).
 */
void webterm_session_on_reset(webterm_session_t *s);

/* -------------------------------------------------------------------------
 * Configuration
 * ---------------------------------------------------------------------- */

/**
 * @brief Seed the Z-machine random number generator.
 *
 * Call after webterm_session_create() to override the default seed (1).
 * Passing @p seed = 0 seeds from the system clock.
 *
 * The default seed of 1 is chosen for deterministic test output.
 * Production deployments should call this with a time-derived seed.
 *
 * @param s     The session.
 * @param seed  Seed value; 0 = use system time.
 */
void webterm_session_set_random_seed(webterm_session_t *s, int seed);

/* -------------------------------------------------------------------------
 * Diagnostics
 * ---------------------------------------------------------------------- */

/**
 * @brief Session state, for diagnostics and platform polling loops.
 */
typedef enum {
    WEBTERM_STATE_IDLE    = 0,  /**< No game loaded.                        */
    WEBTERM_STATE_RUNNING = 1,  /**< Game running, client connected.        */
    WEBTERM_STATE_WAITING = 2,  /**< Running, blocked waiting for input.    */
    WEBTERM_STATE_PAUSED  = 3,  /**< Game loaded, no client connected.      */
    WEBTERM_STATE_DONE    = 4,  /**< Game finished, awaiting load or reset. */
} webterm_state_t;

/** @brief Return the current session state. */
webterm_state_t webterm_session_state(const webterm_session_t *s);

#ifdef __cplusplus
}
#endif

#endif /* WEBTERM_SESSION_H */
