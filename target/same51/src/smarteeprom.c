/* Copyright (c) 2025 Martin R. Raumann */
/* SPDX-License-Identifier: BSD-3-Clause */
/*******************************************************************************
  smarteeprom.c - the SAM E51's SmartEEPROM as a byte store: fuse set-up, unlocking,
  whole-word writes into the page buffer, commit (SEEFLUSH) and reads. See smarteeprom.h.
*******************************************************************************/

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "board_support.h"
#include "smarteeprom.h"        /* SEE_API: empty, or static when a V3 firmware includes this file */

/* Target fuse values for 16KB SmartEEPROM */
#define TARGET_SBLK                 3U  // SBLK=3 for 16KB virtual size
#define TARGET_PSZ                  7U  // PSZ=7 (512-byte pages) for fastest writes

/* SmartEEPROM status structure */
typedef struct {
    uint8_t sblk;
    uint8_t psz;
    bool lock;
    bool rlock;
} smarteeprom_status_t;

/* Parse SmartEEPROM status register */
static smarteeprom_status_t parse_smarteeprom_status(uint32_t raw_status) {
    smarteeprom_status_t status = {0};
    status.sblk = (raw_status >> 8) & 0xF;
    status.psz = (raw_status >> 16) & 0x7;
    status.lock = (raw_status >> 3) & 0x1;
    status.rlock = (raw_status >> 4) & 0x1;
    return status;
}

/* Configure SmartEEPROM fuses for 16KB operation */
static bool configure_smarteeprom_fuses(void) {
    uint32_t user_row_data[128];
    
    if (!NVMCTRL_Read(user_row_data, NVMCTRL_USERROW_SIZE, NVMCTRL_USERROW_START_ADDRESS)) {
        return false;
    }
    
    uint32_t new_word1 = user_row_data[1];
    new_word1 &= ~(0xF << 0);           // Clear SBLK bits
    new_word1 &= ~(0x7 << 4);           // Clear PSZ bits
    new_word1 |= (TARGET_SBLK << 0);    // Set SBLK=3
    new_word1 |= (TARGET_PSZ << 4);     // Set PSZ=7
    user_row_data[1] = new_word1;
    
    if (!NVMCTRL_USER_ROW_RowErase(NVMCTRL_USERROW_START_ADDRESS)) {
        return false;
    }
    while (NVMCTRL_IsBusy()) { /* Wait */ }
    
    if (!NVMCTRL_USER_ROW_PageWrite(user_row_data, NVMCTRL_USERROW_START_ADDRESS)) {
        return false;
    }
    while (NVMCTRL_IsBusy()) { /* Wait */ }
    
    // Wait for UART transmission then reset
    for (volatile int i = 0; i < 100000; i++);
    NVIC_SystemReset();
    
    return true; // Never reached
}

/* Unlock SmartEEPROM access */
static bool unlock_smarteeprom(void) {
    uint32_t see_status = NVMCTRL_SmartEEPROMStatusGet();
    smarteeprom_status_t status = parse_smarteeprom_status(see_status);
    
    // Unlock register space if needed
    if (status.rlock) {
        NVMCTRL_REGS->NVMCTRL_CTRLB = NVMCTRL_CTRLB_CMD_USEER | NVMCTRL_CTRLB_CMDEX_KEY;
        while (NVMCTRL_IsBusy()) { /* Wait */ }
        see_status = NVMCTRL_SmartEEPROMStatusGet();
        if ((see_status >> 4) & 0x1) return false; // Still locked
    }
    
    // Unlock data access if needed
    if (status.lock) {
        NVMCTRL_REGS->NVMCTRL_CTRLB = NVMCTRL_CTRLB_CMD_USEE | NVMCTRL_CTRLB_CMDEX_KEY;
        while (NVMCTRL_IsBusy()) { /* Wait */ }
        see_status = NVMCTRL_SmartEEPROMStatusGet();
        if ((see_status >> 3) & 0x1) return false; // Still locked
    }
    
    return true;
}

/* SmartEEPROM initialization */
SEE_API bool smarteeprom_init(void) {
    uint32_t see_status = NVMCTRL_SmartEEPROMStatusGet();
    smarteeprom_status_t status = parse_smarteeprom_status(see_status);
    
    // Check fuse configuration
    if (status.sblk != TARGET_SBLK || status.psz != TARGET_PSZ) {
        configure_smarteeprom_fuses();
        return false; // Device will reset
    }
    
    // Unlock SmartEEPROM
    if (!unlock_smarteeprom()) {
        return false;
    }
    
    // Configure for optimal operation: BUFFERED mode + APRDIS=0
    uint8_t seecfg = NVMCTRL_REGS->NVMCTRL_SEECFG;
    
    // Enable buffered mode (WMODE=1) for fastest large writes
    if ((seecfg & 0x1) == 0) {
        seecfg |= 0x01;  // Set WMODE bit for BUFFERED mode
    }
    
    // Enable automatic page reallocation (APRDIS=0) for large writes
    if (seecfg & 0x02) {
        seecfg &= 0xFD;  // Clear APRDIS bit
    }
    
    NVMCTRL_REGS->NVMCTRL_SEECFG = seecfg;
    NVMCTRL_SetWriteMode(NVMCTRL_WMODE_MAN);
    return true;
}

