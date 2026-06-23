/* Copyright (c) 2025 Martin R. Raumann */
/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * game_registry_riscv.c
 *
 * RISC-V game registry - loads multi-game manifest from flash TOC
 */

#include "game_registry_riscv.h"
#include "flash_metadata.h"
#include <string.h>

/* Flash TOC pointer (XIP - direct flash access for game data!) */
static volatile flash_toc_t *toc = NULL;

/* Flash TOC base address (loaded dynamically from metadata) */
static uint32_t flash_toc_base = 0;

/* Game count (read from TOC header) */
int num_games = 0;

/* Small cache: copy current game name to RAM (flash may not support byte reads) */
static char cached_game_name[48];
static int cached_game_index = -1;

bool load_game_registry_from_flash(void) {
    // Get TOC address from flash metadata (dynamic discovery!)
    flash_toc_base = flash_metadata_get_games_toc_vaddr();
    if (flash_toc_base == 0) {
        return false;  // Metadata not initialized
    }

    // Access flash TOC directly (XIP - no copying needed!)
    toc = (volatile flash_toc_t*)flash_toc_base;

    // Validate magic number
    if (toc->header.magic != FLASH_TOC_MAGIC) {
        return false;
    }

    // Validate version
    if (toc->header.version != 2) {
        return false;
    }

    // Validate game count (sanity check - make sure it fits in TOC)
    if (toc->header.game_count == 0 || toc->header.game_count > MAX_TOC_ENTRIES) {
        return false;
    }

    // Store game count - no copying needed, we access flash directly!
    num_games = toc->header.game_count;

    return true;
}

const game_entry_t* get_game(int index) {
    if (index < 0 || index >= num_games || !toc || flash_toc_base == 0) {
        return NULL;
    }

    // Calculate TOC entry address (each entry is 80 bytes, header is 16 bytes)
    uint32_t entry_addr = flash_toc_base + 16 + (index * 80);
    const volatile uint32_t *entry_words = (const volatile uint32_t*)entry_addr;

    // Read offset and size using word-aligned reads (first 8 bytes of entry)
    uint32_t game_offset = entry_words[0];  // Offset (CPU virtual address)
    uint32_t game_size = entry_words[1];    // Size in bytes

    // Copy name to RAM cache if needed (flash may not support byte-aligned reads)
    if (cached_game_index != index) {
        // Name field starts at offset 24 in TOC entry (8 bytes offset/size + 16 bytes id)
        const volatile uint32_t *name_words = (const volatile uint32_t*)(entry_addr + 24);

        // Copy 48 bytes as twelve 32-bit words (word-aligned reads only)
        uint32_t *dest_words = (uint32_t*)cached_game_name;
        for (int i = 0; i < 12; i++) {
            dest_words[i] = name_words[i];
        }
        cached_game_name[47] = '\0';  // Ensure null termination
        cached_game_index = index;
    }

    // Build game_entry_t with proper flash-read values
    static game_entry_t result;
    result.name = cached_game_name;                 // RAM cache (byte reads OK)
    result.data = (const uint8_t*)game_offset;      // XIP game data address (from flash)
    result.size = game_size;                        // Game size (from flash)

    return &result;
}
