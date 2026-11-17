/* Copyright (c) 2025 Martin R. Raumann */
/* SPDX-License-Identifier: BSD-3-Clause */
/*******************************************************************************
  save_store.c - see save_store.h
*******************************************************************************/

#include <stdint.h>
#include <string.h>
#include "smarteeprom.h"
#include "save_store.h"

#define RECORD_MAGIC   0x5253565AUL          /* "ZVSR" */
#define CHUNK          64                    /* bytes read or copied at a time */

typedef struct {
    uint32_t magic;
    uint32_t seq;                            /* grows with every save: the newest record wins */
    uint32_t tag;                            /* which story the data belongs to */
    uint16_t game;
    uint16_t len;                            /* data bytes */
    uint32_t data_crc;
    uint32_t hdr_crc;                        /* CRC-32 of the fields above */
} record_header_t;                           /* 24 bytes */

#define HDR_BYTES      ((uint32_t)sizeof(record_header_t))
#define HDR_CRC_SPAN   (HDR_BYTES - 4)

static uint32_t g_area;                      /* bytes per area */
static int      g_games;
static bool     g_available;
static uint32_t g_seq;                       /* the sequence number the next save will use */

#ifdef SAVE_TEST_HOOKS
volatile int save_store_fault;
#define FAULT(n) do { if (save_store_fault == (n)) for (;;) { } } while (0)
#else
#define FAULT(n) ((void)0)
#endif

/* ---- CRC-32 (IEEE, as save_crc32 in save_delta.h), in steps so that a record can be checked as it is read ---- */

static uint32_t crc_step(uint32_t crc, const uint8_t *p, size_t n) {
    size_t i;
    for (i = 0; i < n; i++) {
        int bit;
        crc ^= p[i];
        for (bit = 0; bit < 8; bit++)
            crc = (crc >> 1) ^ (0xEDB88320UL & (0UL - (crc & 1UL)));
    }
    return crc;
}

/* ---- layout ---- */

static uint32_t area_base(int index) { return (uint32_t)index * g_area; }
static int      scratch_index(void)  { return g_games; }

size_t save_store_capacity(void) {
    /* header, the data padded to a word, the end mark */
    return g_area > HDR_BYTES + SAVE_END_MARK_BYTES + 3u ? (size_t)(g_area - HDR_BYTES - SAVE_END_MARK_BYTES) & ~(size_t)3 : 0;
}

void save_store_init(uint32_t area_size, int games, bool available) {
    g_area = area_size;
    g_games = games;
    g_available = available;
    g_seq = 1;
}

/* ---- reading a record ---- */

/* CRC-32 of len bytes of the SmartEEPROM from offset, read a chunk at a time; copied to copy_to as well, if given */
static bool read_chunked_crc(uint32_t offset, uint32_t len, uint32_t *crc_out, uint8_t *copy_to) {
    uint32_t words[CHUNK / 4];                   /* word aligned: the SmartEEPROM is read a word at a time */
    uint8_t *buf = (uint8_t *)words;
    uint32_t crc = 0xFFFFFFFFUL, done = 0;
    while (done < len) {
        uint32_t n = len - done < CHUNK ? len - done : CHUNK;
        if (!smarteeprom_read_data(offset + done, buf, (n + 3u) & ~3u)) return false;
        crc = crc_step(crc, buf, n);
        if (copy_to) memcpy(copy_to + done, buf, n);
        done += n;
    }
    *crc_out = ~crc;
    return true;
}

/* The header of the record in area `index`: false if there is none (no magic) or it is not intact. */
static bool read_header(int index, record_header_t *h, bool *magic_seen) {
    *magic_seen = false;
    if (!smarteeprom_read_data(area_base(index), h, HDR_BYTES)) return false;
    if (h->magic != RECORD_MAGIC) return false;
    *magic_seen = true;
    if (~crc_step(0xFFFFFFFFUL, (const uint8_t *)h, HDR_CRC_SPAN) != h->hdr_crc) return false;
    return h->len <= save_store_capacity();
}

/* The data of the record whose header is h: its CRC must match; copied to copy_to as it is read, if given. */
static bool check_data(int index, const record_header_t *h, uint8_t *copy_to) {
    uint32_t crc;
    return read_chunked_crc(area_base(index) + HDR_BYTES, h->len, &crc, copy_to) && crc == h->data_crc;
}

typedef struct {
    bool whole;                              /* header and data check out */
    record_header_t h;
} record_info_t;

static record_info_t read_record(int index) {
    record_info_t r;
    bool seen;
    memset(&r, 0, sizeof r);
    r.whole = read_header(index, &r.h, &seen) && check_data(index, &r.h, NULL);
    return r;
}

/* ---- writing a record ---- */

/* Write a record (header + data + end mark) into area `index` and commit it. */
static bool write_record(int index, uint32_t seq, uint32_t tag, int game, const void *data, size_t len, uint32_t data_crc) {
    record_header_t h;
    uint32_t base = area_base(index);
    h.magic = RECORD_MAGIC;
    h.seq = seq;
    h.tag = tag;
    h.game = (uint16_t)game;
    h.len = (uint16_t)len;
    h.data_crc = data_crc;
    h.hdr_crc = ~crc_step(0xFFFFFFFFUL, (const uint8_t *)&h, HDR_CRC_SPAN);
    if (!smarteeprom_write_raw(base, &h, HDR_BYTES) ||
        !smarteeprom_write_raw(base + HDR_BYTES, data, len) ||
        !smarteeprom_write_end_mark(base + HDR_BYTES + (uint32_t)len)) return false;
    return true;
}

