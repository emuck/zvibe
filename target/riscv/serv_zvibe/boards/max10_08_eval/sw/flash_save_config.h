/* Copyright (c) 2025 Martin R. Raumann */
/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Flash Save Configuration — Intel MAX10 UFM
 *
 * SAVE_REGION_BASE and NUM_SLOTS are NOT compile-time constants here —
 * they come from the ZVIF metadata read at boot (g_flash_metadata) and
 * are passed to flash_save_init() at runtime. This avoids the circular
 * dependency between flash_layout.h (generated after firmware is built)
 * and the firmware compilation itself.
 *
 * SLOT_SIZE / SECTOR_SIZE: 2KB — one physical UFM page per slot.
 * flash_erase_sector(addr) erases SECTOR_SIZE/2048 = 1 page per call.
 */

#ifndef FLASH_SAVE_CONFIG_H
#define FLASH_SAVE_CONFIG_H

#define SLOT_SIZE           2048U   /* 2KB per slot (1 UFM page) */
#define SECTOR_SIZE         SLOT_SIZE

#define SAVE_HEADER_SIZE    12
#define MAX_SAVE_DATA       (SLOT_SIZE - SAVE_HEADER_SIZE)
#define SAVE_MAGIC          0x5A564942U   /* "ZVIB" */

#endif /* FLASH_SAVE_CONFIG_H */
