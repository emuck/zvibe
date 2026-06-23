/* Copyright (c) 2025 Martin R. Raumann */
/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * MAX10 UFM Flash HAL Header
 *
 * Low-level flash operations for MAX10 UFM via Wishbone bridge.
 */

#ifndef FLASH_HAL_MAX10_H
#define FLASH_HAL_MAX10_H

#include <stdint.h>
#include <stddef.h>

/**
 * Initialize UFM flash HAL.
 * Must be called before using other flash functions.
 *
 * @return 0 on success, -1 on failure
 */
int flash_hal_init(void);

/**
 * Read bytes from flash (XIP memory-mapped read).
 *
 * @param byte_addr Physical byte address in UFM
 * @param buf       Buffer to receive data
 * @param len       Number of bytes to read
 */
void flash_read_bytes(uint32_t byte_addr, void *buf, size_t len);

/**
 * Erase a flash sector (page).
 * On 10M08, erases one 8KB page (minimum erase granularity).
 * Automatically determines UFM sector for write protection.
 *
 * @param byte_addr Any byte address within the 8KB page to erase
 * @return 0 on success, -1 on failure
 */
int flash_erase_sector(uint32_t byte_addr);

/**
 * Program bytes to flash.
 * Flash must be erased first (writes can only clear bits, not set them).
 * Data is programmed in 32-bit word units.
 *
 * @param byte_addr Physical byte address in UFM (word-aligned preferred)
 * @param data      Data to program
 * @param len       Number of bytes to program
 * @return 0 on success, -1 on failure
 */
int flash_program_bytes(uint32_t byte_addr, const void *data, size_t len);

#endif // FLASH_HAL_MAX10_H
