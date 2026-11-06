/* Copyright (c) 2025 Martin R. Raumann */
/* SPDX-License-Identifier: BSD-3-Clause */
/* Shared helpers for tests that need a minimal loadable story in memory. */

#ifndef TEST_STORY_H
#define TEST_STORY_H

#include "zvibe_api.h"
#include "zvibe.h"
#include <string.h>

#define CHECK(cond) do { tests_run++; if (cond) tests_passed++; \
    else printf("FAIL: %s (line %d)\n", #cond, __LINE__); } while (0)

static int tests_run, tests_passed;

static void sink(const char *text, size_t length) { (void)text; (void)length; }

/* A story buffer big enough for the largest limit under test */
static uint8_t story[Z4_STORY_LIMIT + 16];

static void put16(size_t off, unsigned v) { story[off] = (uint8_t)(v >> 8); story[off + 1] = (uint8_t)v; }

/* Header-only story: every address valid for the loader's acceptance checks,
 * rest zero. It loads; it is not an executable story. */
static void make_story_static(uint8_t version, size_t size, unsigned static_base) {
    memset(story, 0, size);
    story[0] = version;
    put16(4, 0x0100);          /* high memory base */
    put16(6, 0x0100);          /* initial PC */
    put16(8, 0x0100);          /* dictionary */
    put16(10, 0x0100);         /* object table */
    put16(12, 0x0200);         /* globals (needs 480 bytes below size) */
    put16(14, static_base);    /* static memory base = size of dynamic memory */
    put16(24, 0x0100);         /* abbreviations */
}

__attribute__((unused)) static void make_story(uint8_t version, size_t size) { make_story_static(version, size, 0x0400); }

#endif /* TEST_STORY_H */
