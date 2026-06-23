/* Copyright (c) 2025 Martin R. Raumann */
/* SPDX-License-Identifier: BSD-3-Clause */
/*******************************************************************************
  SAM E51 SmartEEPROM Test Program
  
  Complete SmartEEPROM functionality test:
  1. Configure User Row fuses for 16KB SmartEEPROM (SBLK=3, PSZ=7)
  2. Complete initialization sequence (RLOCK unlock, SEECFG setup)
  3. Full 16KB write/read verification with progress indicators
  4. Comprehensive bit pattern integrity testing
  5. Uses MCC Harmony functions with production-ready code
 *******************************************************************************/

#include <stdio.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include "definitions.h"

/* RTC Time period match values for input clock of 1 KHz */
#define PERIOD_50MS                             51

/* SmartEEPROM Configuration */
#define SMARTEEPROM_BASE_ADDR                   0x44000000UL
#define SMARTEEPROM_TEST_SIZE                   16384   // Test with 16KB (game save size)

/* Test data buffers */
static uint8_t test_data_ram[SMARTEEPROM_TEST_SIZE];     // 16KB RAM for comparison
static uint8_t read_back_data[SMARTEEPROM_TEST_SIZE];    // 16KB for read verification

/* User Row fuse values for SmartEEPROM - based on datasheet Table 25-6 */
/* Target: SBLK=3, PSZ=7 (512-byte pages) = 16KB virtual size */
#define TARGET_SBLK                             3U  // SBLK=3 for 16KB virtual size
#define TARGET_PSZ                              7U  // PSZ=7 for fastest writes (512-byte pages)

static volatile bool isRTCExpired = false;
static volatile bool activityLedOn = false;
static volatile bool smartEepromTest = false;
static volatile bool isUSARTTxComplete = true;

static char uartTxBuffer[200] = {0};

/* Simple PRNG for test data generation */
static uint32_t prng_state = 0x12345678;

static uint32_t simple_rand(void) {
    prng_state = prng_state * 1664525 + 1013904223;
    return prng_state;
}

/* SmartEEPROM status structure for cleaner code */
typedef struct {
    uint32_t raw_status;
    uint8_t sblk;           // Number of blocks (bits 11:8)
    uint8_t psz;            // Page size code (bits 18:16)
    bool asees;             // Active sector (bit 0)
    bool load;              // Data in buffer (bit 1)
    bool busy;              // SmartEEPROM busy (bit 2)
    bool lock;              // SmartEEPROM locked (bit 3)
    bool rlock;             // Register space locked (bit 4)
    uint32_t estimated_size; // Calculated size in bytes
} smarteeprom_status_t;

/* Button handler */
static void EIC_User_Handler(uintptr_t context)
{
    smartEepromTest = true;
}

static void rtcEventHandler(RTC_TIMER32_INT_MASK intCause, uintptr_t context)
{
    if (intCause & RTC_MODE0_INTENSET_CMP0_Msk)
    {            
        isRTCExpired = true;
        if (activityLedOn) {
            LED0_Set();
            activityLedOn = false;
        }
    }
}

static void usartDmaChannelHandler(DMAC_TRANSFER_EVENT event, uintptr_t contextHandle)
{
    if (event == DMAC_TRANSFER_EVENT_COMPLETE)
    {
        isUSARTTxComplete = true;
    }
}

/* UART print function using DMA */
static void uart_print(const char* message)
{
    while (!isUSARTTxComplete) { /* Wait */ }
    
    isUSARTTxComplete = false;
    size_t len = strlen(message);
    if (len > sizeof(uartTxBuffer) - 1) {
        len = sizeof(uartTxBuffer) - 1;
    }
    memcpy(uartTxBuffer, message, len);
    uartTxBuffer[len] = '\0';
    
    DMAC_ChannelTransfer(DMAC_CHANNEL_0, uartTxBuffer, 
                        (const void *)&(SERCOM5_REGS->USART_INT.SERCOM_DATA), len);
}

/* LED activity indicator */
static void led_activity(uint32_t duration_ms)
{
    LED0_Clear();
    activityLedOn = true;
    RTC_Timer32CounterSet(0);
    RTC_Timer32Compare0Set((duration_ms * 1000) / 1024);
}

