/* Copyright (c) 2025 Martin R. Raumann */
/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Flash Write XIP Test Firmware
 *
 * Tests flash erase and program operations via the 0x81000000 register
 * interface while executing in-place from flash at 0x80100100.
 *
 * Register map (0x81000000):
 *   +0x0  CTRL   (W): bit0=WREN, bit1=SE, bit2=PP, bit3=RDSR
 *   +0x4  ADDR   (R/W): 24-bit flash address
 *   +0x8  DATA   (R/W): 8-bit write data (for PP)
 *   +0xC  STATUS (R): bit1=BUSY, bit3=ERROR
 *
 * Expected UART output (searched by testbench):
 *   "*** TEST PASSED ***"  or  "*** TEST FAILED ***"
 */

// UART Wishbone registers
#define UART_BASE     0x40000000
#define UART_TX_DATA  (*(volatile unsigned int*)(UART_BASE + 0x00))
#define UART_STATUS   (*(volatile unsigned int*)(UART_BASE + 0x08))
#define TX_READY      (1u << 0)

// Flash write controller registers (0x81000000)
#define FW_BASE       0x81000000u
#define FW_CTRL       (*(volatile unsigned int*)(FW_BASE + 0x00))
#define FW_ADDR       (*(volatile unsigned int*)(FW_BASE + 0x04))
#define FW_DATA       (*(volatile unsigned int*)(FW_BASE + 0x08))
#define FW_STATUS     (*(volatile unsigned int*)(FW_BASE + 0x0C))

// CTRL register bits
#define CMD_WREN  (1u << 0)
#define CMD_SE    (1u << 1)
#define CMD_PP    (1u << 2)
#define CMD_RDSR  (1u << 3)

// STATUS register bits
#define STATUS_BUSY   (1u << 1)
#define STATUS_ERROR  (1u << 3)

// Test flash sector at 2MB physical (well above 1MB firmware region)
#define TEST_SECTOR_ADDR  0x00200000u
#define TEST_PAGE_ADDR    0x00200000u

// -------------------------------------------------------------------------
// UART helpers
// -------------------------------------------------------------------------
static void uart_putc(char c) {
    while (!(UART_STATUS & TX_READY));
    UART_TX_DATA = (unsigned int)(unsigned char)c;
}

static void uart_puts(const char *s) {
    while (*s) uart_putc(*s++);
}

static void uart_puthex8(unsigned int v) {
    static const char hex[] = "0123456789ABCDEF";
    uart_putc(hex[(v >> 28) & 0xF]);
    uart_putc(hex[(v >> 24) & 0xF]);
    uart_putc(hex[(v >> 20) & 0xF]);
    uart_putc(hex[(v >> 16) & 0xF]);
    uart_putc(hex[(v >> 12) & 0xF]);
    uart_putc(hex[(v >>  8) & 0xF]);
    uart_putc(hex[(v >>  4) & 0xF]);
    uart_putc(hex[(v      ) & 0xF]);
}

// -------------------------------------------------------------------------
// Flash write controller helpers
// -------------------------------------------------------------------------

// Poll until BUSY clears; return 0 on success, -1 on error/timeout
static int fw_wait_done(void) {
    unsigned int timeout = 0xFFFFFFu;  // ~16M iterations
    while (timeout--) {
        unsigned int st = FW_STATUS;
        if (st & STATUS_ERROR) return -1;
        if (!(st & STATUS_BUSY)) return 0;
    }
    return -1;  // timeout
}

// Write Enable
static int fw_wren(void) {
    FW_CTRL = CMD_WREN;
    return fw_wait_done();
}

// Sector Erase (64KB) at given 24-bit address
static int fw_sector_erase(unsigned int addr) {
    if (fw_wren() != 0) return -1;
    FW_ADDR = addr & 0xFFFFFFu;
    FW_CTRL = CMD_SE;
    return fw_wait_done();
}

