/* Copyright (c) 2025 Martin R. Raumann */
/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * menu_system.h
 *
 * Shared multi-game menu system for ZVibe
 * Platform-independent menu logic used by both SAM E51 and RISC-V targets
 */

#ifndef MENU_SYSTEM_H
#define MENU_SYSTEM_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* Game registry entry structure (shared by all platforms) */
typedef struct {
    const char* name;           /* Display name of the game */
    const uint8_t* data;        /* Pointer to game data array */
    size_t size;                /* Size of game data in bytes */
} game_entry_t;

/* Menu state and functions */

/**
 * Parse menu input and validate game selection
 *
 * Args:
 *   input: User input string
 *   num_games: Total number of available games
 *   selected_index: Output parameter for selected game index (0-based)
 *
 * Returns:
 *   true if valid game selected, false otherwise
 */
bool menu_parse_input(const char *input, int num_games, int *selected_index);

/**
 * Format menu header text
 *
 * Args:
 *   buffer: Output buffer for formatted text
 *   buffer_size: Size of output buffer
 *
 * Returns:
 *   Number of bytes written to buffer
 */
size_t menu_format_header(char *buffer, size_t buffer_size);

/**
 * Format menu entry text for a game
 *
 * Args:
 *   buffer: Output buffer for formatted text
 *   buffer_size: Size of output buffer
 *   game_number: Game number (1-based) for display
 *   game_name: Name of the game
 *
 * Returns:
 *   Number of bytes written to buffer
 */
size_t menu_format_entry(char *buffer, size_t buffer_size, int game_number, const char *game_name);

/**
 * Format menu prompt text
 *
 * Args:
 *   buffer: Output buffer for formatted text
 *   buffer_size: Size of output buffer
 *   num_games: Total number of games
 *
 * Returns:
 *   Number of bytes written to buffer
 */
size_t menu_format_prompt(char *buffer, size_t buffer_size, int num_games);

#endif /* MENU_SYSTEM_H */
