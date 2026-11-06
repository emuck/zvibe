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
    delta_header_t header;
    header.magic = DELTA_MAGIC;
    header.pc = pc;
    header.stack_words = (uint16_t)stack_words;
    header.delta_count = delta_count;
    header.base_ptr = base_ptr;
    memcpy(output, &header, sizeof header);   /* output may be unaligned */

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
    size_t stack_capacity,
    size_t *stack_words,
    uint32_t *base_ptr)
{
    /* Validate everything before touching any output, so a bad save leaves the
     * running machine unchanged. */

    /* Validate minimum size */
    if (input_size < sizeof(delta_header_t)) {
        return -1;
    }

    /* Read and validate header (copied out: the input may be unaligned) */
    delta_header_t hdr;
    memcpy(&hdr, input, sizeof hdr);
    const delta_header_t *header = &hdr;
    if (header->magic != DELTA_MAGIC) {
        return -2;
    }

    if (header->stack_words > stack_capacity) {
        return -7;  /* Saved stack does not fit this build's stack */
    }

    size_t stack_size = header->stack_words * sizeof(uint16_t);
    size_t min_size = sizeof(delta_header_t) + stack_size;
    if (input_size < min_size) {
        return -3;
    }

    /* Validate pass: every delta entry must be complete and in bounds */
    const uint8_t *end = input + input_size;
    const uint8_t *ptr = input + sizeof(delta_header_t) + stack_size;
    for (uint16_t i = 0; i < header->delta_count; i++) {
        /* Compare remaining length: never form a pointer past `end` */
        if ((size_t)(end - ptr) < 3) {
            return -4;  /* Truncated delta entry */
        }
        uint16_t offset = ((uint16_t)ptr[0] << 8) | ptr[1];
        uint8_t length = ptr[2];
        ptr += 3;
        if ((size_t)(end - ptr) < length) {
            return -5;  /* Truncated delta data */
        }
        if ((size_t)offset + length > dynamic_size) {
            return -6;  /* Delta out of bounds */
        }
        ptr += length;
    }

    /* Apply pass: nothing below can fail */

    /* Copy original game data to RAM (fresh start) */
    memcpy(ram_buffer, original, dynamic_size);

    /* Read stack data */
    ptr = input + sizeof(delta_header_t);
    memcpy(stack, ptr, stack_size);
    ptr += stack_size;

    /* Apply delta entries */
    for (uint16_t i = 0; i < header->delta_count; i++) {
        uint16_t offset = ((uint16_t)ptr[0] << 8) | ptr[1];
        uint8_t length = ptr[2];
        ptr += 3;

        memcpy(&ram_buffer[offset], ptr, length);
        ptr += length;
    }

    /* Return execution state */
    *pc = header->pc;
    *stack_words = header->stack_words;
    *base_ptr = header->base_ptr;

    return 0;
}

/* ---- packing a whole save blob against a base image ---- */

#define PACK_HEADER    12     /* magic + total length + packed length */
#define PACK_RUN_HEAD  6      /* offset(4) + count(2) */
#define PACK_MAX_RUN   0xFFFFu
#define PACK_MERGE_GAP 8      /* equal bytes this short between two runs are cheaper to include */

static void put32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

static uint32_t get32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int differs(const uint8_t *data, const uint8_t *base, size_t base_len, size_t i) {
    return i >= base_len || data[i] != base[i];
}

size_t delta_pack(uint8_t *out, size_t capacity, const uint8_t *data, size_t len,
                  const uint8_t *base, size_t base_len)
{
    size_t used = PACK_HEADER;
    size_t i = 0;

    if (capacity < PACK_HEADER || len > 0xFFFFFFFFu) {
        return 0;
    }
    put32(out, DELTA_PACK_MAGIC);
    put32(out + 4, (uint32_t)len);
    put32(out + 8, 0);

    while (i < len) {
        size_t end, j;
        if (!differs(data, base, base_len, i)) {
            i++;
            continue;
        }
        end = i + 1;                    /* one past the last differing byte of this run */
        for (j = end; j < len && j - end < PACK_MERGE_GAP; j++) {
            if (differs(data, base, base_len, j)) {
                end = j + 1;
            }
        }
        if (end - i > PACK_MAX_RUN) {
            end = i + PACK_MAX_RUN;
        }
        if (capacity - used < PACK_RUN_HEAD || capacity - used - PACK_RUN_HEAD < end - i) {
            return 0;
        }
        put32(out + used, (uint32_t)i);
        out[used + 4] = (uint8_t)(end - i);
        out[used + 5] = (uint8_t)((end - i) >> 8);
        memcpy(out + used + PACK_RUN_HEAD, data + i, end - i);
        used += PACK_RUN_HEAD + (end - i);
        i = end;
    }
    put32(out + 8, (uint32_t)used);     /* lets unpack notice a truncated save */
    return used;
}

size_t delta_unpack(uint8_t *out, size_t capacity, const uint8_t *packed, size_t packed_len,
                    const uint8_t *base, size_t base_len)
{
    size_t total, pos;

    if (packed_len < PACK_HEADER || get32(packed) != DELTA_PACK_MAGIC || get32(packed + 8) != packed_len) {
        return 0;
    }
    total = get32(packed + 4);
    if (total == 0 || total > capacity) {
        return 0;
    }

    /* Validate every run before writing anything */
    for (pos = PACK_HEADER; pos < packed_len; ) {
        size_t offset, count;
        if (packed_len - pos < PACK_RUN_HEAD) {
            return 0;
        }
        offset = get32(packed + pos);
        count = (size_t)packed[pos + 4] | ((size_t)packed[pos + 5] << 8);
        pos += PACK_RUN_HEAD;
        if (packed_len - pos < count || offset > total || count > total - offset) {
            return 0;
        }
        pos += count;
    }

    /* Apply: the base image first (zero past its end), then the runs */
    if (base_len >= total) {
        memcpy(out, base, total);
    } else {
        memcpy(out, base, base_len);
        memset(out + base_len, 0, total - base_len);
    }
    for (pos = PACK_HEADER; pos < packed_len; ) {
        size_t offset = get32(packed + pos);
        size_t count = (size_t)packed[pos + 4] | ((size_t)packed[pos + 5] << 8);
        pos += PACK_RUN_HEAD;
        memcpy(out + offset, packed + pos, count);
        pos += count;
    }
    return total;
}
