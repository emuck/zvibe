/* Copyright (c) 2025 Martin R. Raumann */
/* SPDX-License-Identifier: BSD-3-Clause */
/*******************************************************************************
  zvibe Z-Machine Multi-Game with SmartEEPROM Save/Restore - SAM E51 Production
  SAM E51 Curiosity Nano Board - Multi-Game Menu System
*******************************************************************************/

#include <stdio.h>
#include <stddef.h>                     
#include <stdbool.h>                    
#include <stdlib.h>                     
#include <string.h>
#include <ctype.h>
#include "board_support.h"
#include "../../../include/zvibe_api.h"
#include "../../../include/zvibe.h"
#include "game_registry.h"
#include "save_delta.h"

/* RTC Time period match values for input clock of 1 KHz */
#define PERIOD_50MS                             51

static volatile bool isRTCExpired = false;
static volatile bool activityLedOn = false;
static volatile bool zvibeReset = false;
static volatile bool isUSARTTxComplete = true;

#define RX_BUFFER_SIZE 50
static char rxBuffer[RX_BUFFER_SIZE];
static volatile uint16_t rxIndex = 0;
static volatile bool stringReady = false;

/* zvibe context - global for reset functionality */
static zvibeContext *g_zvibe_ctx = NULL;
static bool zvibe_initialized = false;

/* Menu state */
static bool in_menu_mode = true;
static bool menu_shown = false;
static int current_game_index = -1;

/* SmartEEPROM Configuration */
#define SMARTEEPROM_BASE_ADDR       0x44000000UL
#define SMARTEEPROM_SIZE           16384   // 16KB available

/* Target fuse values for 16KB SmartEEPROM */
#define TARGET_SBLK                 3U  // SBLK=3 for 16KB virtual size
#define TARGET_PSZ                  7U  // PSZ=7 (512-byte pages) for fastest writes

/* Per-game save slots using delta compression.
 * Delta saves are typically 500-2000 bytes vs 14KB+ for full saves.
 * With 2KB slots, 16KB SmartEEPROM supports up to 8 games. */
#define SAVE_SLOT_SIZE             2048
#define MAX_SAVE_SLOTS             (SMARTEEPROM_SIZE / SAVE_SLOT_SIZE)
#define DELTA_BUFFER_SIZE          SAVE_SLOT_SIZE

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

static bool game_has_save_slot(int game_index) {
    return game_index >= 0 && game_index < MAX_SAVE_SLOTS && game_index < num_games;
}

