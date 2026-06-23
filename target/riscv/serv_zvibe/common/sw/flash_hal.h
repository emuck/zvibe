/* Copyright (c) 2025 Martin R. Raumann */
/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Flash HAL — Common Interface
 *
 * Three functions that every platform must implement:
 *   flash_read_bytes    — read from flash (XIP or register-based)
 *   flash_erase_sector  — erase a sector/page at byte_addr
 *   flash_program_bytes — program bytes into erased flash
 *
 * MAX10: implemented by boards/max10_08_eval/sw/flash_hal_max10.c
 * Arty:  implemented by boards/arty_s7_50/sw/flash_driver.c
 */

#ifndef FLASH_HAL_H
#define FLASH_HAL_H

#include <stdint.h>
#include <stddef.h>

void flash_read_bytes(uint32_t byte_addr, void *buf, size_t len);
int  flash_erase_sector(uint32_t byte_addr);
int  flash_program_bytes(uint32_t byte_addr, const void *data, size_t len);

#endif /* FLASH_HAL_H */
