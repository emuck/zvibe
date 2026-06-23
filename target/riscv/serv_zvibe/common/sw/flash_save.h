/* Copyright (c) 2025 Martin R. Raumann */
/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Flash Save System — Platform-Agnostic API
 *
 * Simple round-robin wear leveling. One logical save slot (latest write wins).
 * Platform configuration (slot size, region, count) provided by the board's
 * flash_save_config.h, selected via compiler -I path.
 *
 * Supports 0 save slots gracefully: all calls succeed or return NOT_FOUND.
 */

#ifndef FLASH_SAVE_H
#define FLASH_SAVE_H

#include <stdint.h>
#include <stddef.h>

/* Return codes */
#define SAVE_OK             0
#define SAVE_ERR_NOT_FOUND  -5
#define SAVE_ERR_TOO_LARGE  -3
#define SAVE_ERR_NO_SPACE   -4
#define SAVE_ERR_FLASH      -7

/*
 * Scan all slots and initialize internal state (next write slot + sequence).
 * Call once at boot before any save/restore operations.
 *
 * base:      physical byte address of start of save region in flash
 * num_slots: number of slots in the region (0 = save unavailable)
 *
 * For MAX10: pass g_flash_metadata->saves_offset and save_slot_count.
 * For Arty:  pass SAVE_REGION_BASE and NUM_SLOTS from flash_save_config.h.
 */
int flash_save_init(uint32_t base, int num_slots);

/*
 * Write save data. Erases next slot in round-robin, then writes.
 * Returns SAVE_OK or SAVE_ERR_*.
 */
int flash_save_write(const void *data, size_t data_len);

/*
 * Read the most recent save into buf.
 * Returns number of bytes read, or SAVE_ERR_*.
 */
int flash_save_read(void *buf, size_t buf_len);

/*
 * Returns 1 if a valid save exists, 0 otherwise.
 */
int flash_save_exists(void);

/*
 * Returns 1 if flash_save_init() has been called with num_slots > 0.
 */
int flash_save_has_slots(void);

#endif /* FLASH_SAVE_H */
