/* Copyright (c) 2025 Martin R. Raumann */
/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * zvibe_riscv_multi.c
 *
 * ZVibe Z-machine interpreter for RISC-V SERV with XIP
 * Multi-game menu system - games loaded from flash TOC
 *
 * Features:
 * - Interactive game selection menu
 * - Multiple games stored in flash with Table of Contents
 * - Execute-In-Place (XIP) from QSPI flash
 * - Code and game data in flash @ 0x80100000
 * - Dynamic memory (~8KB) in RAM
 * - Stack (1KB) in RAM
 */

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdarg.h>
#include <stdbool.h>

// Forward declarations for stub functions
int snprintf(char *str, size_t size, const char *format, ...);

// ZVibe API and internals (need G for delta saves)
#include "../../../../../include/zvibe_api.h"
#include "../../../../../include/zvibe.h"

// Menu system and game registry
#include "menu_system.h"
#include "game_registry_riscv.h"

// Flash metadata (dynamic layout discovery)
#include "flash_metadata.h"

// Flash save system with delta compression
#include "flash_save.h"
#include "save_delta.h"

//=============================================================================
// Hardware Definitions
//=============================================================================

#define UART_BASE     0x40000000
#define GPIO_BASE     0x40000020

// UART registers (Wishbone interface)
#define UART_TX_DATA  (*(volatile uint32_t *)(UART_BASE + 0x00))
#define UART_RX_DATA  (*(volatile uint32_t *)(UART_BASE + 0x04))
#define UART_STATUS   (*(volatile uint32_t *)(UART_BASE + 0x08))

#define TX_READY      (1 << 0)
#define RX_AVAILABLE  (1 << 1)

// GPIO LEDs
#define GPIO_LEDS     (*(volatile uint32_t *)(GPIO_BASE + 0x00))

// Current game state for save system
static int current_game_index = -1;
static const uint8_t *current_game_data = NULL;  // Original game data in flash

// Delta save buffer — 2KB matches the UFM slot size (SLOT_SIZE = 2048)
// Typical delta saves are 500-2000 bytes vs 14KB+ for full saves
#define DELTA_BUFFER_SIZE 2048
static uint8_t delta_buffer[DELTA_BUFFER_SIZE];

//=============================================================================
// UART I/O Functions
//=============================================================================

// UART output (blocking with TX_READY polling - REQUIRED for hardware)
static void uart_putc(char c) {
    while (!(UART_STATUS & TX_READY));
    UART_TX_DATA = c;
}

static void uart_puts(const char *s) {
    while (*s) {
        if (*s == '\n') uart_putc('\r');  // LF -> CRLF for terminal
        uart_putc(*s++);
    }
}

// Simple hex print for debugging
static void uart_puthex(uint32_t val, int digits) {
    for (int i = digits - 1; i >= 0; i--) {
        uint32_t nibble = (val >> (i * 4)) & 0xF;
        char c = (nibble < 10) ? ('0' + nibble) : ('A' + nibble - 10);
        uart_putc(c);
    }
}

// UART input (blocking, with line editing)
static size_t uart_getline(char *buffer, size_t max_length) {
    size_t pos = 0;

    while (pos < max_length - 1) {
        // Wait for character
        while (!(UART_STATUS & RX_AVAILABLE));
        char c = (char)(UART_RX_DATA & 0xFF);

        // Handle special characters
        if (c == '\r' || c == '\n') {
            uart_puts("\r\n");
            buffer[pos] = '\0';
            return pos;
        } else if (c == '\b' || c == 127) {  // Backspace
            if (pos > 0) {
                pos--;
                uart_puts("\b \b");  // Erase character on screen
            }
        } else if (c >= 32 && c < 127) {  // Printable ASCII
            buffer[pos++] = c;
            uart_putc(c);  // Echo
        }
    }

    buffer[pos] = '\0';
    return pos;
}

//=============================================================================
// Menu System Functions
//=============================================================================