// Page Program: write up to 256 bytes starting at addr
static int fw_page_program(unsigned int addr, const unsigned char *data, unsigned int len) {
    unsigned int i;
    if (fw_wren() != 0) return -1;
    FW_ADDR = addr & 0xFFFFFFu;
    for (i = 0; i < len; i++) {
        FW_DATA = data[i];
        FW_CTRL = CMD_PP;
        if (fw_wait_done() != 0) return -1;
    }
    return 0;
}

// -------------------------------------------------------------------------
// Main
// -------------------------------------------------------------------------

static int failed = 0;

static void pass(const char *name) {
    uart_puts("  PASS: ");
    uart_puts(name);
    uart_puts("\r\n");
}

static void fail(const char *name, unsigned int got) {
    uart_puts("  FAIL: ");
    uart_puts(name);
    uart_puts(" got=0x");
    uart_puthex8(got);
    uart_puts("\r\n");
    failed = 1;
}

int main(void) {
    unsigned int st;
    int rc;

    uart_puts("\r\n");
    uart_puts("Flash Write XIP Test\r\n");
    uart_puts("====================\r\n");

    // ------------------------------------------------------------------
    // Test 1: STATUS register readable and not stuck busy at startup
    // ------------------------------------------------------------------
    uart_puts("\r\n[1] Initial status check\r\n");
    st = FW_STATUS;
    uart_puts("  STATUS=0x");
    uart_puthex8(st);
    uart_puts("\r\n");
    if (st & STATUS_BUSY) {
        fail("initial busy", st);
    } else if (st & STATUS_ERROR) {
        fail("initial error", st);
    } else {
        pass("initial status");
    }

    // ------------------------------------------------------------------
    // Test 2: Write Enable (WREN)
    // ------------------------------------------------------------------
    uart_puts("\r\n[2] Write Enable (WREN)\r\n");
    rc = fw_wren();
    if (rc != 0) {
        fail("WREN", (unsigned int)FW_STATUS);
    } else {
        pass("WREN");
    }

    // ------------------------------------------------------------------
    // Test 3: Sector Erase at TEST_SECTOR_ADDR
    // ------------------------------------------------------------------
    uart_puts("\r\n[3] Sector Erase at 0x");
    uart_puthex8(TEST_SECTOR_ADDR);
    uart_puts("\r\n");
    rc = fw_sector_erase(TEST_SECTOR_ADDR);
    if (rc != 0) {
        fail("sector erase", (unsigned int)FW_STATUS);
    } else {
        pass("sector erase");
    }

    // ------------------------------------------------------------------
    // Test 4: Page Program — write 8 bytes
    // ------------------------------------------------------------------
    uart_puts("\r\n[4] Page Program at 0x");
    uart_puthex8(TEST_PAGE_ADDR);
    uart_puts("\r\n");
    {
        static const unsigned char pattern[8] = {0xA5, 0x5A, 0xDE, 0xAD, 0xBE, 0xEF, 0x12, 0x34};
        rc = fw_page_program(TEST_PAGE_ADDR, pattern, 8);
        if (rc != 0) {
            fail("page program", (unsigned int)FW_STATUS);
        } else {
            pass("page program");
        }
    }

    // ------------------------------------------------------------------
    // Test 5: Verify STATUS is clean after all operations
    // ------------------------------------------------------------------
    uart_puts("\r\n[5] Final status check\r\n");
    st = FW_STATUS;
    uart_puts("  STATUS=0x");
    uart_puthex8(st);
    uart_puts("\r\n");
    if ((st & (STATUS_BUSY | STATUS_ERROR)) == 0) {
        pass("final status");
    } else {
        fail("final status", st);
    }

    // ------------------------------------------------------------------
    // Result
    // ------------------------------------------------------------------
    uart_puts("\r\n");
    if (failed) {
        uart_puts("*** TEST FAILED ***\r\n");
    } else {
        uart_puts("*** TEST PASSED ***\r\n");
    }

    // Spin forever (testbench inactivity watchdog will exit)
    while (1) {}
    return 0;
}
