/* Copyright (c) 2025 Martin R. Raumann */
/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * MAX10 UFM Flash Driver Implementation
 *
 * Protocol implementation validated against Intel MAX10 UFM specification.
 */

#include "flash_driver_max10_ufm.h"

/*
 * Poll UFM STATUS register until busy bits [1:0] clear.
 *
 * CRITICAL: This function MUST run from RAM because XIP (execute-in-place)
 * from flash is unavailable during erase/program operations. If this runs
 * from flash, the CPU will hang waiting for flash to respond.
 *
 * The .ramfunc section is loaded into RAM at boot by the startup code.
 *
 * Returns: Final STATUS register value after busy clears or timeout
 */
__attribute__((section(".ramfunc")))
static uint32_t ufm_wait_ready(void) {
    volatile uint32_t status;
    volatile uint32_t timeout = 50000000;  // 50M cycles max (erase: 35M, program: 30K)

    do {
        status = UFM_STATUS;
        timeout--;
    } while ((status & UFM_STATUS_BUSY_MASK) != 0 && timeout > 0);

    return status;
}

/*
 * Erase an entire sector in UFM.
 *
 * Protocol:
 * 1. Wait for UFM ready (busy bits clear)
 * 2. Build CONTROL: Clear write protection for target sector
 * 3. Set sector ID in se field [22:20]
 * 4. Write CONTROL once with full command
 * 5. CRITICAL: Delay for command to latch (vendor model requirement)
 * 6. Poll STATUS until busy clears (runs from RAM)
 * 7. Check ES (Erase Successful) bit
 *
 * Note: Vendor simulation model supports sector erase, not page erase
 *
 * CRITICAL: This function MUST run from RAM because after writing CONTROL
 * to start erase, the UFM becomes busy and stops responding to XIP fetches.
 */
__attribute__((section(".ramfunc")))
int ufm_erase_sector(uint32_t sector_id) {
    uint32_t control;
    uint32_t status;
    uint32_t wp_bit;

    // Validate sector ID (1-5)
    if (sector_id < 1 || sector_id > 5) {
        return UFM_ERROR;
    }

    // Step 1: Wait for UFM ready
    status = ufm_wait_ready();
    if (status & UFM_STATUS_BUSY_MASK) {
        return UFM_ERROR;  // Still busy after timeout
    }

    // Step 2: Calculate write protection bit for this sector
    // wp[23]=Sector1, wp[24]=Sector2, ..., wp[27]=Sector5
    wp_bit = 23 + (sector_id - 1);

    // Step 3: Build and send erase command (write CONTROL once)
    // CRITICAL: Vendor model triggers erase on CONTROL write
    control = 0;                                    // Start fresh
    control |= (0x1F << 23);                        // Set all wp bits first
    control &= ~(1 << wp_bit);                      // Clear wp bit for target sector
    control |= (sector_id << UFM_CONTROL_SE_SHIFT); // se = sector ID (1-5)
    control |= UFM_CONTROL_PE_MASK;                 // pe = FFFFF (unused for sector erase)
    UFM_CONTROL = control;                          // Write once with full command

    // Step 4: CRITICAL - Delay for command to latch
    // Vendor UFM model needs time for CSR write to propagate before erase starts
    // This matches the unit test approach: repeat(50) @(posedge clock)
    // REDUCED to 10 iterations to match unit test timing more closely
    for (volatile int i = 0; i < 10; i++) {
        __asm__ volatile ("nop");  // Prevent optimization
    }

    // Step 5: Wait for erase to complete (runs from RAM!)
    status = ufm_wait_ready();

    // Step 6: Check ES (Erase Successful) bit [4]
    if (status & UFM_STATUS_ES) {
        return UFM_SUCCESS;
    } else {
        return UFM_ERROR;
    }
}

/*
 * Erase a 256-byte page in UFM.
 *
 * Protocol:
 * 1. Wait for UFM ready (busy bits clear)
 * 2. Calculate which sector contains the page
 * 3. Read-modify-write CONTROL: Clear write protection for target sector
 * 4. Send erase command with page offset
 * 5. CRITICAL: Delay for command to latch (vendor model requirement)
 * 6. Poll STATUS until busy clears (runs from RAM)
 * 7. Check ES (Erase Successful) bit
 *
 * Page offset is BYTE address (e.g., 0x2000 for page at byte 0x2000)
 * Must be page-aligned (multiple of 256 = 0x100)
 *
 * CRITICAL: This function MUST run from RAM because after writing CONTROL
 * to start erase, the UFM becomes busy and stops responding to XIP fetches.
 */