static void single_game_wait_for_return(void) {
    while (UART_STATUS & RX_AVAILABLE) { (void)(UART_RX_DATA & 0xFF); }
    uart_puts("\r\nPress RETURN to start...\r\n");
    /* Drain the full input line (all chars through CR/LF) so that test
     * scripts sending "~reset~\r" on a cold boot don't leave "reset~\r"
     * in the FIFO to be consumed as a spurious first game command. */
    while (1) {
        while (!(UART_STATUS & RX_AVAILABLE));
        uint8_t ch = (uint8_t)(UART_RX_DATA & 0xFF);
        if (ch == '\r' || ch == '\n') break;
    }
}

static void print_menu(void) {
    char buffer[128];

    // Print header
    menu_format_header(buffer, sizeof(buffer));
    uart_puts(buffer);

    // Print game entries
    for (int i = 0; i < num_games; i++) {
        const game_entry_t *game = get_game(i);
        if (game) {
            menu_format_entry(buffer, sizeof(buffer), i + 1, game->name);
            uart_puts(buffer);
        }
    }

    // Print prompt
    menu_format_prompt(buffer, sizeof(buffer), num_games);
    uart_puts(buffer);
}

//=============================================================================
// ZVibe Callbacks
//=============================================================================

// Output callback for ZVibe (called by interpreter for text output)
static void zvibe_output(const char *text, size_t length) {
    for (size_t i = 0; i < length; i++) {
        if (text[i] == '\n') uart_putc('\r');
        uart_putc(text[i]);
    }
}

//=============================================================================
// Delta Save/Restore Functions
//=============================================================================

// Save game using delta compression
// Compares current RAM against original flash data, stores only differences
static int do_delta_save(void) {
    if (current_game_index < 0 || !current_game_data || !G) {
        return 0;
    }

    // Get state from ZVibe internals
    const uint8_t *current_ram = G->memory_state.ram_buffer;
    size_t dynamic_size = G->memory_state.ram_size;
    uint32_t pc = (uint32_t)G->logical_pc;
    const uint16_t *stack = (const uint16_t *)G->stack_mem;
    size_t stack_words = (size_t)(G->stack_ptr - G->stack_mem);
    uint32_t base_ptr = (uint32_t)G->base_ptr;

    // Encode delta save
    size_t save_size = delta_encode(
        delta_buffer, DELTA_BUFFER_SIZE,
        current_ram, current_game_data, dynamic_size,
        pc, stack, stack_words, base_ptr
    );

    if (save_size == 0) {
        uart_puts("[SAVE overflow: delta too large]\r\n");
        return 0;
    }

    int result = flash_save_write(delta_buffer, save_size);
    return (result == SAVE_OK) ? 1 : 0;
}

// Restore game using delta compression
// Copies fresh data from flash, then applies delta patches
static int do_delta_restore(void) {
    if (current_game_index < 0 || !current_game_data || !G) {
        uart_puts("\r\n[Restore failed: no game loaded]\r\n");
        return 0;
    }

    // Check if save exists
    if (!flash_save_exists()) {
        uart_puts("\r\n[No saved game found]\r\n");
        return 0;
    }

    // Read delta save from flash
    int read_size = flash_save_read(delta_buffer, DELTA_BUFFER_SIZE);
    if (read_size <= 0) {
        uart_puts("\r\n[Restore failed: read error]\r\n");
        return 0;
    }

    // Decode and apply delta
    uint32_t pc;
    size_t stack_words;
    uint32_t base_ptr;

    int result = delta_decode(
        delta_buffer, (size_t)read_size,
        G->memory_state.ram_buffer,
        current_game_data,
        G->memory_state.ram_size,
        &pc, (uint16_t *)G->stack_mem, &stack_words, &base_ptr
    );

    if (result != 0) {
        uart_puts("\r\n[Restore failed: decode error ");
        uart_puthex(-result, 2);
        uart_puts("]\r\n");
        return 0;
    }

    // Update ZVibe state
    G->logical_pc = (zDWord)pc;
    G->stack_ptr = G->stack_mem + stack_words;
    G->base_ptr = (zWord)base_ptr;

    uart_puts("\r\n[Game restored]\r\n");
    return 1;
}

