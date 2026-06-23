/* Copyright (c) 2025 Martin R. Raumann */
/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * game_registry_riscv.h
 *
 * RISC-V game registry - loads multi-game manifest from flash TOC
 */

#ifndef GAME_REGISTRY_RISCV_H
#define GAME_REGISTRY_RISCV_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "menu_system.h"

/* Flash TOC base address is now dynamic - read from flash metadata at runtime.
 * See flash_metadata.h for the ZVIF header format.
 * No longer hardcoded to support multiple platforms!
 */

/* Maximum games that can fit in a 4KB TOC (sanity check only) */
#define MAX_TOC_ENTRIES ((4096 - 16) / 80)  /* (4KB - header) / 80 bytes per entry = 51 */

/* Flash TOC structures (must match Python builder format in tools/game_builder/riscv.py) */

/* TOC Header (16 bytes) */
typedef struct __attribute__((packed)) {
    uint32_t magic;         /* 'ZVGM' magic number */
    uint32_t version;       /* TOC format version (2) */
    uint32_t game_count;    /* Number of games in TOC */
    uint32_t reserved;      /* Reserved for future use */
} flash_toc_header_t;

/* TOC Entry (80 bytes — v2) */
typedef struct __attribute__((packed)) {
    uint32_t offset;          /* Absolute flash address of game data */
    uint32_t size;            /* Size of game data in bytes */
    char id[16];              /* Game ID (null-terminated) */
    char name[48];            /* Display name (null-terminated) */
    uint32_t saves_offset;    /* Flash offset of this game's save ring (0 = none) */
    uint32_t save_slot_count; /* Number of save slots for this game (0 = none) */
} flash_toc_entry_t;          /* 80 bytes total */

_Static_assert(sizeof(flash_toc_entry_t) == 80, "flash_toc_entry_t must be 80 bytes");

/* Complete TOC (header + entries) */
typedef struct __attribute__((packed)) {
    flash_toc_header_t header;
    flash_toc_entry_t entries[];  /* Flexible array - no fixed limit! */
} flash_toc_t;

/* Expected magic number 'ZVGM' */
#define FLASH_TOC_MAGIC 0x4D47565A  /* 'ZVGM' in little-endian: bytes 5A 56 47 4D */

/* Game count (read from flash TOC header) */
extern int num_games;

/**
 * Load game registry from flash TOC
 *
 * Reads the Table of Contents from flash and populates the games[] array.
 * Game data pointers point directly into flash (XIP - no copying needed).
 * Game names are copied into RAM for stability.
 *
 * Returns:
 *   true if TOC loaded successfully, false on error
 */
bool load_game_registry_from_flash(void);

/**
 * Get game by index
 *
 * Args:
 *   index: Game index (0-based)
 *
 * Returns:
 *   Pointer to game entry, or NULL if index out of range
 */
const game_entry_t* get_game(int index);

#endif /* GAME_REGISTRY_RISCV_H */
