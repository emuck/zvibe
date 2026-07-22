/* Copyright (c) 2025 Martin R. Raumann */
/* SPDX-License-Identifier: BSD-3-Clause */
/**
 * @file cartographer.c
 * @brief cart_room_changed() — see cartographer.h.
 */

#include "cartographer.h"

#include <stdio.h>
#include <string.h>

#define CART_BUF_SIZE 3072

/* Append a null-terminated string to buf, truncating rather than
 * overflowing. Returns the new write position. */
static size_t cart_append(char *buf, size_t pos, size_t max, const char *s) {
    size_t n = strlen(s);
    size_t room = (max > pos) ? max - pos - 1 : 0;
    if (n > room) n = room;
    memcpy(buf + pos, s, n);
    return pos + n;
}

/* JSON-escape a null-terminated string into out (no surrounding quotes),
 * writing at most out_max bytes. Returns the number of bytes written. */
static size_t cart_json_escape(const char *in, char *out, size_t out_max) {
    size_t pos = 0;
    if (!in) return 0;
    for (size_t i = 0; in[i] != '\0'; i++) {
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

void cart_room_changed(webterm_platform_t *platform, const char *cmd,
                        uint16_t room_id, const char *room_name) {
    if (!platform || !platform->send) return;

    char buf[CART_BUF_SIZE];
    size_t pos = 0;

    pos = cart_append(buf, pos, sizeof(buf), "{\"type\":\"room\",\"cmd\":");

    if (cmd) {
        pos = cart_append(buf, pos, sizeof(buf), "\"");
        pos += cart_json_escape(cmd, buf + pos, sizeof(buf) - pos);
        pos = cart_append(buf, pos, sizeof(buf), "\"");
    } else {
        pos = cart_append(buf, pos, sizeof(buf), "null");
    }

    char idbuf[24];
    snprintf(idbuf, sizeof(idbuf), ",\"id\":%u,\"name\":\"", (unsigned)room_id);
    pos = cart_append(buf, pos, sizeof(buf), idbuf);
    pos += cart_json_escape(room_name, buf + pos, sizeof(buf) - pos);
    pos = cart_append(buf, pos, sizeof(buf), "\"}");

    platform->send(platform, buf, pos);
}