//=============================================================================
// Game Initialization
//=============================================================================

static bool init_zvibe_game(zvibeContext **ctx, int game_index) {
    // Get game from registry (XIP - no copying!)
    const game_entry_t *game = get_game(game_index);
    if (!game) {
        return false;
    }

    // Destroy existing context if any
    if (*ctx) {
        zvibe_destroy(*ctx);
        *ctx = NULL;
    }

    // Track current game for save/restore (including original data for delta compression)
    current_game_index = game_index;
    current_game_data = game->data;

    // Initialize save system for this game's per-game slot range
    {
        const flash_toc_t *toc = (const flash_toc_t *)
            (FLASH_XIP_BASE + g_flash_metadata->games_toc_offset);
        const flash_toc_entry_t *entry = &toc->entries[game_index];
        if (entry->saves_offset != 0 && entry->save_slot_count > 0) {
            flash_save_init(entry->saves_offset, (int)entry->save_slot_count);
        }
    }

    // Create new context
    *ctx = zvibe_create(zvibe_output);
    if (!*ctx) {
        uart_puts("ERROR: Failed to create ZVibe context\r\n");
        return false;
    }

    // Load game from flash (XIP - data pointer is direct flash address!)
    zvibeResult result = zvibe_load_story_from_memory(*ctx, game->data, game->size);

    if (result != ZVIBE_OK) {
        uart_puts("ERROR: Failed to load story (error ");
        uart_puthex(result, 2);
        uart_puts(")\r\n");
        zvibe_destroy(*ctx);
        *ctx = NULL;
        current_game_index = -1;
        current_game_data = NULL;
        return false;
    }

    // Show save status
    if (flash_save_exists()) {
        uart_puts("[Saved game available - type RESTORE to continue]\r\n\r\n");
    }

    return true;
}

//=============================================================================
// Main Application
//=============================================================================

