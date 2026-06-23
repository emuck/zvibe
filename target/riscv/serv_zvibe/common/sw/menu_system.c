/* Copyright (c) 2025 Martin R. Raumann */
/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * menu_system.c
 *
 * Shared multi-game menu system for ZVibe
 * Platform-independent menu logic used by both SAM E51 and RISC-V targets
 */

#include "menu_system.h"
#include <string.h>

// Declared as external - implemented in platform stubs
extern int snprintf(char *str, size_t size, const char *format, ...);

// Simple inline ctype replacements (avoid dependency on C library)
static inline int is_space(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

static inline int is_digit(char c) {
    return c >= '0' && c <= '9';
}

bool menu_parse_input(const char *input, int num_games, int *selected_index) {
    if (!input || !selected_index || num_games <= 0) {
        return false;
    }

    // Skip leading whitespace
    while (*input && is_space(*input)) {
        input++;
    }

    // Parse multi-digit number
    if (*input && is_digit(*input)) {
        int game_num = 0;

        // Parse all consecutive digits
        while (*input && is_digit(*input)) {
            game_num = game_num * 10 + (*input - '0');
            input++;
        }

        // Skip trailing whitespace
        while (*input && is_space(*input)) {
            input++;
        }

        // Should be end of string for valid input
        if (*input == '\0') {
            // Validate range (1-based input, 0-based index)
            if (game_num >= 1 && game_num <= num_games) {
                *selected_index = game_num - 1;
                return true;
            }
        }
    }

    return false;
}

size_t menu_format_header(char *buffer, size_t buffer_size) {
    if (!buffer || buffer_size == 0) {
        return 0;
    }

    const char *header = "\r\n\r\nZ-Machine Game Library\r\n"
                        "======================\r\n";

    size_t len = strlen(header);
    if (len >= buffer_size) {
        len = buffer_size - 1;
    }

    memcpy(buffer, header, len);
    buffer[len] = '\0';

    return len;
}

size_t menu_format_entry(char *buffer, size_t buffer_size, int game_number, const char *game_name) {
    if (!buffer || buffer_size == 0 || !game_name) {
        return 0;
    }

    return snprintf(buffer, buffer_size, "%d. %s\r\n", game_number, game_name);
}

size_t menu_format_prompt(char *buffer, size_t buffer_size, int num_games) {
    if (!buffer || buffer_size == 0) {
        return 0;
    }

    return snprintf(buffer, buffer_size, "\r\nEnter game number (1-%d): ", num_games);
}