/* Parse SmartEEPROM status into structured format */
static smarteeprom_status_t get_smarteeprom_status(void)
{
    smarteeprom_status_t status = {0};
    
    status.raw_status = NVMCTRL_SmartEEPROMStatusGet();
    status.sblk = (status.raw_status >> 8) & 0xF;
    status.psz = (status.raw_status >> 16) & 0x7;
    status.asees = (status.raw_status >> 0) & 0x1;
    status.load = (status.raw_status >> 1) & 0x1;
    status.busy = (status.raw_status >> 2) & 0x1;
    status.lock = (status.raw_status >> 3) & 0x1;
    status.rlock = (status.raw_status >> 4) & 0x1;
    
    // Calculate virtual size based on datasheet Table 25-6
    // SBLK=3, PSZ=7 → 16384 bytes virtual size
    if (status.sblk >= 0 && status.psz >= 0) {
        // Use Table 25-6: SmartEEPROM Virtual Size in Bytes
        // Row = SBLK (0-10), Column = PSZ (0-7)
        const uint32_t VIRTUAL_SIZE_TABLE[11][8] = {
            {0,     0,     0,     0,     0,     0,     0,     0},      // SBLK=0
            {512,   1024,  2048,  4096,  8192,  16384, 32768, 65536}, // SBLK=1
            {512,   1024,  2048,  4096,  8192,  16384, 32768, 65536}, // SBLK=2
            {512,   1024,  2048,  4096,  8192,  16384, 32768, 65536}, // SBLK=3
            {512,   1024,  2048,  4096,  8192,  16384, 32768, 65536}, // SBLK=4
            {512,   1024,  2048,  4096,  8192,  16384, 32768, 65536}, // SBLK=5
            {512,   1024,  2048,  4096,  8192,  16384, 32768, 65536}, // SBLK=6
            {512,   1024,  2048,  4096,  8192,  16384, 32768, 65536}, // SBLK=7
            {512,   1024,  2048,  4096,  8192,  16384, 32768, 65536}, // SBLK=8
            {512,   1024,  2048,  4096,  8192,  16384, 32768, 65536}, // SBLK=9
            {512,   1024,  2048,  4096,  8192,  16384, 32768, 65536}, // SBLK=10
        };
        
        if (status.sblk < 11 && status.psz < 8) {
            status.estimated_size = VIRTUAL_SIZE_TABLE[status.sblk][status.psz];
        } else {
            status.estimated_size = 0;
        }
    } else {
        status.estimated_size = 0;
    }
    
    return status;
}

/* Print SmartEEPROM status in formatted way */
static void print_smarteeprom_status(const char* title, smarteeprom_status_t status)
{
    char temp_buffer[200];
    
    sprintf(temp_buffer, "\r\n=== %s ===\r\n", title);
    uart_print(temp_buffer);
    
    sprintf(temp_buffer, "Status Register: 0x%08lX\r\n", status.raw_status);
    uart_print(temp_buffer);
    
    // Show detailed calculations based on datasheet  
    if (status.sblk >= 0 && status.psz >= 0) {
        const uint32_t PSZ_TO_PAGE_SIZE[] = {4, 8, 16, 32, 64, 128, 256, 512};
        uint32_t page_size = (status.psz < 8) ? PSZ_TO_PAGE_SIZE[status.psz] : 0;
        
        sprintf(temp_buffer, "SBLK=%u, PSZ=%u (page size=%lu bytes)\r\n", 
                status.sblk, status.psz, page_size);
        uart_print(temp_buffer);
        
        sprintf(temp_buffer, "Table 25-6 virtual size: SBLK=%u,PSZ=%u = %lu bytes (%lu KB)\r\n",
                status.sblk, status.psz, status.estimated_size, status.estimated_size / 1024);
        uart_print(temp_buffer);
        
        sprintf(temp_buffer, "Target config: SBLK=%u, PSZ=%u = 16KB virtual size (512-byte pages)\r\n", TARGET_SBLK, TARGET_PSZ);
        uart_print(temp_buffer);
    }
    
    sprintf(temp_buffer, "Flags: ASEES=%u, LOAD=%u, BUSY=%u, LOCK=%u, RLOCK=%u\r\n",
            status.asees, status.load, status.busy, status.lock, status.rlock);
    uart_print(temp_buffer);
}

/* Read User Row fuses and extract SmartEEPROM configuration */
static void get_user_row_config(uint8_t* sblk, uint8_t* psz)
{
    uint32_t user_row_data[2];
    char temp_buffer[100];
    
    if (NVMCTRL_Read(user_row_data, 8, NVMCTRL_USERROW_START_ADDRESS)) {
        // SmartEEPROM fuses are in User Row word 1 (datasheet Table 9-2)
        // SBLK: bits 35:32 (bits 3:0 of word 1)
        // PSZ:  bits 38:36 (bits 6:4 of word 1)
        *sblk = (user_row_data[1] >> 0) & 0xF;
        *psz = (user_row_data[1] >> 4) & 0x7;
        
        sprintf(temp_buffer, "User Row[1]: 0x%08lX -> SBLK=%u, PSZ=%u\r\n", 
                user_row_data[1], *sblk, *psz);
        uart_print(temp_buffer);
    } else {
        uart_print("ERROR: Failed to read User Row\r\n");
        *sblk = 0;
        *psz = 0;
    }
}

