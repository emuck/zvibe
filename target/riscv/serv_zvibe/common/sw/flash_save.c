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
 * Read:  scan all slots, return data from the slot with the highest seq whose data matches its CRC
 *
 * A power cut during a write leaves its slot with a header and missing or partial data. Such a slot
 * fails its CRC, so it is skipped and the save before it is used; the next write goes to that damaged
 * slot rather than erasing the good one.
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

/* CRC-16/CCITT (bitwise — no lookup table, saves ~512B vs table), in steps */
static uint16_t crc16_update(uint16_t crc, const void *data, size_t len) {
    const uint8_t *p = (const uint8_t *)data;
    for (size_t i = 0; i < len; i++) {
        crc ^= (uint16_t)p[i] << 8;
        for (int b = 0; b < 8; b++)
            crc = (crc & 0x8000) ? (crc << 1) ^ 0x1021 : (crc << 1);
    }
    return crc;
}

static uint16_t crc16(const void *data, size_t len) {
    return crc16_update(0xFFFF, data, len);
}

/* A header that was written: the magic, and a sequence number that is not 0 (never used) or all ones
 * (the field of a header cut short before its sequence number was programmed). */
static int hdr_written(const save_hdr_t *h) {
    return h->magic == SAVE_MAGIC && h->seq != 0 && h->seq != 0xFFFFFFFFu;
}

/* Does slot idx hold data that matches the CRC in its header? (the data is read in small pieces) */
static int slot_intact(int idx, const save_hdr_t *h) {
    uint8_t  piece[64];
    uint16_t crc = 0xFFFF;
    uint32_t at = slot_base(idx) + sizeof(save_hdr_t);
    size_t   left = h->size;
    if (left > MAX_SAVE_DATA)
        return 0;
    while (left) {
        size_t n = left < sizeof piece ? left : sizeof piece;
        flash_read_bytes(at, piece, n);
        crc = crc16_update(crc, piece, n);
        at += (uint32_t)n;
        left -= n;
    }
    return crc == h->crc;
}

/* The slot with the highest sequence number whose save is whole; -1 if there is none. A newer slot that
 * fails its CRC (a save cut short) is passed over for the one before it. */
static int find_slot(void) {
    uint64_t limit = (uint64_t)1 << 32;          /* only sequence numbers below this are considered */
    for (;;) {
        uint32_t best_seq = 0;
        int      best     = -1;
        for (int i = 0; i < g_num_slots; i++) {
            save_hdr_t hdr;
            flash_read_bytes(slot_base(i), &hdr, sizeof(hdr));
            if (hdr_written(&hdr) && hdr.seq > best_seq && hdr.seq < limit) {
                best_seq = hdr.seq;
                best     = i;
            }
        }
        if (best < 0)
            return -1;
        {
            save_hdr_t hdr;
            flash_read_bytes(slot_base(best), &hdr, sizeof(hdr));
            if (slot_intact(best, &hdr))
                return best;
        }
        limit = best_seq;
    }
}

int flash_save_init(uint32_t base, int num_slots) {
    g_base      = base;
    g_num_slots = num_slots;
    g_next_seq  = 1;
    g_next_slot = 0;

    if (num_slots <= 0)
        return SAVE_OK;

    /* Sequence numbers go on from the highest one written, a cut-short save's included (so one is never
     * reused); the next save goes to the slot after the newest whole one, which is the damaged slot if
     * the last save was cut short. */
    uint32_t max_seq = 0;
    for (int i = 0; i < num_slots; i++) {
        save_hdr_t hdr;
        flash_read_bytes(slot_base(i), &hdr, sizeof(hdr));
        if (hdr_written(&hdr) && hdr.seq > max_seq)
            max_seq = hdr.seq;
    }
    if (max_seq)
        g_next_seq = max_seq + 1;

    int whole = find_slot();
    if (whole >= 0)
        g_next_slot = (whole + 1) % num_slots;
    return SAVE_OK;
}

ZV_RAMFUNC
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

/* The slot with the highest sequence number whose header was written, whole or not. When no slot is whole, the
 * caller still gets this one, so that its own checks report a damaged save (and not "no save"). */
static int newest_written_slot(void) {
    uint32_t best_seq = 0;
    int      best     = -1;
    for (int i = 0; i < g_num_slots; i++) {
        save_hdr_t hdr;
        flash_read_bytes(slot_base(i), &hdr, sizeof(hdr));
        if (hdr_written(&hdr) && hdr.seq > best_seq) {
            best_seq = hdr.seq;
            best     = i;
        }
    }
    return best;
}

int flash_save_exists(void) {
    return newest_written_slot() >= 0;
}

int flash_save_has_slots(void) {
    return g_num_slots > 0;
}

int flash_save_read(void *buf, size_t buf_len) {
    int idx = find_slot();
    if (idx < 0)
        idx = newest_written_slot();
    if (idx < 0)
        return SAVE_ERR_NOT_FOUND;

    save_hdr_t hdr;
    flash_read_bytes(slot_base(idx), &hdr, sizeof(hdr));

    if (hdr.size > buf_len)
        return SAVE_ERR_TOO_LARGE;

    flash_read_bytes(slot_base(idx) + sizeof(hdr), buf, hdr.size);
    return (int)hdr.size;
}