/* The same record, copied word for word from one area to another and committed (used by recovery). */
static bool copy_record(int from, int to, const record_header_t *h) {
    uint32_t words[CHUNK / 4];
    uint8_t *buf = (uint8_t *)words;
    uint32_t total = HDR_BYTES + (((uint32_t)h->len + 3u) & ~3u), done = 0;
    while (done < total) {
        uint32_t n = total - done < CHUNK ? total - done : CHUNK;
        if (!smarteeprom_read_data(area_base(from) + done, buf, n)) return false;
        if (!smarteeprom_write_raw(area_base(to) + done, buf, n)) return false;
        done += n;
    }
    return smarteeprom_write_end_mark(area_base(to) + total) && smarteeprom_flush();
}

/* Is area `index` holding exactly the record (seq, tag, len, data_crc)? */
static bool holds(int index, uint32_t seq, uint32_t tag, size_t len, uint32_t data_crc) {
    record_info_t r = read_record(index);
    return r.whole && r.h.seq == seq && r.h.tag == tag && r.h.len == len && r.h.data_crc == data_crc;
}

/* ---- the public operations ---- */

save_result_t save_store_write(int game, uint32_t tag, const void *data, size_t len) {
    uint32_t seq, data_crc;
    int attempt;
    if (!g_available) return SAVE_E_UNAVAILABLE;
    if (game < 0 || game >= g_games || len == 0) return SAVE_E_WRITE;                 /* a caller's mistake */
    if (len > save_store_capacity()) return SAVE_E_TOO_LARGE;

    /* A refused save left its record only in the scratch. Put it into its area first: the scratch is about to be reused,
     * and a cut during that must not find the only whole copy in it. (Does nothing when nothing is pending.) */
    save_store_recover();

    seq = g_seq;
    data_crc = ~crc_step(0xFFFFFFFFUL, (const uint8_t *)data, len);

    /* 1. the scratch: complete and read back before the game's own area is touched */
    FAULT(1);
    if (!write_record(scratch_index(), seq, tag, game, data, len, data_crc)) return SAVE_E_WRITE;
    FAULT(2);
    if (!smarteeprom_flush()) return SAVE_E_WRITE;
    FAULT(3);
    if (!holds(scratch_index(), seq, tag, len, data_crc)) return SAVE_E_VERIFY;

    /* 2. the game's area, read back too (a second try if the first did not take) */
    for (attempt = 0; attempt < 2; attempt++) {
        if (!write_record(game, seq, tag, game, data, len, data_crc)) return SAVE_E_WRITE;
        FAULT(4);
        if (!smarteeprom_flush()) return SAVE_E_WRITE;
        FAULT(5);
        if (holds(game, seq, tag, len, data_crc)) {
            g_seq = seq + 1;
            return SAVE_OK;
        }
    }
    return SAVE_E_VERIFY;       /* the scratch still holds the new save: recovery at the next start retries the copy */
}

load_result_t save_store_read(int game, uint32_t tag, void *buf, size_t cap, size_t *len) {
    record_header_t h;
    bool seen;
    if (!g_available) return LOAD_UNAVAILABLE;
    if (game < 0 || game >= g_games) return LOAD_NONE;
    if (!read_header(game, &h, &seen)) return seen ? LOAD_DAMAGED : LOAD_NONE;   /* no magic: never used (or erased) */
    if (h.tag != tag) return LOAD_OTHER_STORY;
    if (h.len > cap || !check_data(game, &h, (uint8_t *)buf)) return LOAD_DAMAGED;
    *len = h.len;
    return LOAD_OK;
}

void save_store_recover(void) {
    int i;
    record_info_t scratch;
    uint32_t max_seq = 0;
    if (!g_available) return;

    for (i = 0; i <= g_games; i++) {
        record_info_t r = read_record(i);
        if (r.whole && r.h.seq > max_seq) max_seq = r.h.seq;
    }
    g_seq = max_seq + 1;

    /* A save that was interrupted after the scratch was complete: the scratch holds the newest record of
     * its game, and the game's own area is empty, damaged or older. Finish the copy. */
    scratch = read_record(scratch_index());
    if (scratch.whole && scratch.h.game < g_games) {
        int g = scratch.h.game;
        record_info_t own = read_record(g);
        /* Newer by sequence alone: the numbers are global, and a normal save replaces whatever the area held, a save
         * of another story included. (Comparing tags here stranded the first save of a replaced story.) */
        bool newer = !own.whole || own.h.seq < scratch.h.seq;
        if (newer) {
            int attempt;
            for (attempt = 0; attempt < 2; attempt++) {
                if (copy_record(scratch_index(), g, &scratch.h) &&
                    holds(g, scratch.h.seq, scratch.h.tag, scratch.h.len, scratch.h.data_crc)) break;
            }
        }
    }
}

const char *save_store_save_message(save_result_t r) {
    switch (r) {
    case SAVE_OK:            return "";
    case SAVE_E_UNAVAILABLE: return "[Save failed: the SmartEEPROM is not available]";
    case SAVE_E_TOO_LARGE:   return "[Save failed: the changes do not fit this game's save area]";
    case SAVE_E_WRITE:       return "[Save failed: the SmartEEPROM did not take the write]";
    default:                 return "[Save failed: it did not read back correctly]";
    }
}

const char *save_store_load_message(load_result_t r) {
    switch (r) {
    case LOAD_OK:            return "";
    case LOAD_UNAVAILABLE:   return "[Restore failed: the SmartEEPROM is not available]";
    case LOAD_NONE:          return "[No saved game found]";
    case LOAD_DAMAGED:       return "[The saved game is damaged]";
    default:                 return "[That save is from a different story or release]";
    }
}
