/* Copyright (c) 2025 Martin R. Raumann */
/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * save_delta.c - Delta compression for Z-machine saves
 */

#include "save_delta.h"
#include <string.h>

/* Maximum run length for a single delta entry */
#define MAX_RUN_LENGTH 255

size_t delta_encode(
    uint8_t *output,
    size_t output_size,
    const uint8_t *current_ram,
    const uint8_t *original,
    size_t dynamic_size,
    uint32_t pc,
    const uint16_t *stack,
    size_t stack_words,
    uint32_t base_ptr)
{
    /* Calculate minimum size needed for header + stack */
    size_t header_size = sizeof(delta_header_t);
    size_t stack_size = stack_words * sizeof(uint16_t);
    size_t min_size = header_size + stack_size;

    if (output_size < min_size) {
        return 0;
    }

    /* First pass: count deltas to get delta_count for header */
    uint16_t delta_count = 0;
    size_t delta_bytes = 0;

    for (size_t i = 0; i < dynamic_size; ) {
        if (current_ram[i] != original[i]) {
            size_t run_len = 0;
            while (i < dynamic_size &&
                   current_ram[i] != original[i] &&
                   run_len < MAX_RUN_LENGTH) {
                i++;
                run_len++;
            }
            delta_count++;
            delta_bytes += 3 + run_len;  /* offset(2) + len(1) + data */
        } else {
            i++;
        }
    }

    /* Check if everything fits */
    size_t total_size = min_size + delta_bytes;
    if (total_size > output_size) {
        return 0;
    }

    /* Write header */
    delta_header_t *header = (delta_header_t *)output;
    header->magic = DELTA_MAGIC;
    header->pc = pc;
    header->stack_words = (uint16_t)stack_words;
    header->delta_count = delta_count;
    header->base_ptr = base_ptr;

    uint8_t *ptr = output + header_size;

    /* Write stack data */
    memcpy(ptr, stack, stack_size);
    ptr += stack_size;

    /* Second pass: write delta entries */
    for (size_t i = 0; i < dynamic_size; ) {
        if (current_ram[i] != original[i]) {
            uint16_t offset = (uint16_t)i;
            size_t run_len = 0;

            while (i < dynamic_size &&
                   current_ram[i] != original[i] &&
                   run_len < MAX_RUN_LENGTH) {
                i++;
                run_len++;
            }

            /* Write delta entry: offset (big-endian), length, data */
            *ptr++ = (uint8_t)(offset >> 8);
            *ptr++ = (uint8_t)(offset & 0xFF);
            *ptr++ = (uint8_t)run_len;
            memcpy(ptr, &current_ram[offset], run_len);
            ptr += run_len;
        } else {
            i++;
        }
    }

    return total_size;
}

int delta_decode(
    const uint8_t *input,
    size_t input_size,
    uint8_t *ram_buffer,
    const uint8_t *original,
    size_t dynamic_size,
    uint32_t *pc,
    uint16_t *stack,
    size_t *stack_words,
    uint32_t *base_ptr)
{
    /* Validate minimum size */
    if (input_size < sizeof(delta_header_t)) {
        return -1;
    }

    /* Read and validate header */
    const delta_header_t *header = (const delta_header_t *)input;
    if (header->magic != DELTA_MAGIC) {
        return -2;
    }

    size_t stack_size = header->stack_words * sizeof(uint16_t);
    size_t min_size = sizeof(delta_header_t) + stack_size;
    if (input_size < min_size) {
        return -3;
    }

    /* Copy original game data to RAM (fresh start) */
    memcpy(ram_buffer, original, dynamic_size);

    /* Read stack data */
    const uint8_t *ptr = input + sizeof(delta_header_t);
    memcpy(stack, ptr, stack_size);
    ptr += stack_size;

    /* Apply delta entries */
    const uint8_t *end = input + input_size;
    for (uint16_t i = 0; i < header->delta_count; i++) {
        if (ptr + 3 > end) {
            return -4;  /* Truncated delta entry */
        }

        uint16_t offset = ((uint16_t)ptr[0] << 8) | ptr[1];
        uint8_t length = ptr[2];
        ptr += 3;

        if (ptr + length > end) {
            return -5;  /* Truncated delta data */
        }
        if (offset + length > dynamic_size) {
            return -6;  /* Delta out of bounds */
        }

        memcpy(&ram_buffer[offset], ptr, length);
        ptr += length;
    }

    /* Return execution state */
    *pc = header->pc;
    *stack_words = header->stack_words;
    *base_ptr = header->base_ptr;

    return 0;
}