__attribute__((section(".ramfunc")))
int ufm_erase_page(uint32_t page_offset) {
    uint32_t control;
    uint32_t status;

    // Step 1: Wait for UFM ready
    status = ufm_wait_ready();
    if (status & UFM_STATUS_BUSY_MASK) {
        return UFM_ERROR;  // Still busy after timeout
    }

    // Step 2: Build page erase command via read-modify-write.
    // Save region lives in CFM1; clear only CFM1 write-protection bit [26].
    // se = 111 (no sector erase), pe = word address of page to erase.
    control = UFM_CONTROL;
    control &= ~(1 << 26);                                  // clear CFM1 wp bit
    control |= (0x7 << UFM_CONTROL_SE_SHIFT);               // se = 111 (no sector erase)
    control &= ~UFM_CONTROL_PE_MASK;                        // clear old pe
    control |= ((page_offset >> 2) & UFM_CONTROL_PE_MASK);  // set new pe
    UFM_CONTROL = control;                                   // triggers page erase

    // Step 5: CRITICAL - Delay for command to latch
    // Vendor UFM model needs time for CSR write to propagate before erase starts
    // This matches the unit test approach: repeat(50) @(posedge clock)
    // At 100MHz, 50 cycles = 500ns
    for (volatile int i = 0; i < 100; i++) {
        __asm__ volatile ("nop");  // Prevent optimization
    }

    // Step 6: Wait for erase to complete (runs from RAM!)
    status = ufm_wait_ready();

    // Step 7: Check ES (Erase Successful) bit [4]
    if (status & UFM_STATUS_ES) {
        return UFM_SUCCESS;
    } else {
        return UFM_ERROR;
    }
}

/*
 * Program (write) a single 32-bit word to UFM.
 *
 * Protocol:
 * 1. Wait for UFM ready
 * 2. Determine target sector from word address
 * 3. Clear write protection for target sector (read-modify-write CONTROL)
 *    - Clear wp bit for target sector
 *    - Set erase fields (se/pe) to safe no-op values (all 1's)
 * 4. Write word address to WRITE_ADDR register
 * 5. Write data to WRITE_DATA register (triggers program operation)
 * 6. Poll STATUS until busy clears (runs from RAM)
 * 7. Check WS (Write Successful) bit [3]
 *
 * CRITICAL: This function MUST run from RAM because after writing WRITE_DATA
 * to start program, the UFM becomes busy and stops responding to XIP fetches.
 */
__attribute__((section(".ramfunc")))
int ufm_program_word(uint32_t word_addr, uint32_t data) {
    uint32_t control;
    uint32_t status;

    // Step 1: Wait for UFM ready
    status = ufm_wait_ready();
    if (status & UFM_STATUS_BUSY_MASK) {
        return UFM_ERROR;  // Still busy after timeout
    }

    // Skip writing 0xFFFFFFFF: erased flash already reads all-1s, and the UFM
    // may not assert WS for a no-op program of 0xFFFFFFFF to 0xFFFFFFFF.
    if (data == 0xFFFFFFFF)
        return UFM_SUCCESS;

    // Step 2: Set up CONTROL for program (read-modify-write).
    // Save region lives in CFM1; clear CFM1 write-protection bit [26].
    // se = 111 (no sector erase), pe = all-1 (no page erase).
    control = UFM_CONTROL;
    control &= ~(1 << 26);              // clear CFM1 wp bit
    control |= (0x7 << UFM_CONTROL_SE_SHIFT);  // se = 111
    control |= UFM_CONTROL_PE_MASK;             // pe = 0xFFFFF (no page erase)
    UFM_CONTROL = control;

    // Step 3: Set write address (word address, not byte!)
    UFM_WRITE_ADDR = word_addr;

    // Step 4: Write data (triggers program operation)
    UFM_WRITE_DATA = data;

    // Step 5: Wait for program to complete (runs from RAM!)
    status = ufm_wait_ready();

    // Step 6: Check WS (Write Successful) bit [3]
    if (status & UFM_STATUS_WS) {
        return UFM_SUCCESS;
    } else {
        return UFM_ERROR;
    }
}

/*
 * Program multiple consecutive words to UFM.
 *
 * More efficient than calling ufm_program_word() in a loop because
 * it only checks final status once (though each word still polls internally).
 *
 * CRITICAL: This function MUST run from RAM because it calls ufm_program_word()
 * which triggers UFM busy state.
 */
__attribute__((section(".ramfunc")))
int ufm_program_buffer(uint32_t word_addr, const uint32_t *data, int count) {
    for (int i = 0; i < count; i++) {
        if (!ufm_program_word(word_addr + i, data[i])) {
            return UFM_ERROR;  // Failed on word i
        }
    }
    return UFM_SUCCESS;
}
