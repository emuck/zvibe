/*
 * Copyright (c) 2025 Martin R. Raumann
 * SPDX-License-Identifier: BSD-3-Clause
 */
/**
 * @file zvibe_memory.c
 * @brief Split memory architecture implementation
 * @ingroup MemoryAbstraction
 *
 * Implements split memory model with separate RAM and flash regions.
 * Reduces RAM requirements by 85-95% compared to full story buffering.
 *
 * **Memory Regions:**
 * - Dynamic (0 to staticmem_addr): Writable, in RAM
 * - Static (staticmem_addr to story_size): Read-only, in flash/ROM
 *
 * **Embedded Constraints:**
 * - No malloc in zmem_init_embedded() (uses caller-provided buffers)
 * - Optional malloc in zmem_init() for desktop builds
 * - Configurable bounds checking via state->bounds_checking
 *
 * **Implementation Notes:**
 * - Big-endian word access (Z-machine spec)
 * - Write protection enforced on static region
 * - Debug output via zmem_debug() when state->debug_mode enabled
 */

#include "zvibe_memory.h"
#include <stdlib.h>
#include <string.h>
#if ZVIBE_ENABLE_DIAGNOSTICS
#include <stdio.h>
#include <stdarg.h>
#endif

/* -------------------- INTERNAL HELPERS -------------------- */

/**
 * Internal debug print function
 */
#if ZVIBE_ENABLE_DIAGNOSTICS
static void zmem_debug(const zmem_state_t *state, const char *fmt, ...) {
    if (state && state->debug_mode) {
        va_list args;
        va_start(args, fmt);
        printf("[ZMEM DEBUG] ");
        vprintf(fmt, args);
        printf("\n");
        va_end(args);
    }
}
#else
static void zmem_debug(const zmem_state_t *state, const char *fmt, ...) {
    (void)state;
    (void)fmt;
}
#endif

/**
 * Internal error print function
 */
#if ZVIBE_ENABLE_DIAGNOSTICS
static void zmem_error(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    printf("[ZMEM ERROR] ");
    vprintf(fmt, args);
    printf("\n");
    va_end(args);
}
#else
static void zmem_error(const char *fmt, ...) {
    (void)fmt;
}
#endif

/* -------------------- CORE FUNCTIONS -------------------- */

int zmem_init(zmem_state_t *state, const zmem_config_t *config, 
              const void *story_data, size_t story_size) {
    if (!state || !config || !story_data) {
        zmem_error("Invalid parameters");
        return ZMEM_ERROR_NULL_PTR;
    }
    
    if (config->staticmem_addr == 0 || config->staticmem_addr >= story_size) {
        zmem_error("Invalid staticmem_addr: %u (story_size: %zu)", 
                   config->staticmem_addr, story_size);
        return ZMEM_ERROR_BAD_SIZE;
    }
    
    /* Clear state */
    memset(state, 0, sizeof(zmem_state_t));
    
    /* Copy configuration */
    state->config = *config;
    state->bounds_checking = 1;  /* Enable by default */
    state->debug_mode = 0;       /* Disable by default */
    
    /* Allocate RAM buffer for dynamic memory */
    state->ram_size = config->staticmem_addr;
    state->ram_buffer = malloc(state->ram_size);
    if (!state->ram_buffer) {
        zmem_error("Failed to allocate %u bytes for RAM", state->ram_size);
        return ZMEM_ERROR_BAD_SIZE;
    }
    state->owns_ram_buffer = 1;
    
    /* Set up flash memory (static part) */
    state->flash_data = (const zmem_byte_t *)story_data;
    state->flash_size = story_size;
    
    /* Initialize RAM with initial story data (0x0 to staticmem_addr-1) */
    if (story_data) {
        memcpy(state->ram_buffer, story_data, state->ram_size);
        zmem_debug(state, "Copied %u bytes from story to RAM", state->ram_size);
    }
    
    zmem_debug(state, "Initialized: RAM=%u bytes, Flash=%u bytes",
               state->ram_size, state->flash_size);
    
    return ZMEM_SUCCESS;
}

void zmem_cleanup(zmem_state_t *state) {
    if (!state) return;
    
    if (state->owns_ram_buffer && state->ram_buffer) {
        free(state->ram_buffer);
        state->ram_buffer = NULL;
    }
    
    memset(state, 0, sizeof(zmem_state_t));
}

/* -------------------- VALIDATION FUNCTIONS -------------------- */

