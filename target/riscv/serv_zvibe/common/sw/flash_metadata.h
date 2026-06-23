/* Copyright (c) 2025 Martin R. Raumann */
/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * flash_metadata.h
 *
 * Dynamic flash layout metadata - platform-agnostic
 *
 * Firmware reads layout information from flash at boot time,
 * enabling the same binary to work on any flash size with any
 * number of games and save slots.
 */

#ifndef FLASH_METADATA_H
#define FLASH_METADATA_H

#include <stdint.h>
#include <stdbool.h>

/* ZVIF Magic Number: 'ZVIF' = ZVibe Flash Info */
#define ZVIF_MAGIC 0x46495A56  /* 'ZVIF' in little-endian: 0x5A 0x56 0x49 0x46 */
#define ZVIF_VERSION 1

/* Flash metadata header (256 bytes @ flash offset 0x0000) */
typedef struct __attribute__((packed)) {
    //=== Header (16 bytes) ===
    uint32_t magic;              /* 'ZVIF' magic number */
    uint32_t version;            /* Metadata format version (1) */
    uint32_t flash_size;         /* Total flash size in bytes */
    uint32_t header_size;        /* Size of this header (256) */

    //=== Firmware Region (16 bytes) ===
    uint32_t firmware_offset;    /* Physical offset in flash */
    uint32_t firmware_size;      /* Actual firmware size in bytes */
    uint32_t firmware_vaddr;     /* Virtual address where firmware runs */
    uint32_t firmware_reserved;  /* Reserved space for firmware growth */

    //=== Games Region (16 bytes) ===
    uint32_t games_toc_offset;   /* Physical offset of games TOC */
    uint32_t games_toc_size;     /* TOC size in bytes */
    uint32_t games_data_offset;  /* Physical offset of first game */
    uint32_t games_data_size;    /* Total game data size */

    //=== Save Slots Region (16 bytes) ===
    uint32_t saves_offset;       /* Physical offset (0 = no saves) */
    uint32_t saves_size;         /* Total save region size */
    uint32_t save_slot_size;     /* Size per save slot in bytes */
    uint32_t save_slot_count;    /* Number of save slots (0 = no saves) */

    //=== Platform Info (16 bytes) ===
    char platform[16];           /* Platform name: "max10", "arty", etc. */

    //=== Reserved (176 bytes) ===
    uint8_t reserved[176];       /* Reserved for future expansion */

} zvif_header_t;  /* Total: 256 bytes */

/*
 * FLASH_METADATA_VADDR — XIP virtual address of the ZVIF header (direct pointer).
 * FLASH_XIP_BASE       — XIP base for converting physical offsets to virtual addresses.
 *
 * On MAX10: both are 0x80000000 (UFM starts at flash physical 0).
 * On Arty:  FLASH_METADATA_VADDR = 0x80100000 (header at physical 0x100000,
 *           after bitstream), FLASH_XIP_BASE = 0x80000000 (covers all of QSPI).
 *
 * Override in boards/<board>/sw/flash_save_config.h.
 */
#ifndef FLASH_METADATA_VADDR
#define FLASH_METADATA_VADDR 0x80000000
#endif
#ifndef FLASH_XIP_BASE
#define FLASH_XIP_BASE 0x80000000
#endif

/* Global metadata pointer (initialized by flash_metadata_init) */
extern const zvif_header_t *g_flash_metadata;

/**
 * Initialize flash metadata system
 *
 * Reads and validates the ZVIF header from flash.
 * Must be called before any other flash operations.
 *
 * Returns:
 *   true if metadata valid, false on error
 */
bool flash_metadata_init(void);

/**
 * Get games TOC virtual address
 *
 * Returns:
 *   Virtual address of games TOC, or 0 if metadata not initialized
 */
uint32_t flash_metadata_get_games_toc_vaddr(void);

/**
 * Get save slots virtual address
 *
 * Returns:
 *   Virtual address of save slots region, or 0 if no saves available
 */
uint32_t flash_metadata_get_saves_vaddr(void);

/**
 * Check if save system is available
 *
 * Returns:
 *   true if save slots available, false otherwise
 */
bool flash_metadata_has_saves(void);

#endif /* FLASH_METADATA_H */
