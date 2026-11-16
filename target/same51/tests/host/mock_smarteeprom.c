/* Copyright (c) 2025 Martin R. Raumann */
/* SPDX-License-Identifier: BSD-3-Clause */
#include <string.h>
#include "smarteeprom.h"
#include "mock_smarteeprom.h"

#define PAGE SMARTEEPROM_PAGE

uint8_t mock_mem[SMARTEEPROM_SIZE];
int mock_commits, mock_cut_at = -1, mock_drop_at = -1, mock_drop_count = 1, mock_available = 1;
jmp_buf mock_power_cut;

static int buf_page = -1;
static uint8_t buf[PAGE];
static int buf_dirty;

void mock_erase(void) { memset(mock_mem, 0xFF, sizeof mock_mem); buf_page = -1; buf_dirty = 0; mock_reset_counters(); }
void mock_reset_counters(void) { mock_commits = 0; mock_cut_at = -1; mock_drop_at = -1; mock_drop_count = 1; }
void mock_reboot(void) { buf_page = -1; buf_dirty = 0; mock_reset_counters(); }

static void commit(void) {
    if (buf_page < 0 || !buf_dirty) { buf_dirty = 0; return; }
    if (mock_cut_at >= 0 && mock_commits == mock_cut_at) {      /* power cut: this commit and the buffer are lost */
        buf_page = -1; buf_dirty = 0;
        longjmp(mock_power_cut, 1);
    }
    if (!(mock_drop_at >= 0 && mock_commits >= mock_drop_at && mock_commits < mock_drop_at + mock_drop_count))
        memcpy(mock_mem + (size_t)buf_page * PAGE, buf, PAGE);   /* atomic: the whole page or nothing */
    mock_commits++;
    buf_dirty = 0;
}

static void select_page(int page) {
    if (page == buf_page) return;
    commit();
    buf_page = page;
    memcpy(buf, mock_mem + (size_t)page * PAGE, PAGE);
}

bool smarteeprom_init(void) { return mock_available; }

bool smarteeprom_write_raw(uint32_t offset, const void *data, size_t size) {
    const uint8_t *src = (const uint8_t *)data;
    size_t words = (size + 3) / 4, i;
    if ((offset & 3) != 0 || offset + words * 4 > SMARTEEPROM_SIZE) return false;
    for (i = 0; i < words; i++) {
        uint32_t word = 0xFFFFFFFFUL, off = offset + (uint32_t)i * 4;
        size_t n = size - i * 4;
        memcpy(&word, src + i * 4, n < 4 ? n : 4);
        select_page((int)(off / PAGE));
        memcpy(buf + off % PAGE, &word, 4);
        buf_dirty = 1;
    }
    return true;
}

bool smarteeprom_write_end_mark(uint32_t data_end_offset) {
    static const uint32_t mark = 0xA55A5AA5UL;
    return smarteeprom_write_raw((data_end_offset + 3) & ~3UL, &mark, sizeof mark);
}

bool smarteeprom_flush(void) { commit(); return true; }

bool smarteeprom_read_data(uint32_t offset, void *buffer, size_t size) {
    if (offset + size > SMARTEEPROM_SIZE) return false;
    /* a read sees the committed content (the page in the buffer is not stored until it is committed) */
    memcpy(buffer, mock_mem + offset, size);
    return true;
}
