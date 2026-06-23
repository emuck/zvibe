/* Copyright (c) 2025 Martin R. Raumann */
/* SPDX-License-Identifier: BSD-3-Clause */
/*******************************************************************************
  zvibe Z-Machine with SmartEEPROM Save/Restore - SAM E51 Production
  SAM E51 Curiosity Nano Board - Final Integration
*******************************************************************************/

#include <stdio.h>
#include <stddef.h>                     
#include <stdbool.h>                    
#include <stdlib.h>                     
#include <string.h>
#include "board_support.h"
#include "../../../include/zvibe_api.h"

/* External story data (generated at build time) */
extern const uint8_t story_data[];
extern const size_t story_data_size;

/* RTC Time period match values for input clock of 1 KHz */
#define PERIOD_50MS                             51

static volatile bool isRTCExpired = false;
static volatile bool activityLedOn = false;
static volatile bool zvibeReset = false;

#define RX_BUFFER_SIZE 50
static char rxBuffer[RX_BUFFER_SIZE];
static volatile uint16_t rxIndex = 0;
static volatile bool stringReady = false;

/* zvibe context - global for reset functionality */
static zvibeContext *g_zvibe_ctx = NULL;
static bool zvibe_initialized = false;

/* Save/restore configuration - minimal buffer for restore operations only */
#define MAX_SAVE_SIZE    16384  /* Maximum expected save size */

/* SmartEEPROM Configuration */
#define SMARTEEPROM_BASE_ADDR       0x44000000UL
#define SMARTEEPROM_SIZE           16384   // 16KB available
#define SAVE_MAGIC_HEADER          0x5A564942  // "ZVIB" in hex

/* Target fuse values for 16KB SmartEEPROM */
#define TARGET_SBLK                 3U  // SBLK=3 for 16KB virtual size
#define TARGET_PSZ                  7U  // PSZ=7 (512-byte pages) for fastest writes