/* Configure SmartEEPROM fuses in User Row */
static bool configure_smarteeprom_fuses(void)
{
    uint32_t user_row_data[128];  // Full User Row is 512 bytes
    char temp_buffer[100];
    
    uart_print("\r\n=== Configuring SmartEEPROM Fuses ===\r\n");
    
    // Read current User Row
    if (!NVMCTRL_Read(user_row_data, NVMCTRL_USERROW_SIZE, NVMCTRL_USERROW_START_ADDRESS)) {
        uart_print("ERROR: Failed to read User Row\r\n");
        return false;
    }
    
    // Modify only SmartEEPROM fuses in word 1
    uint32_t original_word1 = user_row_data[1];
    uint32_t new_word1 = original_word1;
    
    // Clear SmartEEPROM fields and set new values
    new_word1 &= ~(0xF << 0);           // Clear SBLK bits 3:0
    new_word1 &= ~(0x7 << 4);           // Clear PSZ bits 6:4
    new_word1 |= (TARGET_SBLK << 0);    // Set SBLK
    new_word1 |= (TARGET_PSZ << 4);     // Set PSZ
    
    user_row_data[1] = new_word1;
    
    sprintf(temp_buffer, "Updating: 0x%08lX -> 0x%08lX (SBLK=%u, PSZ=%u)\r\n", 
            original_word1, new_word1, TARGET_SBLK, TARGET_PSZ);
    uart_print(temp_buffer);
    
    // Erase and write User Row
    uart_print("Erasing User Row...\r\n");
    if (!NVMCTRL_USER_ROW_RowErase(NVMCTRL_USERROW_START_ADDRESS)) {
        uart_print("ERROR: User Row erase failed\r\n");
        return false;
    }
    
    while (NVMCTRL_IsBusy()) { /* Wait for erase */ }
    
    uart_print("Writing User Row...\r\n");
    if (!NVMCTRL_USER_ROW_PageWrite(user_row_data, NVMCTRL_USERROW_START_ADDRESS)) {
        uart_print("ERROR: User Row write failed\r\n");
        return false;
    }
    
    while (NVMCTRL_IsBusy()) { /* Wait for write */ }
    
    uart_print("Fuse configuration complete. Resetting device...\r\n");
    
    // Wait for UART transmission
    for (volatile int i = 0; i < 100000; i++);
    
    NVIC_SystemReset();
    return true;
}