int zmem_validate_addr(const zmem_state_t *state, zmem_addr_t addr, int for_write) {
    if (!state) return ZMEM_ERROR_NULL_PTR;
    
    /* Check bounds */
    if (addr >= state->config.story_size) {
        if (state->bounds_checking) {
            zmem_error("Address 0x%04X out of bounds (max: 0x%04X)", 
                       addr, state->config.story_size - 1);
        }
        return ZMEM_ERROR_OUT_OF_BOUNDS;
    }
    
    /* Check write protection for static memory */
    if (for_write && addr >= state->config.staticmem_addr) {
        if (state->bounds_checking) {
            zmem_error("Attempt to write to static memory at 0x%04X", addr);
        }
        return ZMEM_ERROR_WRITE_PROTECTED;
    }
    
    return ZMEM_SUCCESS;
}

int zmem_is_ram_addr(const zmem_state_t *state, zmem_addr_t addr) {
    if (!state) return 0;
    return (addr < state->config.staticmem_addr) ? 1 : 0;
}

/* -------------------- MEMORY ACCESS FUNCTIONS -------------------- */

int zmem_read_byte(const zmem_state_t *state, zmem_addr_t addr, zmem_byte_t *value) {
    if (!state || !value) return ZMEM_ERROR_NULL_PTR;
    
    int result = zmem_validate_addr(state, addr, 0);
    if (result != ZMEM_SUCCESS) return result;
    
    if (zmem_is_ram_addr(state, addr)) {
        /* Read from RAM */
        *value = state->ram_buffer[addr];
        zmem_debug(state, "Read byte 0x%02X from RAM[0x%04X]", *value, addr);
    } else {
        /* Read from flash - addr is direct index into flash_data */
        if (addr >= state->flash_size) {
            return ZMEM_ERROR_OUT_OF_BOUNDS;
        }
        *value = state->flash_data[addr];
        zmem_debug(state, "Read byte 0x%02X from Flash[0x%04X]", *value, addr);
    }
    
    return ZMEM_SUCCESS;
}

int zmem_read_word(const zmem_state_t *state, zmem_addr_t addr, zmem_word_t *value) {
    if (!state || !value) return ZMEM_ERROR_NULL_PTR;
    
    /* Check alignment (optional for Z-machine, but good practice) */
    if (addr & 1) {
        zmem_debug(state, "Unaligned word read at 0x%04X", addr);
    }
    
    /* Validate both bytes */
    int result = zmem_validate_addr(state, addr, 0);
    if (result != ZMEM_SUCCESS) return result;
    
    result = zmem_validate_addr(state, addr + 1, 0);
    if (result != ZMEM_SUCCESS) return result;
    
    zmem_byte_t high_byte, low_byte;
    
    /* Read high byte */
    result = zmem_read_byte(state, addr, &high_byte);
    if (result != ZMEM_SUCCESS) return result;
    
    /* Read low byte */
    result = zmem_read_byte(state, addr + 1, &low_byte);
    if (result != ZMEM_SUCCESS) return result;
    
    /* Combine in big-endian format */
    *value = (((zmem_word_t)high_byte) << 8) | low_byte;
    
    zmem_debug(state, "Read word 0x%04X from 0x%04X", *value, addr);
    
    return ZMEM_SUCCESS;
}

int zmem_write_byte(zmem_state_t *state, zmem_addr_t addr, zmem_byte_t value) {
    if (!state) return ZMEM_ERROR_NULL_PTR;
    
    int result = zmem_validate_addr(state, addr, 1);
    if (result != ZMEM_SUCCESS) return result;
    
    /* Can only write to RAM */
    if (!zmem_is_ram_addr(state, addr)) {
        return ZMEM_ERROR_WRITE_PROTECTED;
    }
    
    state->ram_buffer[addr] = value;
    zmem_debug(state, "Wrote byte 0x%02X to RAM[0x%04X]", value, addr);
    
    return ZMEM_SUCCESS;
}

int zmem_write_word(zmem_state_t *state, zmem_addr_t addr, zmem_word_t value) {
    if (!state) return ZMEM_ERROR_NULL_PTR;
    
    /* Check alignment */
    if (addr & 1) {
        zmem_debug(state, "Unaligned word write at 0x%04X", addr);
    }
    
    /* Validate both bytes for writing */
    int result = zmem_validate_addr(state, addr, 1);
    if (result != ZMEM_SUCCESS) return result;
    
    result = zmem_validate_addr(state, addr + 1, 1);
    if (result != ZMEM_SUCCESS) return result;
    
    /* Write high byte */
    result = zmem_write_byte(state, addr, (value >> 8) & 0xFF);
    if (result != ZMEM_SUCCESS) return result;
    
    /* Write low byte */
    result = zmem_write_byte(state, addr + 1, value & 0xFF);
    if (result != ZMEM_SUCCESS) return result;
    
    zmem_debug(state, "Wrote word 0x%04X to 0x%04X", value, addr);
    
    return ZMEM_SUCCESS;
}

