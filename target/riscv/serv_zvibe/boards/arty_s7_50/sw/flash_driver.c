/* Copyright (c) 2025 Martin R. Raumann */
/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Flash Driver for ZVibe RISC-V
 *
 * Provides flash read/write/erase operations for the save system.
 * Uses XIP for reads, hardware write controller for writes.
 *
 * IMPORTANT: Flash write functions are placed in .ramfunc section because
 * they must execute from RAM while flash is unavailable during writes.
 * The XIP controller is disconnected when the write controller is active.
 */

#include <stdint.h>
#include <stddef.h>

// Attribute to place functions in RAM (copied from flash at startup)
#define RAMFUNC __attribute__((section(".ramfunc")))

//=============================================================================
// Hardware Registers
//=============================================================================

// Flash write controller registers
#define FLASH_WRITE_BASE   0x81000000
#define FLASH_WRITE_CTRL   (*(volatile uint32_t*)(FLASH_WRITE_BASE + 0x00))
#define FLASH_WRITE_ADDR   (*(volatile uint32_t*)(FLASH_WRITE_BASE + 0x04))
#define FLASH_WRITE_DATA   (*(volatile uint32_t*)(FLASH_WRITE_BASE + 0x08))
#define FLASH_WRITE_STATUS (*(volatile uint32_t*)(FLASH_WRITE_BASE + 0x0C))

// Flash write commands (bits in CTRL register)
#define CMD_WREN  (1 << 0)    // Write Enable (MUST be sent before SE or PP!)
#define CMD_SE    (1 << 1)    // Sector Erase
#define CMD_PP    (1 << 2)    // Page Program (single byte)
#define CMD_RDSR  (1 << 3)    // Read Status Register

// Flash write status bits
#define STATUS_BUSY  (1 << 1)

// XIP base address (for reading)
#define XIP_BASE  0x80000000

//=============================================================================
// Flash Operations
//=============================================================================

// Small delay for hardware timing (must be in RAM)
static void RAMFUNC delay(int n) {
    for (volatile int i = 0; i < n; i++);
}

// Wait for flash controller to complete (must be in RAM)
static int RAMFUNC flash_wait_complete(void) {
    int timeout = 10000000;  // ~1 second at 166MHz
    while ((FLASH_WRITE_STATUS & STATUS_BUSY) && timeout > 0) {
        timeout--;
    }
    return (timeout == 0) ? -1 : 0;
}

// Send Write Enable command (required before every SE or PP) (must be in RAM)
static int RAMFUNC flash_write_enable(void) {
    FLASH_WRITE_CTRL = CMD_WREN;
    delay(10);
    return flash_wait_complete();
}

// Read bytes from flash via XIP
void flash_read_bytes(uint32_t addr, void *buf, size_t len) {
    volatile uint8_t *src = (volatile uint8_t *)(XIP_BASE + addr);
    uint8_t *dst = (uint8_t *)buf;

    while (len--) {
        *dst++ = *src++;
    }
}

// Erase a 64KB sector (must be in RAM - flash is unavailable during erase)
int RAMFUNC flash_erase_sector(uint32_t addr) {
    // Align to sector boundary
    addr &= 0xFF0000;

    // Send WREN first (required by flash chip)
    if (flash_write_enable() < 0) {
        return -1;
    }

    FLASH_WRITE_ADDR = addr;
    delay(100);
    FLASH_WRITE_CTRL = CMD_SE;

    return flash_wait_complete();
}

// Program bytes to flash (must be in RAM - flash is unavailable during program)
int RAMFUNC flash_program_bytes(uint32_t addr, const void *data, size_t len) {
    const uint8_t *src = (const uint8_t *)data;

    while (len--) {
        // Send WREN before each byte (WEL is cleared after each PP)
        if (flash_write_enable() < 0) {
            return -1;
        }

        FLASH_WRITE_ADDR = addr++;
        delay(50);
        FLASH_WRITE_DATA = *src++;
        FLASH_WRITE_CTRL = CMD_PP;

        if (flash_wait_complete() < 0) {
            return -1;
        }
    }

    return 0;
}
