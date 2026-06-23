/* Copyright (c) 2025 Martin R. Raumann */
/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * save_delta.h - Delta compression for Z-machine saves
 *
 * Instead of saving the entire dynamic memory (~14KB), we save only
 * the bytes that differ from the original game data in flash.
 *
 * Format:
 *   [Header: 16 bytes]
 *     magic(4) + pc(4) + stack_words(2) + delta_count(2) + base_ptr(4)
 *   [Stack data: stack_words * 2 bytes]
 *   [Delta entries: variable]
 *     Each entry: offset(2) + length(1) + data(length)
 *
 * Typical saves: 500-2000 bytes instead of 14KB+
 */

#ifndef SAVE_DELTA_H
#define SAVE_DELTA_H

#include <stdint.h>
#include <stddef.h>

#define DELTA_MAGIC  0x444C5445  /* 'DELT' */

/* Delta save header - 16 bytes */
typedef struct {
    uint32_t magic;
    uint32_t pc;
    uint16_t stack_words;
    uint16_t delta_count;
    uint32_t base_ptr;
} delta_header_t;

/*
 * Encode a delta save.
 *
 * Compares current_ram against original_flash and encodes differences.
 *
 * Parameters:
 *   output       - Buffer to write encoded save
 *   output_size  - Size of output buffer
 *   current_ram  - Current dynamic memory contents
 *   original     - Original game data (from flash)
 *   dynamic_size - Size of dynamic memory (staticmem_addr)
 *   pc           - Program counter
 *   stack        - Stack data (array of 16-bit words)
 *   stack_words  - Number of stack words in use
 *   base_ptr     - Base pointer
 *
 * Returns: Bytes written on success, 0 if buffer too small
 */
size_t delta_encode(
    uint8_t *output,
    size_t output_size,
    const uint8_t *current_ram,
    const uint8_t *original,
    size_t dynamic_size,
    uint32_t pc,
    const uint16_t *stack,
    size_t stack_words,
    uint32_t base_ptr
);

/*
 * Decode a delta save.
 *
 * First copies original data to ram_buffer, then applies delta patches.
 *
 * Parameters:
 *   input        - Encoded save data
 *   input_size   - Size of input data
 *   ram_buffer   - RAM buffer to restore to (must be >= dynamic_size)
 *   original     - Original game data (from flash)
 *   dynamic_size - Size of dynamic memory
 *   pc           - Output: program counter
 *   stack        - Output: stack buffer (must be large enough)
 *   stack_words  - Output: number of stack words restored
 *   base_ptr     - Output: base pointer
 *
 * Returns: 0 on success, negative on error
 */
int delta_decode(
    const uint8_t *input,
    size_t input_size,
    uint8_t *ram_buffer,
    const uint8_t *original,
    size_t dynamic_size,
    uint32_t *pc,
    uint16_t *stack,
    size_t *stack_words,
    uint32_t *base_ptr
);

#endif /* SAVE_DELTA_H */
