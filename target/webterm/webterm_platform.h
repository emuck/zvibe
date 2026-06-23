/* Copyright (c) 2025 Martin R. Raumann */
/* SPDX-License-Identifier: BSD-3-Clause */
/**
 * @file webterm_platform.h
 * @brief Platform interface for the webterm Z-machine web terminal target.
 *
 * A platform is any hardware or OS environment that can:
 *   - Accept WebSocket connections from a browser
 *   - Send JSON text frames to a connected client
 *   - Optionally persist save game data
 *
 * To port webterm to a new platform, implement this interface and wire
 * the session API (webterm_session.h) to your networking stack.
 * See README.md for a step-by-step checklist.
 */

#ifndef WEBTERM_PLATFORM_H
#define WEBTERM_PLATFORM_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Opaque platform context.
 *
 * Each platform defines its own struct that begins with this header.
 * The session layer holds a pointer to webterm_platform_t and passes it
 * back to all platform callbacks — similar to a vtable + self pointer.
 *
 * Example:
 * @code
 * typedef struct {
 *     webterm_platform_t base;   // must be first
 *     esp_websocket_handle_t ws;
 *     nvs_handle_t nvs;
 * } my_esp32_platform_t;
 * @endcode
 */
typedef struct webterm_platform_t webterm_platform_t;

struct webterm_platform_t {
    /**
     * @brief Send a JSON frame to the connected client.
     *
     * Called by the session whenever it has output to deliver — game text,
     * status updates, session state changes, errors.
     *
     * @param platform  The platform context.
     * @param json      UTF-8 JSON string. Not null-terminated — use @p len.
     * @param len       Length of @p json in bytes.
     *
     * If no client is currently connected, drop the frame silently. The
     * session does not buffer unsent frames; the history ring buffer handles
     * reconnect replay at a higher level.
     *
     * This function may be called from any context the platform chooses to
     * run the session in (interrupt, task, thread). The platform is
     * responsible for any locking required by its networking stack.
     */
    void (*send)(webterm_platform_t *platform, const char *json, size_t len);

    /**
     * @brief Persist Z-machine save data. (Optional — may be NULL.)
     *
     * Called when the Z-machine executes a SAVE instruction and the player
     * has confirmed. The platform should write @p data to durable storage
     * (NVS, flash, filesystem, etc.).
     *
     * Only one save slot is required. Overwriting a previous save is fine.
     *
     * @param platform  The platform context.
     * @param data      Raw save state bytes (interpreter-managed format).
     * @param len       Number of bytes to persist.
     * @return          1 on success, 0 on failure.
     */
    int (*save)(webterm_platform_t *platform, const void *data, size_t len);

    /**
     * @brief Restore Z-machine save data. (Optional — may be NULL.)
     *
     * Called when the Z-machine executes a RESTORE instruction. The platform
     * should read the previously saved bytes into @p buf.
     *
     * @param platform  The platform context.
     * @param buf       Destination buffer.
     * @param max_len   Maximum number of bytes to read.
     * @return          Number of bytes read, or 0 if no save data exists.
     */
    size_t (*restore)(webterm_platform_t *platform, void *buf, size_t max_len);
};

#ifdef __cplusplus
}
#endif

#endif /* WEBTERM_PLATFORM_H */
