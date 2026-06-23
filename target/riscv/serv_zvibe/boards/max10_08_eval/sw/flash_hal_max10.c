/* Copyright (c) 2025 Martin R. Raumann */
/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * MAX10 UFM Flash HAL
 *
 * Thin adapter implementing the flash_save.c HAL API on top of the
 * proven flash_driver_max10_ufm.c functions (hardware-validated Feb 12, 2026).
 *
 * API (called by flash_save.c):
 *   flash_read_bytes()    - XIP memory-mapped read, no driver needed
 *   flash_erase_sector()  - delegates to ufm_erase_page()
 *   flash_program_bytes() - delegates to ufm_program_word(), word-aligned
 *
 * Address convention: all addresses are physical UFM byte offsets
 * (same as flash_save_max10.h SAVE_REGION_BASE / flash_layout.h offsets).
 * The XIP base (0x80000000) is added only for reads.
 */

#include "flash_hal_max10.h"
#include "flash_driver_max10_ufm.h"
#include "flash_save_config.h"

/*
 * Initialize UFM HAL.
 * The UFM IP is ready after FPGA configuration; just poll until idle.
 */
int flash_hal_init(void) {
    /* ufm_wait_ready() is static inside flash_driver_max10_ufm.c, but
     * a simple STATUS read is enough — if we can read it, the IP is up. */
    volatile uint32_t status = UFM_STATUS;
    return ((status & UFM_STATUS_BUSY_MASK) == 0) ? 0 : -1;
}

/*
 * Read bytes from UFM via XIP.
 * Physical byte offset maps directly to 0x80000000 + byte_addr.
 */
void flash_read_bytes(uint32_t byte_addr, void *buf, size_t len) {
    volatile uint8_t *src = (volatile uint8_t *)(0x80000000u + byte_addr);
    uint8_t *dst = (uint8_t *)buf;
    for (size_t i = 0; i < len; i++)
        dst[i] = src[i];
}

/*
 * Erase SECTOR_SIZE bytes of UFM starting at byte_addr.
 * UFM physical erase granularity is 2KB; SECTOR_SIZE may be a multiple.
 * flash_save.c passes SECTOR_SIZE-aligned addresses.
 * Returns 0 on success, -1 on first failure.
 */
__attribute__((section(".ramfunc")))
int flash_erase_sector(uint32_t byte_addr) {
    for (uint32_t i = 0; i < SECTOR_SIZE / 2048; i++) {
        if (!ufm_erase_page(byte_addr + (uint32_t)i * 2048))
            return -1;
    }
    return 0;
}

/*
 * Program len bytes to UFM starting at byte_addr.
 * Handles sub-word alignment by padding incomplete words with 0xFF.
 * Page must already be erased.
 * Returns 0 on success, -1 on first failure.
 */
__attribute__((section(".ramfunc")))
int flash_program_bytes(uint32_t byte_addr, const void *data, size_t len) {
    const uint8_t *src = (const uint8_t *)data;
    uint32_t word_addr = byte_addr / 4;
    int byte_in_word   = (int)(byte_addr & 3);  /* starting byte offset within first word */

    while (len > 0) {
        /* Build one 32-bit word, padding unwritten bytes with 0xFF */
        uint32_t word = 0xFFFFFFFF;
        int i = byte_in_word;
        while (i < 4 && len > 0) {
            word &= ~(0xFFu << (i * 8));
            word |= ((uint32_t)(*src++)) << (i * 8);
            i++;
            len--;
        }
        if (!ufm_program_word(word_addr, word))
            return -1;
        word_addr++;
        byte_in_word = 0;  /* subsequent words are always aligned */
    }
    return 0;
}