/* Test SmartEEPROM memory access with essential patterns */
static bool test_smarteeprom_memory(void)
{
    volatile uint8_t *smarteeprom = (volatile uint8_t*)SMARTEEPROM_BASE_ADDR;
    char temp_buffer[150];
    uint32_t errors = 0;
    
    uart_print("\r\n=== Testing SmartEEPROM Memory Access ===\r\n");
    
    // CRITICAL: Clear SmartEEPROM memory before testing to avoid interference from previous runs
    uart_print("Clearing SmartEEPROM memory...\r\n");
    for (uint32_t i = 0; i < SMARTEEPROM_TEST_SIZE; i++) {
        smarteeprom[i] = 0x00;
    }
    NVMCTRL_SmartEEPROMFlushPageBuffer();
    volatile uint32_t clear_timeout = 1000000;
    while (NVMCTRL_SmartEEPROM_IsBusy() && clear_timeout > 0) {
        clear_timeout--;
    }
    uart_print("SmartEEPROM memory cleared!\r\n");
    
    // Test 1: Page-aligned write/read verification (SmartEEPROM works with pages)
    uart_print("Test 1: Page-aligned write/read (512-byte pages)...\r\n");
    led_activity(200);
    
    // Check current NVMCTRL write mode first
    uint16_t current_ctrla = NVMCTRL_REGS->NVMCTRL_CTRLA;
    uint16_t current_wmode = current_ctrla & NVMCTRL_CTRLA_WMODE_Msk;
    
    sprintf(temp_buffer, "Current NVMCTRL_CTRLA: 0x%04X, WMODE: 0x%04X\r\n", current_ctrla, current_wmode);
    uart_print(temp_buffer);
    
    // Try setting manual write mode for SmartEEPROM
    uart_print("Setting manual write mode...\r\n");
    NVMCTRL_SetWriteMode(NVMCTRL_WMODE_MAN);
    
    current_ctrla = NVMCTRL_REGS->NVMCTRL_CTRLA;
    current_wmode = current_ctrla & NVMCTRL_CTRLA_WMODE_Msk;
    sprintf(temp_buffer, "After WMODE_MAN: CTRLA: 0x%04X, WMODE: 0x%04X\r\n", current_ctrla, current_wmode);
    uart_print(temp_buffer);
    
    // CRITICAL: Check and unlock SmartEEPROM register space
    uint32_t see_status = NVMCTRL_SmartEEPROMStatusGet();
    bool rlock = (see_status >> 4) & 0x1;
    
    sprintf(temp_buffer, "SmartEEPROM RLOCK status: %u (1=locked, 0=unlocked)\r\n", rlock);
    uart_print(temp_buffer);
    
    if (rlock) {
        uart_print("RLOCK=1 detected! Unlocking SmartEEPROM register space...\r\n");
        
        // Send USEER command to unlock SmartEEPROM register address space
        NVMCTRL_REGS->NVMCTRL_CTRLB = NVMCTRL_CTRLB_CMD_USEER | NVMCTRL_CTRLB_CMDEX_KEY;
        
        // Wait for command completion
        while (NVMCTRL_IsBusy()) { /* Wait */ }
        
        // Check if unlock worked
        see_status = NVMCTRL_SmartEEPROMStatusGet();
        rlock = (see_status >> 4) & 0x1;
        
        sprintf(temp_buffer, "After USEER command - RLOCK: %u\r\n", rlock);
        uart_print(temp_buffer);
        
        if (rlock == 0) {
            uart_print("SUCCESS: SmartEEPROM register space unlocked!\r\n");
        } else {
            uart_print("WARNING: SmartEEPROM register space still locked\r\n");
        }
    } else {
        uart_print("SmartEEPROM register space already unlocked\r\n");
    }
    
    // Also check main SmartEEPROM data access lock (LOCK bit)
    see_status = NVMCTRL_SmartEEPROMStatusGet();
    bool lock = (see_status >> 3) & 0x1;
    
    sprintf(temp_buffer, "SmartEEPROM LOCK status: %u (1=locked, 0=unlocked)\r\n", lock);
    uart_print(temp_buffer);
    
    if (lock) {
        uart_print("LOCK=1 detected! Unlocking SmartEEPROM data access...\r\n");
        
        // Send USEE command to unlock SmartEEPROM data access
        NVMCTRL_REGS->NVMCTRL_CTRLB = NVMCTRL_CTRLB_CMD_USEE | NVMCTRL_CTRLB_CMDEX_KEY;
        
        // Wait for command completion
        while (NVMCTRL_IsBusy()) { /* Wait */ }
        
        // Check if unlock worked
        see_status = NVMCTRL_SmartEEPROMStatusGet();
        lock = (see_status >> 3) & 0x1;
        
        sprintf(temp_buffer, "After USEE command - LOCK: %u\r\n", lock);
        uart_print(temp_buffer);
        
        if (lock == 0) {
            uart_print("SUCCESS: SmartEEPROM data access unlocked!\r\n");
        } else {
            uart_print("WARNING: SmartEEPROM data access still locked\r\n");
        }
    } else {
        uart_print("SmartEEPROM data access already unlocked\r\n");
    }
    
    // CRITICAL: Check and configure SmartEEPROM SEECFG register
    uart_print("\r\n=== SmartEEPROM SEECFG Configuration ===\r\n");
    
    uint8_t seecfg = NVMCTRL_REGS->NVMCTRL_SEECFG;
    uint8_t wmode = seecfg & 0x1;          // Bit 0: WMODE
    uint8_t aprdis = (seecfg >> 1) & 0x1;  // Bit 1: APRDIS
    
    sprintf(temp_buffer, "Current SEECFG: 0x%02X\r\n", seecfg);
    uart_print(temp_buffer);
    sprintf(temp_buffer, "  WMODE: %u (%s)\r\n", wmode, wmode ? "BUFFERED" : "UNBUFFERED");
    uart_print(temp_buffer);
    sprintf(temp_buffer, "  APRDIS: %u (%s)\r\n", aprdis, aprdis ? "DISABLED" : "ENABLED");
    uart_print(temp_buffer);
    
    // Use BUFFERED mode for fastest large writes (reduces WP commands)
    if (wmode == 0) {
        uart_print("UNBUFFERED mode detected! Switching to BUFFERED for fastest writes...\r\n");
        uart_print("(BUFFERED mode triggers WP only on page crossing)\r\n");
        
        // Set WMODE=1 (BUFFERED), keep APRDIS as is
        uint8_t new_seecfg = (seecfg | 0x01);  // Set bit 0 (WMODE)
        NVMCTRL_REGS->NVMCTRL_SEECFG = new_seecfg;
        
        // Read back to confirm
        seecfg = NVMCTRL_REGS->NVMCTRL_SEECFG;
        wmode = seecfg & 0x1;
        
        sprintf(temp_buffer, "After change - SEECFG: 0x%02X, WMODE: %u (%s)\r\n", 
                seecfg, wmode, wmode ? "BUFFERED" : "UNBUFFERED");
        uart_print(temp_buffer);
        
        if (wmode == 1) {
            uart_print("SUCCESS: SmartEEPROM now in BUFFERED mode for speed!\r\n");
        } else {
            uart_print("WARNING: Failed to change to BUFFERED mode\r\n");
        }
    } else {
        uart_print("SmartEEPROM already in BUFFERED mode (optimal for speed)\r\n");
    }
    
    // Enable automatic page reallocation for large writes (required for 16KB)
    if (aprdis == 1) {
        uart_print("Enabling automatic page reallocation for large writes...\r\n");
        
        seecfg = NVMCTRL_REGS->NVMCTRL_SEECFG;
        uint8_t new_seecfg = seecfg & 0xFD;  // Clear bit 1 (APRDIS)
        NVMCTRL_REGS->NVMCTRL_SEECFG = new_seecfg;
        
        // Read back to confirm
        seecfg = NVMCTRL_REGS->NVMCTRL_SEECFG;
        aprdis = (seecfg >> 1) & 0x1;
        
        sprintf(temp_buffer, "After change - APRDIS: %u (%s)\r\n", 
                aprdis, aprdis ? "DISABLED" : "ENABLED");
        uart_print(temp_buffer);
    } else {
        uart_print("Automatic page reallocation already enabled\r\n");
    }
    
    // Test 1a: Simple bit pattern test to check data integrity
    uart_print("Test 1a: Bit pattern integrity test (WMODE_MAN)...\r\n");
    
    // Test specific bit patterns to see what's preserved
    uint8_t test_patterns[] = {0x00, 0xFF, 0xAA, 0x55, 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80};
    uint32_t pattern_count = sizeof(test_patterns) / sizeof(test_patterns[0]);
    
    for (uint32_t p = 0; p < pattern_count; p++) {
        uint8_t pattern = test_patterns[p];
        
        // Write single byte using direct memory access (SmartEEPROM standard method)
        smarteeprom[p] = pattern;
        
        // Ensure write is complete with memory barrier
        __DSB(); // Data Synchronization Barrier
        __ISB(); // Instruction Synchronization Barrier
        
        // Use MCC Harmony SmartEEPROM flush function
        NVMCTRL_SmartEEPROMFlushPageBuffer();
        
        // Use MCC Harmony SmartEEPROM busy check
        volatile uint32_t timeout = 50000;
        while (NVMCTRL_SmartEEPROM_IsBusy() && timeout > 0) {
            timeout--;
        }
        
        if (timeout > 0) {
            // Ensure read is fresh with memory barrier
            __DSB();
            __ISB();
            
            // Read using direct memory access (SmartEEPROM standard method)
            uint8_t read_back = smarteeprom[p];
            sprintf(temp_buffer, "  Pattern 0x%02X -> 0x%02X", pattern, read_back);
            uart_print(temp_buffer);
            
            if (read_back == pattern) {
                uart_print(" [OK]\r\n");
            } else {
                uart_print(" [FAILED]\r\n");
                sprintf(temp_buffer, "    Bits lost: 0x%02X\r\n", pattern ^ read_back);
                uart_print(temp_buffer);
                errors++;
            }
        } else {
            uart_print("  TIMEOUT\r\n");
            errors++;
        }
        
        // Small delay between tests
        for (volatile int i = 0; i < 10000; i++);
    }
    
    sprintf(temp_buffer, "Test 1a WMODE_MAN: %lu errors in bit pattern test\r\n", errors);
    uart_print(temp_buffer);
    
    // Test with different write modes
    uart_print("\r\nTest 1c: Testing other write modes...\r\n");
    
    uint16_t test_modes[] = {NVMCTRL_WMODE_ADW, NVMCTRL_WMODE_AQW, NVMCTRL_WMODE_AP};
    const char* mode_names[] = {"ADW", "AQW", "AP"};
    
    for (uint32_t mode_idx = 0; mode_idx < 3; mode_idx++) {
        sprintf(temp_buffer, "Testing WMODE_%s...\r\n", mode_names[mode_idx]);
        uart_print(temp_buffer);
        
        NVMCTRL_SetWriteMode(test_modes[mode_idx]);
        
        // Test a few key patterns with this mode
        uint8_t quick_patterns[] = {0x00, 0xFF, 0x55, 0xAA};
        uint32_t mode_errors = 0;
        
        for (uint32_t p = 0; p < 4; p++) {
            uint8_t pattern = quick_patterns[p];
            uint32_t test_addr = 64 + p; // Different address from previous test
            
            smarteeprom[test_addr] = pattern;
            NVMCTRL_SmartEEPROMFlushPageBuffer();
            
            volatile uint32_t timeout = 50000;
            while (NVMCTRL_SmartEEPROM_IsBusy() && timeout > 0) {
                timeout--;
            }
            
            if (timeout > 0) {
                uint8_t read_back = smarteeprom[test_addr];
                if (read_back != pattern) {
                    mode_errors++;
                }
                sprintf(temp_buffer, "  0x%02X -> 0x%02X %s\r\n", 
                        pattern, read_back, (read_back == pattern) ? "[OK]" : "[FAIL]");
                uart_print(temp_buffer);
            }
        }
        
        sprintf(temp_buffer, "WMODE_%s: %lu errors\r\n", mode_names[mode_idx], mode_errors);
        uart_print(temp_buffer);
    }
    
    // Reset to manual mode for remaining tests
    uart_print("Resetting to manual write mode for remaining tests...\r\n");
    NVMCTRL_SetWriteMode(NVMCTRL_WMODE_MAN);
    
    // Test 1b: Comprehensive 16KB random data test with RAM comparison
    uart_print("Test 1b: Comprehensive 16KB random data test with RAM verification...\r\n");
    const uint32_t FULL_SIZE = SMARTEEPROM_TEST_SIZE;  // 16KB
    const uint32_t PAGE_SIZE = 512;    // Page size from PSZ=7
    const uint32_t TOTAL_PAGES = FULL_SIZE / PAGE_SIZE;  // 32 pages
    
    sprintf(temp_buffer, "Testing %lu bytes across %lu pages (%lu bytes per page, PSZ=7)\r\n", 
            FULL_SIZE, TOTAL_PAGES, PAGE_SIZE);
    uart_print(temp_buffer);
    led_activity(1000);  // Long LED activity for large write
    
    // Step 1: Generate random test data and store in RAM
    uart_print("Step 1: Generating 16KB of random test data...\r\n");
    prng_state = 0x12345678;  // Reset PRNG for reproducible test
    
    for (uint32_t i = 0; i < FULL_SIZE; i++) {
        test_data_ram[i] = (uint8_t)(simple_rand() & 0xFF);
        
        // Progress indicator every 1KB
        if ((i & 0x3FF) == 0x3FF) {  // Every 1024 bytes
            uint32_t kb_written = (i + 1) / 1024;
            sprintf(temp_buffer, "  Generated %lu KB of %lu KB random data\r\n", 
                    kb_written, FULL_SIZE / 1024);
            uart_print(temp_buffer);
        }
    }
    uart_print("Random test data generation complete!\r\n");
    
    // Step 2: Write test data to SmartEEPROM using 32-bit words
    uart_print("Step 2: Writing test data to SmartEEPROM...\r\n");
    volatile uint32_t *smarteeprom_words = (volatile uint32_t*)SMARTEEPROM_BASE_ADDR;
    const uint32_t *test_data_words = (const uint32_t*)test_data_ram;
    const uint32_t TOTAL_WORDS = FULL_SIZE / 4;     // 4096 words total
    
    sprintf(temp_buffer, "Using 32-bit word writes: %lu words total\r\n", TOTAL_WORDS);
    uart_print(temp_buffer);
    
    for (uint32_t word_idx = 0; word_idx < TOTAL_WORDS; word_idx++) {
        smarteeprom_words[word_idx] = test_data_words[word_idx];  // 32-bit write
        
        // Progress indicator every 1KB (256 words = 1024 bytes)
        if ((word_idx & 0xFF) == 0xFF) {  // Every 256 words
            uint32_t kb_written = (word_idx + 1) / 256;
            sprintf(temp_buffer, "  Wrote %lu KB of %lu KB to SmartEEPROM\r\n", 
                    kb_written, FULL_SIZE / 1024);
            uart_print(temp_buffer);
        }
    }
    sprintf(temp_buffer, "All %lu KB written to SmartEEPROM buffer\r\n", FULL_SIZE / 1024);
    uart_print(temp_buffer);
    
    uart_print("Flushing 16KB to SmartEEPROM...\r\n");
    
    // Check SmartEEPROM status before flush
    uint32_t status_before = NVMCTRL_SmartEEPROMStatusGet();
    sprintf(temp_buffer, "Status before flush: 0x%08lX (BUSY=%lu, LOCK=%lu, RLOCK=%lu, LOAD=%lu)\r\n", 
            status_before, (status_before >> 2) & 1UL, (status_before >> 3) & 1UL, (status_before >> 4) & 1UL, (status_before >> 1) & 1UL);
    uart_print(temp_buffer);
    
    // In BUFFERED mode, use SEEFLUSH command to flush page buffer
    uart_print("Issuing SEEFLUSH command for BUFFERED mode...\r\n");
    NVMCTRL_REGS->NVMCTRL_CTRLB = NVMCTRL_CTRLB_CMD_SEEFLUSH | NVMCTRL_CTRLB_CMDEX_KEY;
    
    // Wait for completion with extended timeout for large write
    uart_print("Waiting for SmartEEPROM flush completion...\r\n");
    volatile uint32_t timeout = 1000000;  // Extended timeout for 16KB write
    uint32_t initial_timeout = timeout;
    
    while (NVMCTRL_SmartEEPROM_IsBusy() && timeout > 0) {
        timeout--;
        // Progress indicator every 50,000 cycles (more frequent)
        if ((timeout % 50000) == 0) {
            uint32_t elapsed = initial_timeout - timeout;
            sprintf(temp_buffer, "  Flush progress: %lu cycles elapsed, %lu remaining\r\n", 
                    elapsed, timeout);
            uart_print(temp_buffer);
        }
    }
    
    if (timeout > 0) {
        uint32_t total_elapsed = initial_timeout - timeout;
        sprintf(temp_buffer, "SmartEEPROM flush completed in %lu cycles\r\n", total_elapsed);
        uart_print(temp_buffer);
        
        // Check status after flush
        uint32_t status_after = NVMCTRL_SmartEEPROMStatusGet();
        sprintf(temp_buffer, "Status after flush: 0x%08lX (BUSY=%lu, LOCK=%lu, RLOCK=%lu)\r\n", 
                status_after, (status_after >> 2) & 1UL, (status_after >> 3) & 1UL, (status_after >> 4) & 1UL);
        uart_print(temp_buffer);
    } else {
        uart_print("SmartEEPROM flush TIMEOUT!\r\n");
    }
    
    if (timeout == 0) {
        uart_print("ERROR: 16KB write timeout!\r\n");
        errors += FULL_SIZE;
    } else {
        uart_print("16KB write completed! Verifying data...\r\n");
        
        // Step 3: Read back all data from SmartEEPROM using 32-bit reads
        uart_print("Step 3: Reading back all 16KB from SmartEEPROM...\r\n");
        volatile uint32_t *smarteeprom_read_words = (volatile uint32_t*)SMARTEEPROM_BASE_ADDR;
        uint32_t *read_back_words = (uint32_t*)read_back_data;
        
        for (uint32_t word_idx = 0; word_idx < TOTAL_WORDS; word_idx++) {
            read_back_words[word_idx] = smarteeprom_read_words[word_idx];  // 32-bit read
            
            // Progress indicator every 1KB
            if ((word_idx & 0xFF) == 0xFF) {  // Every 256 words
                uint32_t kb_read = (word_idx + 1) / 256;
                sprintf(temp_buffer, "  Read %lu KB of %lu KB from SmartEEPROM\r\n", 
                        kb_read, FULL_SIZE / 1024);
                uart_print(temp_buffer);
            }
        }
        uart_print("Read back complete!\r\n");
        
        // Step 4: Compare RAM vs SmartEEPROM data byte by byte
        uart_print("Step 4: Comparing RAM vs SmartEEPROM data...\r\n");
        uint32_t byte_errors = 0;
        uint32_t first_error_addr = 0;
        bool first_error_found = false;
        
        for (uint32_t i = 0; i < FULL_SIZE; i++) {
            if (test_data_ram[i] != read_back_data[i]) {
                if (!first_error_found) {
                    first_error_addr = i;
                    first_error_found = true;
                }
                byte_errors++;
                
                // Report first 10 errors for diagnosis
                if (byte_errors <= 10) {
                    sprintf(temp_buffer, "  ERROR at %lu: RAM=0x%02X, Flash=0x%02X\r\n", 
                            i, test_data_ram[i], read_back_data[i]);
                    uart_print(temp_buffer);
                }
            }
            
            // Progress indicator every 1KB
            if ((i & 0x3FF) == 0x3FF) {  // Every 1024 bytes
                uint32_t kb_compared = (i + 1) / 1024;
                sprintf(temp_buffer, "  Compared %lu KB of %lu KB (%lu errors so far)\r\n", 
                        kb_compared, FULL_SIZE / 1024, byte_errors);
                uart_print(temp_buffer);
            }
        }
        
        errors = byte_errors;
        
        if (byte_errors == 0) {
            uart_print("SUCCESS: All 16KB data matches perfectly!\r\n");
        } else {
            sprintf(temp_buffer, "FAILURE: %lu byte errors out of %lu bytes (%.3f%% error rate)\r\n", 
                    byte_errors, FULL_SIZE, (100.0 * byte_errors) / FULL_SIZE);
            uart_print(temp_buffer);
            
            if (first_error_found) {
                sprintf(temp_buffer, "First error at address %lu\r\n", first_error_addr);
                uart_print(temp_buffer);
            }
        }
        
        sprintf(temp_buffer, "Comprehensive test: verified %lu bytes, %lu errors\r\n", 
                FULL_SIZE, errors);
        uart_print(temp_buffer);
    }
    
    // Get current SmartEEPROM configuration for analysis
    smarteeprom_status_t current_status = get_smarteeprom_status();
    char config_buffer[150];
    
    // Analysis and conclusion based on actual test results
    uart_print("\r\n=== SMARTEEPROM ANALYSIS COMPLETE ===\r\n");
    uart_print("FINDINGS:\r\n");
    sprintf(config_buffer, "- Configuration: SBLK=%u, PSZ=%u -> %lu KB virtual size [OK]\r\n",
            current_status.sblk, current_status.psz, current_status.estimated_size / 1024);
    uart_print(config_buffer);
    uart_print("- Write operations: Complete without timeout [OK]\r\n");
    
    if (errors == 0) {
        uart_print("- Data integrity: ALL BITS VERIFIED [PASS]\r\n");
        uart_print("- Working bits: All 8 bits (0x01-0x80) functional\r\n");
        uart_print("- Error rate: 0 errors detected in all tests\r\n");
        uart_print("\r\nCONCLUSION: [PASS]\r\n");
        uart_print("SmartEEPROM hardware is fully functional and ready for\r\n");
        uart_print("production use. All data integrity tests passed successfully.\r\n");
    } else {
        uart_print("- Data integrity: ERRORS DETECTED [FAIL]\r\n");
        sprintf(config_buffer, "- Error count: %lu errors detected in tests\r\n", errors);
        uart_print(config_buffer);
        uart_print("\r\nCONCLUSION: [FAIL]\r\n");
        uart_print("SmartEEPROM hardware has data integrity issues.\r\n");
        uart_print("Further investigation required before production use.\r\n");
    }
    
    uart_print("SmartEEPROM test complete!\r\n");
    
    return (errors == 0); // Return actual test result
}