static uint32_t slot_offset(int game_index) {
    return (uint32_t)game_index * SAVE_SLOT_SIZE;
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

/* Save using delta compression into per-game SmartEEPROM slot */
static int do_delta_save(void) {
    if (!game_has_save_slot(current_game_index) || !G) {
        printf("\n[Save not available for this game (EEPROM full)]\n");
        return 0;
    }

    const uint8_t *current_ram = G->memory_state.ram_buffer;
    size_t dynamic_size = G->memory_state.ram_size;
    uint32_t pc = (uint32_t)G->logical_pc;
    const uint16_t *stack = (const uint16_t *)G->stack_mem;
    size_t stack_words = (size_t)(G->stack_ptr - G->stack_mem);
    uint32_t base_ptr = (uint32_t)G->base_ptr;

    const game_entry_t *game = &games[current_game_index];

    uint8_t delta_buf[DELTA_BUFFER_SIZE];
    size_t save_size = delta_encode(
        delta_buf, DELTA_BUFFER_SIZE,
        current_ram, game->data, dynamic_size,
        pc, stack, stack_words, base_ptr
    );

    if (save_size == 0) {
        printf("\n[Save failed: delta too large for slot]\n");
        return 0;
    }

    uint32_t offset = slot_offset(current_game_index);
    if (!smarteeprom_write_data(offset, delta_buf, save_size)) {
        printf("\n[Save failed: SmartEEPROM write error]\n");
        return 0;
    }

    return 1;
}

/* Restore using delta compression from per-game SmartEEPROM slot */
static int do_delta_restore(void) {
    if (!game_has_save_slot(current_game_index) || !G) {
        printf("\n[Restore not available for this game]\n");
        return 0;
    }

    uint8_t delta_buf[DELTA_BUFFER_SIZE];
    uint32_t offset = slot_offset(current_game_index);

    if (!smarteeprom_read_data(offset, delta_buf, DELTA_BUFFER_SIZE)) {
        printf("\n[Restore failed: SmartEEPROM read error]\n");
        return 0;
    }

    const delta_header_t *hdr = (const delta_header_t *)delta_buf;
    if (hdr->magic != DELTA_MAGIC) {
        printf("\n[No saved game found]\n");
        return 0;
    }

    const game_entry_t *game = &games[current_game_index];

    uint32_t pc;
    size_t stack_words;
    uint32_t base_ptr;

    int result = delta_decode(
        delta_buf, DELTA_BUFFER_SIZE,
        G->memory_state.ram_buffer,
        game->data,
        G->memory_state.ram_size,
        &pc, (uint16_t *)G->stack_mem, &stack_words, &base_ptr
    );

    if (result != 0) {
        printf("\n[Restore failed: corrupt save data]\n");
        return 0;
    }

    G->logical_pc = (zDWord)pc;
    G->stack_ptr = G->stack_mem + stack_words;
    G->base_ptr = (zWord)base_ptr;

    return 1;
}

/* Check for existing save in current game's slot */
static void check_existing_save(void) {
    if (!game_has_save_slot(current_game_index)) {
        printf("\n[Save/restore not available for this game (EEPROM full)]\n");
        return;
    }

    delta_header_t hdr;
    uint32_t offset = slot_offset(current_game_index);
    if (smarteeprom_read_data(offset, &hdr, sizeof(hdr))) {
        if (hdr.magic == DELTA_MAGIC && hdr.stack_words > 0) {
            printf("\n[Saved game found - type 'restore' to load]\n");
        }
    }
}

/* Menu System Functions */
static void print_menu(void) {
    uint8_t menuBuffer[1000] = {0};
    int len = 0;
    
    len += sprintf((char*)menuBuffer + len, "\r\n\r\n");
    len += sprintf((char*)menuBuffer + len, "Z-Machine Game Library\r\n");
    len += sprintf((char*)menuBuffer + len, "======================\r\n");
    
    for (int i = 0; i < num_games; i++) {
        const char *save_tag = game_has_save_slot(i) ? "" : " [no save]";
        len += sprintf((char*)menuBuffer + len, "%d. %s%s\r\n", i + 1, games[i].name, save_tag);
    }
    
    len += sprintf((char*)menuBuffer + len, "\r\nEnter game number (1-%d): ", num_games);
    
    // Wait for previous transmission to complete
    while (!isUSARTTxComplete) {}
    isUSARTTxComplete = false;
    
    // Send via DMA
    DMAC_ChannelTransfer(DMAC_CHANNEL_0, menuBuffer, 
            (const void *)&(SERCOM5_REGS->USART_INT.SERCOM_DATA), 
            len);
}

static bool handle_menu_input(const char *input) {
    // Skip whitespace
    while (*input && isspace(*input)) input++;
    
    // Check if it's a single digit
    if (*input && isdigit(*input) && *(input + 1) == '\0') {
        int game_num = *input - '0';
        if (game_num >= 1 && game_num <= num_games) {
            current_game_index = game_num - 1;
            return true;
        }
    }
    
    // Any non-valid input shows menu again (don't print error message)
    return false;
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

static void usartDmaChannelHandler(DMAC_TRANSFER_EVENT event, uintptr_t contextHandle) {
    if (event == DMAC_TRANSFER_EVENT_COMPLETE) {
        isUSARTTxComplete = true;
    }
}

static void usartReadEventHandler(uintptr_t context) {
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

/* Initialize zvibe Z-machine with selected game */
static bool init_zvibe_game(int game_index) {
    if (game_index < 0 || game_index >= num_games) {
        return false;
    }
    
    if (g_zvibe_ctx) {
        zvibe_destroy(g_zvibe_ctx);
        g_zvibe_ctx = NULL;
    }
    
    g_zvibe_ctx = zvibe_create(zvibe_output_func);
    if (!g_zvibe_ctx) {
        return false;
    }

    const game_entry_t *game = &games[game_index];
    if (zvibe_load_story_from_memory(g_zvibe_ctx, game->data, game->size) != ZVIBE_OK) {
        zvibe_destroy(g_zvibe_ctx);
        g_zvibe_ctx = NULL;
        return false;
    }
    
    printf("\r\n\r\n*** Loading %s ***\r\n\r\n", game->name);
    return true;
}

int main(void) {
    uint8_t uartLocalTxBuffer[100] = {0};
    
    /* Initialize all modules */
    SYS_Initialize(NULL);
    DMAC_ChannelCallbackRegister(DMAC_CHANNEL_0, usartDmaChannelHandler, 0);
    EIC_CallbackRegister(EIC_PIN_15, EIC_User_Handler, 0);
    RTC_Timer32CallbackRegister(rtcEventHandler, 0);
    SERCOM5_USART_ReadCallbackRegister(usartReadEventHandler, 0);
    
    LED0_Set(); // LED off at startup
    RTC_Timer32Compare0Set(PERIOD_50MS);
    RTC_Timer32Start();
    
    /* Initialize SmartEEPROM */
    bool smarteeprom_available = smarteeprom_init();
    
    /* Print startup message and wait for key press */
    uint8_t startupBuffer[100] = {0};
    sprintf((char*)startupBuffer, "\r\n\r\nZ-Machine Multi-Game System Ready\r\nPress any key to show game menu...\r\n");
    while (!isUSARTTxComplete) {}
    isUSARTTxComplete = false;
    DMAC_ChannelTransfer(DMAC_CHANNEL_0, startupBuffer, 
            (const void *)&(SERCOM5_REGS->USART_INT.SERCOM_DATA), 
            strlen((const char*)startupBuffer));

    while (true) {
        if (isRTCExpired == true) {
            isRTCExpired = false;
        }
        
        /* Handle reset button - return to menu */
        if (zvibeReset == true) {
            zvibeReset = false;
            if (isUSARTTxComplete == true) {
                isUSARTTxComplete = false;
                sprintf((char*)uartLocalTxBuffer, "\r\n\r\n*** BACK TO MENU ***\r\n");
                DMAC_ChannelTransfer(DMAC_CHANNEL_0, uartLocalTxBuffer, 
                        (const void *)&(SERCOM5_REGS->USART_INT.SERCOM_DATA), 
                        strlen((const char*)uartLocalTxBuffer));
                in_menu_mode = true;
                menu_shown = false;
                zvibe_initialized = false;
                current_game_index = -1;
                print_menu();
            }
        }
        
        /* Handle input */
        if (stringReady == true && isUSARTTxComplete == true) {
            if (in_menu_mode) {
                /* Menu mode - always show menu, then handle selection if valid */
                if (handle_menu_input(rxBuffer)) {
                    // Valid game selection
                    zvibe_initialized = init_zvibe_game(current_game_index);
                    if (zvibe_initialized) {
                        in_menu_mode = false;
                        menu_shown = false; // Reset for next time
                        if (smarteeprom_available) {
                            check_existing_save();
                        }
                    } else {
                        uint8_t errorBuffer[100] = {0};
                        sprintf((char*)errorBuffer, "Failed to load game. Please try again.\r\n");
                        while (!isUSARTTxComplete) {}
                        isUSARTTxComplete = false;
                        DMAC_ChannelTransfer(DMAC_CHANNEL_0, errorBuffer, 
                                (const void *)&(SERCOM5_REGS->USART_INT.SERCOM_DATA), 
                                strlen((const char*)errorBuffer));
                        print_menu();
                    }
                } else {
                    // Any other input - show menu
                    print_menu();
                }
                stringReady = false;
                rxIndex = 0;
            } else {
                /* Game mode - pass input to zvibe */
                if (zvibe_initialized && g_zvibe_ctx) {
                    while ((SERCOM5_REGS->USART_INT.SERCOM_INTFLAG & SERCOM_USART_INT_INTFLAG_DRE_Msk) == 0U) {}
                    SERCOM5_REGS->USART_INT.SERCOM_DATA = (uint16_t)'\r';
                    while ((SERCOM5_REGS->USART_INT.SERCOM_INTFLAG & SERCOM_USART_INT_INTFLAG_DRE_Msk) == 0U) {}
                    SERCOM5_REGS->USART_INT.SERCOM_DATA = (uint16_t)'\n';
                    
                    zvibe_input(g_zvibe_ctx, rxBuffer);
                    stringReady = false;
                    rxIndex = 0;
                }
            }
        }
        
        /* Handle zvibe Z-machine execution */
        if (!in_menu_mode && zvibe_initialized && g_zvibe_ctx) {
            zvibeResult result = zvibe_run(g_zvibe_ctx);
            
            if (result == ZVIBE_WAIT_FOR_INPUT) {
                /* Waiting for input */
            } else if (result == ZVIBE_SAVE_REQUESTED) {
                int success = smarteeprom_available ? do_delta_save() : 0;
                zvibe_save_completed(g_zvibe_ctx, success);
            } else if (result == ZVIBE_RESTORE_REQUESTED) {
                int success = smarteeprom_available ? do_delta_restore() : 0;
                zvibe_restore_completed(g_zvibe_ctx, success);
            } else if (result == ZVIBE_RESTART_REQUESTED) {
                zvibe_restart_completed(g_zvibe_ctx);
            } else if (result == ZVIBE_GAME_FINISHED) {
                sprintf((char*)uartLocalTxBuffer, "\r\n\r\n[Game finished - press button for menu]\r\n");
                while (!isUSARTTxComplete) {}
                isUSARTTxComplete = false;
                DMAC_ChannelTransfer(DMAC_CHANNEL_0, uartLocalTxBuffer, 
                        (const void *)&(SERCOM5_REGS->USART_INT.SERCOM_DATA), 
                        strlen((const char*)uartLocalTxBuffer));
                zvibe_initialized = false;
                in_menu_mode = true;
                menu_shown = false;
                current_game_index = -1;
            } else if (result == ZVIBE_ERROR) {
                uint8_t errorMsgBuffer[100] = {0};
                sprintf((char*)errorMsgBuffer, "\r\n[Game error - returning to menu]\r\n");
                while (!isUSARTTxComplete) {}
                isUSARTTxComplete = false;
                DMAC_ChannelTransfer(DMAC_CHANNEL_0, errorMsgBuffer, 
                        (const void *)&(SERCOM5_REGS->USART_INT.SERCOM_DATA), 
                        strlen((const char*)errorMsgBuffer));
                zvibe_initialized = false;
                in_menu_mode = true;
                menu_shown = false;
                current_game_index = -1;
                print_menu();
            }
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
