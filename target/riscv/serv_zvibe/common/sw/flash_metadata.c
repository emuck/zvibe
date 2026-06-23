/* Copyright (c) 2025 Martin R. Raumann */
/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * flash_metadata.c
 *
 * Dynamic flash layout metadata implementation
 */

#include <stddef.h>
/* Board config may override FLASH_METADATA_VADDR / FLASH_XIP_BASE — include
 * before flash_metadata.h so the #ifndef guards pick up board values. */
#include "flash_save_config.h"
#include "flash_metadata.h"

/* Global metadata pointer */
const zvif_header_t *g_flash_metadata = NULL;

bool flash_metadata_init(void) {
    /* Read metadata header from flash */
    const zvif_header_t *header = (const zvif_header_t *)FLASH_METADATA_VADDR;

    /* Validate magic number */
    if (header->magic != ZVIF_MAGIC) {
        return false;
    }

    /* Validate version */
    if (header->version != ZVIF_VERSION) {
        return false;
    }

    /* Validate header size */
    if (header->header_size != sizeof(zvif_header_t)) {
        return false;
    }

    /* Sanity checks */
    if (header->flash_size == 0 || header->flash_size == 0xFFFFFFFF) {
        return false;
    }

    if (header->firmware_offset == 0 || header->firmware_size == 0) {
        return false;
    }

    if (header->games_toc_offset == 0) {
        return false;
    }

    /* All checks passed - store pointer */
    g_flash_metadata = header;
    return true;
}

uint32_t flash_metadata_get_games_toc_vaddr(void) {
    if (!g_flash_metadata) {
        return 0;
    }
    return FLASH_XIP_BASE + g_flash_metadata->games_toc_offset;
}

uint32_t flash_metadata_get_saves_vaddr(void) {
    if (!g_flash_metadata || g_flash_metadata->saves_offset == 0) {
        return 0;
    }
    return FLASH_XIP_BASE + g_flash_metadata->saves_offset;
}

bool flash_metadata_has_saves(void) {
    return (g_flash_metadata != NULL) &&
           (g_flash_metadata->save_slot_count > 0) &&
           (g_flash_metadata->saves_offset != 0);
}