/* SmartEEPROM save header structure */
typedef struct {
    uint32_t magic;
    uint32_t data_size;
    uint32_t checksum;
} save_header_t;

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
static bool smarteeprom_init(void) {
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

/* Calculate checksum */
static uint32_t calculate_checksum(const void *data, size_t size) {
    const uint8_t *bytes = (const uint8_t*)data;
    uint32_t checksum = 0;
    for (size_t i = 0; i < size; i++) {
        checksum = (checksum << 1) ^ bytes[i];
    }
    return checksum;
}

/* Write data to SmartEEPROM using optimized 32-bit word writes */
static bool smarteeprom_write_data(uint32_t offset, const void *data, size_t size) {
    if (offset + size > SMARTEEPROM_SIZE) return false;
    
    const uint8_t *src = (const uint8_t*)data;
    
    // Handle word-aligned portion with 32-bit writes
    if (size >= 4 && (offset & 3) == 0) {
        volatile uint32_t *smarteeprom_words = (volatile uint32_t*)(SMARTEEPROM_BASE_ADDR + offset);
        const uint32_t *src_words = (const uint32_t*)data;
        size_t word_count = size / 4;
        
        for (size_t i = 0; i < word_count; i++) {
            smarteeprom_words[i] = src_words[i];
        }
        
        // Handle remaining bytes
        size_t remaining = size % 4;
        if (remaining > 0) {
            volatile uint8_t *smarteeprom_bytes = (volatile uint8_t*)(SMARTEEPROM_BASE_ADDR + offset + (word_count * 4));
            const uint8_t *src_bytes = src + (word_count * 4);
            
            for (size_t i = 0; i < remaining; i++) {
                smarteeprom_bytes[i] = src_bytes[i];
            }
        }
    } else {
        // Fall back to byte writes for unaligned data
        volatile uint8_t *smarteeprom = (volatile uint8_t*)(SMARTEEPROM_BASE_ADDR + offset);
        
        for (size_t i = 0; i < size; i++) {
            smarteeprom[i] = src[i];
        }
    }
    
    __DSB();
    __ISB();
    
    // Use SEEFLUSH command for BUFFERED mode
    NVMCTRL_REGS->NVMCTRL_CTRLB = NVMCTRL_CTRLB_CMD_SEEFLUSH | NVMCTRL_CTRLB_CMDEX_KEY;
    
    volatile uint32_t timeout = 1000000;
    while (NVMCTRL_SmartEEPROM_IsBusy() && timeout > 0) {
        timeout--;
    }
    
    return timeout > 0;
}

/* Read data from SmartEEPROM using optimized 32-bit word reads */
static bool smarteeprom_read_data(uint32_t offset, void *buffer, size_t size) {
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

/* Save callback - save to both RAM and SmartEEPROM */
static int save_callback(const void *data, size_t size) {
    size_t total_size = sizeof(save_header_t) + size;
    
    if (total_size > SMARTEEPROM_SIZE) {
        printf("\n[ERROR: save data requires %zu bytes, SmartEEPROM size is %d]\n", total_size, SMARTEEPROM_SIZE);
        return 0;
    }
    
    // Save directly to SmartEEPROM with header (no RAM buffer needed)
    save_header_t header = {
        .magic = SAVE_MAGIC_HEADER,
        .data_size = size,
        .checksum = calculate_checksum(data, size)
    };
    
    if (!smarteeprom_write_data(0, &header, sizeof(header))) {
        printf("\n[ERROR: failed to write save header to flash]\n");
        return 0;
    }
    
    if (!smarteeprom_write_data(sizeof(header), data, size)) {
        printf("\n[ERROR: failed to write save data to flash]\n");
        return 0;
    }
    
    return 1;
}

/* Restore callback - restore from SmartEEPROM only */
static size_t restore_callback(void *buffer, size_t max_size) {
    save_header_t header;
    
    // Read from SmartEEPROM 
    if (smarteeprom_read_data(0, &header, sizeof(header))) {
        if (header.magic == SAVE_MAGIC_HEADER && header.data_size > 0 && header.data_size <= max_size) {
            if (smarteeprom_read_data(sizeof(header), buffer, header.data_size)) {
                uint32_t calculated_checksum = calculate_checksum(buffer, header.data_size);
                if (calculated_checksum == header.checksum) {
                    return header.data_size;
                }
            }
        }
    }
    
    return 0;
}

/* Check for existing save at startup */
static void check_existing_save(void) {
    save_header_t header;
    if (smarteeprom_read_data(0, &header, sizeof(header))) {
        if (header.magic == SAVE_MAGIC_HEADER && header.data_size > 0) {
            printf("\n[Existing save detected - type 'restore' to load]\n");
        }
    }
}

/* Interrupt handlers */
static void EIC_User_Handler(uintptr_t context) {
    zvibeReset = true;
}

static void rtcEventHandler(RTC_TIMER32_INT_MASK intCause, uintptr_t context) {
    if (intCause & RTC_MODE0_INTENSET_CMP0_Msk) {            
        isRTCExpired = true;
        if (activityLedOn) {
            LED0_Set();
            activityLedOn = false;
        }
    }
}

/* zvibe output function */
static void zvibe_output_func(const char *text, size_t length) {
    if (length == 0) return;
    
    for (size_t i = 0; i < length; i++) {
        char c = text[i];
        
        if (c == '\n' && (i == 0 || text[i-1] != '\r')) {
            while ((SERCOM5_REGS->USART_INT.SERCOM_INTFLAG & SERCOM_USART_INT_INTFLAG_DRE_Msk) == 0U) {}
            SERCOM5_REGS->USART_INT.SERCOM_DATA = (uint16_t)'\r';
        }
        
        while ((SERCOM5_REGS->USART_INT.SERCOM_INTFLAG & SERCOM_USART_INT_INTFLAG_DRE_Msk) == 0U) {}
        SERCOM5_REGS->USART_INT.SERCOM_DATA = (uint16_t)c;
    }
}

/* Wait for the user to press RETURN before starting the game.
 *
 * Drains stale RX bytes, prints the prompt, then polls the hardware FIFO
 * directly until a CR/LF arrives.  Also checks stringReady so an early
 * line (e.g. ~reset~\r already buffered before this is called) is honoured
 * without re-printing the prompt. */
static void wait_for_return(void) {
    /* Drain hardware FIFO.  Do NOT clear stringReady: the main loop's polling
     * path may have already processed a complete line before we were called. */
    while (SERCOM5_USART_ReceiverIsReady()) (void)SERCOM5_USART_ReadByte();
    rxIndex = 0;

    if (!stringReady) {
        zvibe_output_func("\r\nPress RETURN to start...\r\n",
                          sizeof("\r\nPress RETURN to start...\r\n") - 1);

        /* Spin until a CR/LF is received.  We check both the interrupt-driven
         * stringReady flag AND the hardware FIFO directly — the latter covers
         * driver configurations where the RXC callback does not fire (polling
         * mode), which is the case on this SAM E51 build. */
        while (!stringReady) {
            if (SERCOM5_USART_ReceiverIsReady()) {
                char c = (char)SERCOM5_USART_ReadByte();
                if (c == '\r' || c == '\n') {
                    break;  /* CR/LF seen — done waiting */
                }
                /* Buffer other chars so they reach the Z-machine if needed */
                if (rxIndex < (RX_BUFFER_SIZE - 1)) {
                    rxBuffer[rxIndex++] = c;
                }
            }
        }
    }

    stringReady = false;
    rxIndex     = 0;
}

/* Initialize zvibe Z-machine */
static bool init_zvibe() {
    if (g_zvibe_ctx) {
        zvibe_destroy(g_zvibe_ctx);
        g_zvibe_ctx = NULL;
    }
    
    g_zvibe_ctx = zvibe_create(zvibe_output_func);
    if (!g_zvibe_ctx) {
        return false;
    }

    if (zvibe_load_story_from_memory(g_zvibe_ctx, story_data, story_data_size) != ZVIBE_OK) {
        zvibe_destroy(g_zvibe_ctx);
        g_zvibe_ctx = NULL;
        return false;
    }
    
    return true;
}

int main(void) {
    /* Initialize all modules */
    SYS_Initialize(NULL);
    EIC_CallbackRegister(EIC_PIN_15, EIC_User_Handler, 0);
    RTC_Timer32CallbackRegister(rtcEventHandler, 0);
    
    LED0_Set(); // LED off at startup
    RTC_Timer32Compare0Set(PERIOD_50MS);
    RTC_Timer32Start();
    
    /* Initialize SmartEEPROM first */
    bool smarteeprom_available = smarteeprom_init();
    
    /* Initialize zvibe Z-machine */
    zvibe_initialized = init_zvibe();

    /* Check for existing saves */
    if (smarteeprom_available) {
        check_existing_save();
    }

    /* Wait for RETURN before first game launch (also drains any ~reset~\r
     * sent by test scripts on cold boot, preventing spurious first input) */
    if (zvibe_initialized) {
        wait_for_return();
    }

    while (true) {
        if (isRTCExpired == true) {
            isRTCExpired = false;
        }
        
        /* Handle reset button */
        if (zvibeReset == true) {
            zvibeReset = false;
            zvibe_initialized = init_zvibe();
            if (zvibe_initialized) {
                wait_for_return();
            }
        }
        
        /* Handle zvibe Z-machine execution */
        if (zvibe_initialized && g_zvibe_ctx) {
            zvibeResult result = zvibe_run(g_zvibe_ctx);
            
            if (result == ZVIBE_WAIT_FOR_INPUT) {
                /* Waiting for input */
            } else if (result == ZVIBE_SAVE_REQUESTED) {
                size_t save_size = zvibe_get_save_size(g_zvibe_ctx);
                if (save_size <= MAX_SAVE_SIZE) {
                    /* Use local buffer only during save operation */
                    char save_buffer[MAX_SAVE_SIZE];
                    size_t actual_size = zvibe_get_save_data(g_zvibe_ctx, save_buffer, MAX_SAVE_SIZE);
                    if (actual_size > 0) {
                        int success = save_callback(save_buffer, actual_size);
                        zvibe_save_completed(g_zvibe_ctx, success);
                    } else {
                        zvibe_save_completed(g_zvibe_ctx, 0);
                    }
                } else {
                    zvibe_save_completed(g_zvibe_ctx, 0);
                }
            } else if (result == ZVIBE_RESTORE_REQUESTED) {
                /* Use local buffer only during restore operation */
                char restore_buffer[MAX_SAVE_SIZE];
                size_t restored_size = restore_callback(restore_buffer, MAX_SAVE_SIZE);
                if (restored_size > 0) {
                    zvibeResult restore_result = zvibe_restore_data(g_zvibe_ctx, restore_buffer, restored_size);
                    zvibe_restore_completed(g_zvibe_ctx, restore_result == ZVIBE_OK);
                } else {
                    zvibe_restore_completed(g_zvibe_ctx, 0);
                }
            } else if (result == ZVIBE_RESTART_REQUESTED) {
                zvibe_restart_completed(g_zvibe_ctx);
            } else if (result == ZVIBE_GAME_FINISHED) {
                zvibe_output_func("\r\n[Game finished]\r\n",
                                  sizeof("\r\n[Game finished]\r\n") - 1);
                zvibe_initialized = init_zvibe();
                if (zvibe_initialized) {
                    wait_for_return();
                }
            } else if (result == ZVIBE_ERROR) {
                zvibe_output_func("\r\n[Game error - restarting]\r\n",
                                  sizeof("\r\n[Game error - restarting]\r\n") - 1);
                zvibe_initialized = init_zvibe();
                if (zvibe_initialized) {
                    wait_for_return();
                }
            }
        }
        
        /* Handle UART input */
        if (stringReady == true && zvibe_initialized && g_zvibe_ctx) {
            /* ~reset~ control command: reinitialize game without "Press RETURN" prompt.
             * Works whether sent by a test script or typed interactively. */
            if (strcmp(rxBuffer, "~reset~") == 0) {
                stringReady = false;
                rxIndex = 0;
                zvibe_output_func("\r\n[Game reset]\r\n\r\n",
                                  sizeof("\r\n[Game reset]\r\n\r\n") - 1);
                zvibe_initialized = init_zvibe();
                /* No wait_for_return: game restarts immediately for test scripts */
            } else {
                while ((SERCOM5_REGS->USART_INT.SERCOM_INTFLAG & SERCOM_USART_INT_INTFLAG_DRE_Msk) == 0U) {}
                SERCOM5_REGS->USART_INT.SERCOM_DATA = (uint16_t)'\r';
                while ((SERCOM5_REGS->USART_INT.SERCOM_INTFLAG & SERCOM_USART_INT_INTFLAG_DRE_Msk) == 0U) {}
                SERCOM5_REGS->USART_INT.SERCOM_DATA = (uint16_t)'\n';

                zvibe_input(g_zvibe_ctx, rxBuffer);

                stringReady = false;
                rxIndex = 0;
            }
        } else if (stringReady == true && !zvibe_initialized) {
            stringReady = false;
            rxIndex = 0;
            zvibe_initialized = init_zvibe();
        }
        
        /* Handle immediate UART echo */
        if (SERCOM5_USART_ReceiverIsReady()) {
            char receivedChar = (char)SERCOM5_USART_ReadByte();
            
            if (!activityLedOn) {
                LED0_Clear();
                activityLedOn = true;
                RTC_Timer32CounterSet(0);
            }
            
            if (receivedChar == '\r' || receivedChar == '\n') {
                rxBuffer[rxIndex] = '\0';
                stringReady = true;
                SERCOM5_USART_WriteByte('\r');
                SERCOM5_USART_WriteByte('\n');
            } else if (receivedChar == '\b' || receivedChar == 0x7F) {
                if (rxIndex > 0) {
                    rxIndex--;
                    SERCOM5_USART_WriteByte('\b');
                    SERCOM5_USART_WriteByte(' ');
                    SERCOM5_USART_WriteByte('\b');
                }
            } else if (rxIndex < (RX_BUFFER_SIZE - 1)) {
                rxBuffer[rxIndex++] = receivedChar;
                SERCOM5_USART_WriteByte(receivedChar);
            }
        }
    }

    return (EXIT_FAILURE);
}
