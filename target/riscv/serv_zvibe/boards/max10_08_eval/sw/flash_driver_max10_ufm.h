/* Copyright (c) 2025 Martin R. Raumann */
/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * MAX10 UFM Flash Driver
 *
 * Provides erase, program, and read operations for Intel MAX10 on-chip UFM.
 * Protocol validated against Intel MAX10 UFM specification.
 *
 * Key requirements:
 * - Polling functions MUST run from RAM (XIP unavailable during erase/program)
 * - Use read-modify-write for CONTROL register
 * - Clear write protection before erase/program
 * - Poll STATUS busy bits until operations complete
 *
 * Usage:
 *   1. Call ufm_erase_page() to erase a page
 *   2. Call ufm_program_word() or ufm_program_buffer() to write data
 *   3. Use XIP reads (0x80000000 + byte_offset) to verify
 */

#ifndef FLASH_DRIVER_MAX10_UFM_H
#define FLASH_DRIVER_MAX10_UFM_H

#include <stdint.h>

// UFM unified controller registers
#define UFM_STATUS      (*(volatile uint32_t *)0x80040000)
#define UFM_CONTROL     (*(volatile uint32_t *)0x80040004)
#define UFM_WRITE_ADDR  (*(volatile uint32_t *)0x80040008)
#define UFM_WRITE_DATA  (*(volatile uint32_t *)0x8004000C)

// XIP base address for UFM reads
#define UFM_XIP_BASE    0x80000000

// STATUS register bits (from Intel UFM spec UG-M10UFM 683180)
#define UFM_STATUS_BUSY_MASK    0x03    // Bits [1:0]: busy
#define UFM_STATUS_WS           (1<<3)  // Write Successful (bit 3)
#define UFM_STATUS_ES           (1<<4)  // Erase Successful (bit 4)

// CONTROL register fields
#define UFM_CONTROL_WP_SECTOR4  (1<<27) // Write protect for Sector 4
#define UFM_CONTROL_SE_SHIFT    20      // Sector erase field [22:20]
#define UFM_CONTROL_PE_MASK     0xFFFFF // Page erase field [19:0]

// Return codes
#define UFM_SUCCESS     1
#define UFM_ERROR       0

/*
 * Erase an entire sector in UFM (for simulation/testing).
 *
 * Parameters:
 *   sector_id - Sector number (1-5)
 *
 * Returns:
 *   UFM_SUCCESS (1) if erase successful (ES bit set)
 *   UFM_ERROR (0) on timeout or failure
 *
 * Note: Vendor UFM simulation model only supports sector erase, not page erase.
 *       Use this for Questa simulation testing.
 *       Erase can take up to 350ms (35M cycles @ 100MHz)
 */
int ufm_erase_sector(uint32_t sector_id);

/*
 * Erase a 256-byte page in UFM (for hardware use).
 *
 * Parameters:
 *   page_offset - Byte offset of page to erase (e.g., 0x2000 for page at byte 0x2000)
 *
 * Returns:
 *   UFM_SUCCESS (1) if erase successful (ES bit set)
 *   UFM_ERROR (0) on timeout or failure
 *
 * Note: Page erase works on real hardware but may not work in vendor simulation model.
 *       Erase can take up to 350ms (35M cycles @ 100MHz)
 */
int ufm_erase_page(uint32_t page_offset);

/*
 * Program (write) a single 32-bit word to UFM.
 *
 * Parameters:
 *   word_addr - UFM word address (NOT byte address)
 *               Example: word_addr=0x2000 writes to byte offset 0x8000
 *   data      - 32-bit data to write
 *
 * Returns:
 *   UFM_SUCCESS (1) if program successful (WS bit set)
 *   UFM_ERROR (0) on timeout or failure
 *
 * Note: Page must be erased before programming
 *       Program takes ~305µs max (30K cycles @ 100MHz)
 */
int ufm_program_word(uint32_t word_addr, uint32_t data);

/*
 * Program (write) multiple consecutive words to UFM.
 *
 * Parameters:
 *   word_addr - Starting UFM word address
 *   data      - Pointer to data buffer (32-bit words)
 *   count     - Number of words to write
 *
 * Returns:
 *   UFM_SUCCESS (1) if all words programmed successfully
 *   UFM_ERROR (0) on any failure
 *
 * Note: More efficient than calling ufm_program_word() in a loop
 */
int ufm_program_buffer(uint32_t word_addr, const uint32_t *data, int count);

/*
 * Read UFM data via XIP (Execute-In-Place).
 *
 * Parameters:
 *   word_addr - UFM word address to read
 *
 * Returns:
 *   32-bit data at the specified address
 *
 * Note: This is a simple inline helper for clarity.
 *       Direct memory access works the same: *(volatile uint32_t *)(UFM_XIP_BASE + word_addr*4)
 */
static inline uint32_t ufm_read_word(uint32_t word_addr) {
    volatile uint32_t *ptr = (volatile uint32_t *)(UFM_XIP_BASE + word_addr * 4);
    return *ptr;
}

#endif /* FLASH_DRIVER_MAX10_UFM_H */