zmem_byte_t *zmem_get_ptr(const zmem_state_t *state, zmem_addr_t addr, int for_write) {
    if (!state) return NULL;
    
    int result = zmem_validate_addr(state, addr, for_write);
    if (result != ZMEM_SUCCESS) return NULL;
    
    if (zmem_is_ram_addr(state, addr)) {
        /* Return pointer to RAM */
        return &state->ram_buffer[addr];
    } else if (!for_write) {
        /* Return pointer to flash (read-only) */
        return (zmem_byte_t *)&state->flash_data[addr];
    } else {
        /* Cannot get write pointer to flash */
        zmem_error("Cannot get write pointer to static memory at 0x%04X", addr);
        return NULL;
    }
}

int zmem_copy_block(zmem_state_t *state, zmem_addr_t dst_addr, 
                    zmem_addr_t src_addr, size_t size) {
    if (!state || size == 0) return ZMEM_ERROR_NULL_PTR;
    
    /* Validate source and destination ranges */
    for (size_t i = 0; i < size; i++) {
        int result = zmem_validate_addr(state, src_addr + i, 0);
        if (result != ZMEM_SUCCESS) return result;
        
        result = zmem_validate_addr(state, dst_addr + i, 1);
        if (result != ZMEM_SUCCESS) return result;
    }
    
    /* Perform byte-by-byte copy to handle cross-boundary copies */
    for (size_t i = 0; i < size; i++) {
        zmem_byte_t value;
        int result = zmem_read_byte(state, src_addr + i, &value);
        if (result != ZMEM_SUCCESS) return result;
        
        result = zmem_write_byte(state, dst_addr + i, value);
        if (result != ZMEM_SUCCESS) return result;
    }
    
    zmem_debug(state, "Copied %zu bytes from 0x%04X to 0x%04X", 
               size, src_addr, dst_addr);
    
    return ZMEM_SUCCESS;
}

/* -------------------- UTILITY FUNCTIONS -------------------- */

void zmem_get_stats(const zmem_state_t *state, size_t *ram_used) {
    if (!state) return;
    if (ram_used)
        *ram_used = state->ram_size;
}

/* -------------------- EMBEDDED SYSTEM SUPPORT -------------------- */

/* Initialize with caller-provided static buffers (no malloc) */
int zmem_init_embedded(zmem_state_t *state, const zmem_config_t *config,
                       const void *story_data, size_t story_size,
                       uint8_t *ram_buffer) {
    if (!state || !config || !story_data || !ram_buffer) {
        zmem_error("Invalid parameters for embedded init");
        return ZMEM_ERROR_NULL_PTR;
    }
    
    if (config->staticmem_addr == 0 || config->staticmem_addr >= story_size) {
        zmem_error("Invalid staticmem_addr: %u (story_size: %zu)", 
                   config->staticmem_addr, story_size);
        return ZMEM_ERROR_BAD_SIZE;
    }
    
    /* Clear state */
    memset(state, 0, sizeof(zmem_state_t));
    
    /* Copy configuration */
    state->config = *config;
    state->bounds_checking = 1;  /* Enable by default */
    state->debug_mode = 0;       /* Disable by default for embedded */
    
    /* Use provided RAM buffer for dynamic memory */
    state->ram_size = config->staticmem_addr;
    state->ram_buffer = ram_buffer;
    state->owns_ram_buffer = 0;
    
    /* Set up flash memory (static part) */
    state->flash_data = (const zmem_byte_t *)story_data;
    state->flash_size = story_size;
    
    /* Initialize RAM with initial story data (0x0 to staticmem_addr-1) */
    if (story_data) {
        memcpy(state->ram_buffer, story_data, state->ram_size);
        zmem_debug(state, "Copied %u bytes from story to static RAM buffer", state->ram_size);
    }
    
    zmem_debug(state, "Embedded init: RAM=%u bytes, Flash=%u bytes",
               state->ram_size, state->flash_size);
    
    return ZMEM_SUCCESS;
}