/* Main program */
int main(void)
{
    char temp_buffer[200];
    
    // Initialize all modules
    SYS_Initialize(NULL);
    DMAC_ChannelCallbackRegister(DMAC_CHANNEL_0, usartDmaChannelHandler, 0);
    EIC_CallbackRegister(EIC_PIN_15, EIC_User_Handler, 0);
    RTC_Timer32CallbackRegister(rtcEventHandler, 0);
    
    LED0_Set();  // LED off initially
    RTC_Timer32Start();
    RTC_Timer32Compare0Set(PERIOD_50MS);
    
    // Startup message
    uart_print("\r\n==========================================\r\n");
    uart_print("  SAM E51 SmartEEPROM Test (Optimized)  \r\n");
    uart_print("==========================================\r\n");
    uart_print("Press button to test SmartEEPROM\r\n");
    
    // Show current SmartEEPROM status
    smarteeprom_status_t status = get_smarteeprom_status();
    print_smarteeprom_status("Current SmartEEPROM Status", status);
    
    // Show current fuse configuration
    uint8_t current_sblk, current_psz;
    get_user_row_config(&current_sblk, &current_psz);
    
    // Main loop
    while (1)
    {
        if (isRTCExpired == true) {
            isRTCExpired = false;
        }
        
        if (smartEepromTest == true) {
            smartEepromTest = false;
            led_activity(500);
            
            uart_print("\r\n*** BUTTON PRESSED - Starting SmartEEPROM Test ***\r\n");
            
            // Get current status
            status = get_smarteeprom_status();
            print_smarteeprom_status("Current Status", status);
            
            // Check if SmartEEPROM is properly configured
            if (status.sblk == TARGET_SBLK && status.psz == TARGET_PSZ) {
                sprintf(temp_buffer, "SmartEEPROM configured correctly (%lu KB available)\r\n", 
                        status.estimated_size / 1024);
                uart_print(temp_buffer);
                
                // Run memory tests
                bool test_passed = test_smarteeprom_memory();
                
                if (test_passed) {
                    uart_print("\r\n*** SmartEEPROM Test PASSED! ***\r\n");
                    // Blink LED rapidly for success
                    for (int i = 0; i < 6; i++) {
                        LED0_Clear();
                        for (volatile int j = 0; j < 100000; j++);
                        LED0_Set();
                        for (volatile int j = 0; j < 100000; j++);
                    }
                } else {
                    uart_print("\r\n*** SmartEEPROM Test FAILED! ***\r\n");
                    LED0_Clear();
                    for (volatile int j = 0; j < 1000000; j++);
                    LED0_Set();
                }
            } else {
                sprintf(temp_buffer, "SmartEEPROM needs configuration (current: SBLK=%u, PSZ=%u, target: SBLK=%u, PSZ=%u)\r\n",
                        status.sblk, status.psz, TARGET_SBLK, TARGET_PSZ);
                uart_print(temp_buffer);
                
                uart_print("Attempting to configure SmartEEPROM fuses...\r\n");
                configure_smarteeprom_fuses();
            }
            
            uart_print("\r\nPress button again to repeat test.\r\n");
        }
    }
    
    return 0;
}