/* Write data into the SmartEEPROM page buffer with 32-bit word writes only. Nothing is stored for
 * good until smarteeprom_flush() runs.
 *
 * Two things the SmartEEPROM does not do, found on the board: a single-byte store is not
 * persisted, and zero words at the END of a write are not persisted over older non-zero data (the
 * tail of a save came back as whatever an earlier, larger save had left there; the old checksum
 * mostly hid it). So the last, partial word is padded with ones and written whole, and the caller
 * ends every write with a non-zero word (smarteeprom_write_end_mark). The padding and the mark land
 * after the data, in space the save does not use. */
SEE_API bool smarteeprom_write_raw(uint32_t offset, const void *data, size_t size) {
    const uint8_t *src = (const uint8_t*)data;
    size_t words = (size + 3) / 4;
    
    if ((offset & 3) != 0 || offset + words * 4 > SMARTEEPROM_SIZE) return false;
    
    volatile uint32_t *smarteeprom_words = (volatile uint32_t*)(SMARTEEPROM_BASE_ADDR + offset);
    for (size_t i = 0; i < words; i++) {
        uint32_t word = 0xFFFFFFFFUL;
        size_t n = size - i * 4;
        memcpy(&word, src + i * 4, n < 4 ? n : 4);
        smarteeprom_words[i] = word;
    }
    
    return true;
}

/* A non-zero word right after a save's data, so that the last words of the data (which may be zero)
 * are not the tail of the write. Needs room for one word after the data's padded end. */
SEE_API bool smarteeprom_write_end_mark(uint32_t data_end_offset) {
    static const uint32_t mark = 0xA55A5AA5UL;
    return smarteeprom_write_raw((data_end_offset + 3) & ~3UL, &mark, sizeof mark);
}

/* Commit what was written (SEEFLUSH in BUFFERED mode) and wait until the SmartEEPROM is done. */
SEE_API bool smarteeprom_flush(void) {
    __DSB();
    __ISB();
    NVMCTRL_REGS->NVMCTRL_CTRLB = NVMCTRL_CTRLB_CMD_SEEFLUSH | NVMCTRL_CTRLB_CMDEX_KEY;
    
    volatile uint32_t timeout = 1000000;
    while (NVMCTRL_SmartEEPROM_IsBusy() && timeout > 0) {
        timeout--;
    }
    
    return timeout > 0;
}

/* Read data from SmartEEPROM using optimized 32-bit word reads */
SEE_API bool smarteeprom_read_data(uint32_t offset, void *buffer, size_t size) {
    if (offset + size > SMARTEEPROM_SIZE) return false;
    
    // Ensure unlocked
    if (!unlock_smarteeprom()) return false;
    
    uint8_t *dest = (uint8_t*)buffer;
    
    __DSB();
    __ISB();
    
    // Handle word-aligned portion with 32-bit reads
    if (size >= 4 && (offset & 3) == 0 && ((uintptr_t)buffer & 3) == 0) {
        volatile uint32_t *smarteeprom_words = (volatile uint32_t*)(SMARTEEPROM_BASE_ADDR + offset);
        uint32_t *dest_words = (uint32_t*)buffer;
        size_t word_count = size / 4;
        
        for (size_t i = 0; i < word_count; i++) {
            dest_words[i] = smarteeprom_words[i];
        }
        
        // Handle remaining bytes
        size_t remaining = size % 4;
        if (remaining > 0) {
            volatile uint8_t *smarteeprom_bytes = (volatile uint8_t*)(SMARTEEPROM_BASE_ADDR + offset + (word_count * 4));
            uint8_t *dest_bytes = dest + (word_count * 4);
            
            for (size_t i = 0; i < remaining; i++) {
                dest_bytes[i] = smarteeprom_bytes[i];
            }
        }
    } else {
        // Fall back to byte reads for unaligned data
        volatile uint8_t *smarteeprom = (volatile uint8_t*)(SMARTEEPROM_BASE_ADDR + offset);
        
        for (size_t i = 0; i < size; i++) {
            dest[i] = smarteeprom[i];
        }
    }
    
    return true;
}

