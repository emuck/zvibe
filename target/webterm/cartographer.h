/* Copyright (c) 2025 Martin R. Raumann */
/* SPDX-License-Identifier: BSD-3-Clause */
/**
 * @file cartographer.h
 * @brief Turn-boundary room-change event emitter for the webterm target.
 *
 * See CARTOGRAPHER.md for the full integration spec. This half of the
 * bridge is deliberately tiny: it observes global[0] changes (via
 * zvibe_get_room()) and emits one JSON message type, {"type":"room"}.
 * All map logic — placement, edges, rendering — lives in cartographer.js.
 *
 * Unlike the C hook sketched in CARTOGRAPHER.md, this is called from
 * webterm_session.c rather than the shared interpreter core, so it needs
 * no TARGET_WEBTERM compile guard: webterm_session.c is itself only ever
 * built into webterm-based platforms.
 */

#ifndef CARTOGRAPHER_H
#define CARTOGRAPHER_H

#include <stdint.h>
#include "webterm_platform.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Emit a {"type":"room"} event over the webterm channel.
 *
 * Call whenever the player's location (global[0]) is found to have
 * changed after a turn completes.
 *
 * @param platform   the active webterm platform; used only to send the frame
 * @param cmd        the raw input line that produced this turn, or NULL if
 *                   the move was not player-initiated (opening placement,
 *                   a restore, etc.)
 * @param room_id    the Z-machine object number of the new location
 * @param room_name  decoded short name of the new location (NULL treated
 *                   as empty string)
 */
void cart_room_changed(webterm_platform_t *platform, const char *cmd,
                        uint16_t room_id, const char *room_name);

#ifdef __cplusplus
}
#endif

#endif /* CARTOGRAPHER_H */
