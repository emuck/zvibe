/* Copyright (c) 2025 Martin R. Raumann */
/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Flash Save System — Simple Round-Robin Wear Leveling
 *
 * Platform-agnostic. Config comes from flash_save_config.h (per-board via -I):
 *   SLOT_SIZE, SECTOR_SIZE, SAVE_HEADER_SIZE, MAX_SAVE_DATA, SAVE_MAGIC
 *
 * Save region location and slot count are passed at runtime to flash_save_init()
 * so the firmware binary is independent of the flash layout (no circular
 * dependency between flash_layout.h generation and firmware compilation).
 *
 * Slot layout: [12-byte header][save data]
 *   magic (4B): SAVE_MAGIC
 *   seq   (4B): monotonic counter — highest is newest
 *   size  (2B): bytes of save data
 *   crc   (2B): CRC-16/CCITT of data
 *
 * Write: erase slot[seq % n] → write header → write data → seq++
 * Read:  scan all slots, return data from slot with highest valid seq
 *
 * CRITICAL: write/erase carry .ramfunc — XIP fetch is unavailable while
 * flash is being erased or programmed.
 */

#include "flash_save.h"
#include "flash_save_config.h"
#include "flash_hal.h"

/* Runtime state — populated by flash_save_init() */
static uint32_t g_base      = 0;
static int      g_num_slots = 0;
static uint32_t g_next_seq  = 1;
static int      g_next_slot = 0;

/* 12-byte slot header */
typedef struct {
    uint32_t magic;
    uint32_t seq;
    uint16_t size;
    uint16_t crc;
} save_hdr_t;

_Static_assert(sizeof(save_hdr_t) == SAVE_HEADER_SIZE,
               "save_hdr_t size mismatch with SAVE_HEADER_SIZE");

static uint32_t slot_base(int idx) {
    return g_base + (uint32_t)idx * SLOT_SIZE;
}

/* CRC-16/CCITT (bitwise — no lookup table, saves ~512B vs table) */
static uint16_t crc16(const void *data, size_t len) {
    const uint8_t *p = (const uint8_t *)data;
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= (uint16_t)p[i] << 8;
        for (int b = 0; b < 8; b++)
            crc = (crc & 0x8000) ? (crc << 1) ^ 0x1021 : (crc << 1);
    }
    return crc;
}

int flash_save_init(uint32_t base, int num_slots) {
    g_base      = base;
    g_num_slots = num_slots;
    g_next_seq  = 1;
    g_next_slot = 0;

    if (num_slots <= 0)
        return SAVE_OK;

    uint32_t best_seq  = 0;
    int      best_slot = -1;

    for (int i = 0; i < num_slots; i++) {
        save_hdr_t hdr;
        flash_read_bytes(slot_base(i), &hdr, sizeof(hdr));
        if (hdr.magic == SAVE_MAGIC && hdr.seq > best_seq) {
            best_seq  = hdr.seq;
            best_slot = i;
        }
    }

    if (best_slot >= 0) {
        g_next_seq  = best_seq + 1;
        g_next_slot = (best_slot + 1) % num_slots;
    }
    return SAVE_OK;
}

__attribute__((section(".ramfunc")))
int flash_save_write(const void *data, size_t data_len) {
    if (g_num_slots <= 0)
        return SAVE_ERR_NO_SPACE;
    if (data_len > MAX_SAVE_DATA)
        return SAVE_ERR_TOO_LARGE;

    uint32_t base = slot_base(g_next_slot);

    if (flash_erase_sector(base) < 0)
        return SAVE_ERR_FLASH;

    save_hdr_t hdr = {
        .magic = SAVE_MAGIC,
        .seq   = g_next_seq,
        .size  = (uint16_t)data_len,
        .crc   = crc16(data, data_len),
    };

    if (flash_program_bytes(base, &hdr, sizeof(hdr)) < 0)
        return SAVE_ERR_FLASH;

    if (flash_program_bytes(base + sizeof(hdr), data, data_len) < 0)
        return SAVE_ERR_FLASH;

    g_next_seq++;
    g_next_slot = (g_next_slot + 1) % g_num_slots;
    return SAVE_OK;
}

static int find_slot(void) {
    uint32_t best_seq = 0;
    int      best     = -1;
    for (int i = 0; i < g_num_slots; i++) {
        save_hdr_t hdr;
        flash_read_bytes(slot_base(i), &hdr, sizeof(hdr));
        if (hdr.magic == SAVE_MAGIC && hdr.seq > best_seq) {
            best_seq = hdr.seq;
            best     = i;
        }
    }
    return best;
}

int flash_save_exists(void) {
    return find_slot() >= 0;
}

int flash_save_has_slots(void) {
    return g_num_slots > 0;
}

int flash_save_read(void *buf, size_t buf_len) {
    int idx = find_slot();
    if (idx < 0)
        return SAVE_ERR_NOT_FOUND;

    save_hdr_t hdr;
    flash_read_bytes(slot_base(idx), &hdr, sizeof(hdr));

    if (hdr.size > buf_len)
        return SAVE_ERR_TOO_LARGE;

    flash_read_bytes(slot_base(idx) + sizeof(hdr), buf, hdr.size);
    return (int)hdr.size;
}