int main(void) {
    zvibeContext *ctx = NULL;
    zvibeResult result;
    char input_buffer[256];
    bool in_menu_mode = true;

    // Startup message
    uart_puts("\r\n\r\n");
    uart_puts("\r\n");
    uart_puts("▄▖▖▖▘▌\r\n");   
    uart_puts("▗▘▌▌▌▛▌█▌\r\n");
    uart_puts("▙▖▚▘▌▙▌▙▖\r\n");
    uart_puts("\r\n");

    // Read flash metadata (dynamic layout discovery)
    if (!flash_metadata_init()) {
        uart_puts("FATAL: Invalid flash metadata\r\n");
        GPIO_LEDS = 0xF;
        while (1);
    }

    if (!load_game_registry_from_flash()) {
        uart_puts("ERROR: Failed to load game registry\r\n");
        GPIO_LEDS = 0xF;
        while (1);
    }

    // Auto-launch silently if only 1 game (no menu needed)
    if (num_games == 1) {
        single_game_wait_for_return();

        if (init_zvibe_game(&ctx, 0)) {
            in_menu_mode = false;
            GPIO_LEDS = 0x1;  // LED 0 on = game mode
        } else {
            uart_puts("ERROR: Failed to auto-launch game!\r\n");
            GPIO_LEDS = 0xF;  // All LEDs on for error
            while (1);
        }
    } else {
        // Multiple games - show menu
        print_menu();
    }

    // Main loop
    while (1) {
        if (in_menu_mode) {
            // Menu mode - wait for game selection
            uart_getline(input_buffer, sizeof(input_buffer));

            int selected_index;
            if (menu_parse_input(input_buffer, num_games, &selected_index)) {
                // Valid selection - initialize game
                if (init_zvibe_game(&ctx, selected_index)) {
                    in_menu_mode = false;
                    GPIO_LEDS = 0x1;  // LED 0 on = game mode
                } else {
                    // Failed to initialize - show menu again
                    uart_puts("Failed to load game. Please try again.\r\n");
                    print_menu();
                }
            } else {
                // Invalid input - show menu again
                print_menu();
            }

        } else {
            // Game mode - run interpreter
            GPIO_LEDS = 0x2;  // LED 1 on during execution

            result = zvibe_run(ctx);

            GPIO_LEDS = 0x1;  // LED 0 on waiting for input

            // Handle result
            if (result == ZVIBE_WAIT_FOR_INPUT) {
                // Get input from user
                uart_getline(input_buffer, sizeof(input_buffer));

                // ~reset~ : destroy and reinit game (never reaches Z-machine)
                if (input_buffer[0] == '~' && input_buffer[1] == 'r' &&
                    input_buffer[2] == 'e' && input_buffer[3] == 's' &&
                    input_buffer[4] == 'e' && input_buffer[5] == 't' &&
                    input_buffer[6] == '~' && input_buffer[7] == '\0') {
                    int game_idx = current_game_index;
                    zvibe_destroy(ctx);
                    ctx = NULL;
                    current_game_index = -1;
                    current_game_data = NULL;
                    uart_puts("\r\n[Game reset]\r\n\r\n");
                    if (game_idx >= 0 && init_zvibe_game(&ctx, game_idx)) {
                        GPIO_LEDS = 0x1;
                    } else {
                        GPIO_LEDS = 0xF;
                        while (1);
                    }
                    continue;
                }

                // Provide input to Z-machine
                result = zvibe_input(ctx, input_buffer);

                if (result != ZVIBE_OK) {
                    uart_puts("ERROR: zvibe_input failed\r\n");
                    zvibe_destroy(ctx);
                    ctx = NULL;
                    current_game_index = -1;
                    current_game_data = NULL;
                    if (num_games == 1) {
                        single_game_wait_for_return();
                        if (!init_zvibe_game(&ctx, 0)) {
                            GPIO_LEDS = 0xF;
                            while (1);
                        }
                        GPIO_LEDS = 0x1;
                    } else {
                        in_menu_mode = true;
                        GPIO_LEDS = 0x0;
                        print_menu();
                    }
                }

                // Continue execution
                continue;

            } else if (result == ZVIBE_SAVE_REQUESTED) {
                // Handle save request using delta compression
                int success = 0;
                if (flash_save_has_slots()) {
                    success = do_delta_save();
                } else {
                    uart_puts("\r\nSAVE not available (no save slots on this platform)\r\n");
                }
                zvibe_save_completed(ctx, success);
                continue;

            } else if (result == ZVIBE_RESTORE_REQUESTED) {
                // Handle restore request using delta compression
                int success = 0;
                if (flash_save_has_slots()) {
                    success = do_delta_restore();
                } else {
                    uart_puts("\r\nRESTORE not available (no save slots on this platform)\r\n");
                }
                zvibe_restore_completed(ctx, success);
                continue;

            } else if (result == ZVIBE_RESTART_REQUESTED) {
                // Handle restart request
                zvibe_restart_completed(ctx);
                continue;

            } else if (result == ZVIBE_GAME_FINISHED || result != ZVIBE_OK) {
                if (result != ZVIBE_GAME_FINISHED) {
                    uart_puts("\r\nERROR: Interpreter error ");
                    uart_puthex(result, 2);
                    uart_puts("\r\n");
                } else {
                    uart_puts("\r\n\r\n");
                }

                zvibe_destroy(ctx);
                ctx = NULL;
                current_game_index = -1;
                current_game_data = NULL;
                if (num_games == 1) {
                    single_game_wait_for_return();
                    if (!init_zvibe_game(&ctx, 0)) {
                        GPIO_LEDS = 0xF;
                        while (1);
                    }
                    GPIO_LEDS = 0x1;
                } else {
                    in_menu_mode = true;
                    GPIO_LEDS = 0x0;
                    print_menu();
                }
            }
        }
    }

    return 0;
}
