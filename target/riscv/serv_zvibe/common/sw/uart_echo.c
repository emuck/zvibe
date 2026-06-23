/* Copyright (c) 2025 Martin R. Raumann */
/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * UART Echo - Unified Implementation
 *
 * Supports both RAM and XIP (flash) execution.
 * Build with -DXIP_MODE for flash execution.
 *
 * Features:
 * - Backspace/delete support (0x08 and 0x7F)
 * - Line buffering with echo
 * - Buffer re-print on Enter
 */

// UART Wishbone registers
#define UART_BASE     0x40000000
#define UART_TX_DATA  (*(volatile unsigned int*)(UART_BASE + 0x00))
#define UART_RX_DATA  (*(volatile unsigned int*)(UART_BASE + 0x04))
#define UART_STATUS   (*(volatile unsigned int*)(UART_BASE + 0x08))

#define TX_READY  (1 << 0)
#define RX_READY  (1 << 1)

// Blocking write - poll TX_READY
static void uart_putc(char c) {
    while (!(UART_STATUS & TX_READY));
    UART_TX_DATA = c;
}

static void uart_puts(const char *s) {
    while (*s) {
        uart_putc(*s++);
    }
}

int main(void) {
    char buffer[128];
    unsigned int pos = 0;

    // Print banner
    uart_puts("\r\n==============================\r\n");
#ifdef XIP_MODE
    uart_puts("UART Echo Test (XIP Mode)\r\n");
    uart_puts("Running from Flash\r\n");
#else
    uart_puts("UART Echo Test (RAM Mode)\r\n");
    uart_puts("Running from RAM\r\n");
#endif
    uart_puts("==============================\r\n");
    uart_puts("> ");

    // Main loop with line editing
    while (1) {
        if (UART_STATUS & RX_READY) {
            char c = (char)(UART_RX_DATA & 0xFF);

            // Handle Enter/Return
            if (c == '\r' || c == '\n') {
                uart_puts("\r\n");

                // Re-print the buffer
                if (pos > 0) {
                    for (unsigned int i = 0; i < pos; i++) {
                        uart_putc(buffer[i]);
                    }
                    uart_puts("\r\n");
                }

                // Reset buffer and show new prompt
                pos = 0;
                uart_puts("> ");
            }
            // Handle Backspace or Delete
            else if (c == '\b' || c == 0x7F) {
                if (pos > 0) {
                    pos--;
                    uart_puts("\b \b");  // Erase character on screen
                }
            }
            // Handle printable ASCII
            else if (c >= 32 && c < 127) {
                if (pos < sizeof(buffer) - 1) {
                    buffer[pos++] = c;
                    uart_putc(c);  // Echo character
                }
            }
        }
    }

    return 0;
}
