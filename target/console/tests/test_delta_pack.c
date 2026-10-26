/* Copyright (c) 2025 Martin R. Raumann */
/* SPDX-License-Identifier: BSD-3-Clause */
/**
 * test_delta_pack.c - Packing a save blob against the story image
 *
 * delta_pack()/delta_unpack() (target/same51/src/save_delta.c, shared with the
 * RISC-V target) let a firmware with little save storage keep a V4 save of tens of
 * kilobytes in a few. They must round-trip every blob exactly, refuse anything
 * malformed without touching the output, and work on a real save from the core.
 */

#include <stdio.h>
#include <stdlib.h>
#include "test_story.h"
#include "save_delta.h"

static unsigned long lcg = 12345;
static unsigned rnd(void) { lcg = lcg * 1103515245UL + 12345UL; return (unsigned)(lcg >> 16) & 0x7FFF; }

/* Pack, unpack and compare. Returns the packed size (0 if packing failed). */
static size_t roundtrip(const uint8_t *data, size_t len, const uint8_t *base, size_t base_len, int line) {
    size_t cap = len + len / 2 + 64, packed_len, got;
    uint8_t *packed = malloc(cap), *out = malloc(len + 1);
    packed_len = delta_pack(packed, cap, data, len, base, base_len);
    if (packed_len) {
        memset(out, 0xEE, len + 1);
        got = delta_unpack(out, len, packed, packed_len, base, base_len);
        if (got != len || memcmp(out, data, len) != 0 || out[len] != 0xEE)
            printf("  round trip failed at line %d\n", line);
        CHECK(got == len);
        CHECK(memcmp(out, data, len) == 0);
        CHECK(out[len] == 0xEE);                           /* nothing written past the blob */
    }
    CHECK(packed_len != 0);
    free(packed);
    free(out);
    return packed_len;
}

static void test_roundtrip(void) {
    uint8_t base[3000], data[3500];
    size_t i, n;
    puts("== pack and unpack ==");
    for (i = 0; i < sizeof base; i++) base[i] = (uint8_t)(i * 7 + 3);

    memcpy(data, base, sizeof base);
    memset(data + sizeof base, 0x5A, 500);                  /* the execution state past the dynamic memory */
    n = roundtrip(data, sizeof base, base, sizeof base, __LINE__);       /* identical: header only */
    CHECK(n == 12);
    n = roundtrip(data, sizeof data, base, sizeof base, __LINE__);       /* only the tail differs: one run */
    CHECK(n == 12 + 6 + 500);

    data[10] ^= 1;                                          /* one changed byte near the front */
    n = roundtrip(data, sizeof data, base, sizeof base, __LINE__);
    CHECK(n == 12 + 6 + 1 + 6 + 500);

    data[14] ^= 1;                                          /* a second change 3 bytes on: merged into one run */
    n = roundtrip(data, sizeof data, base, sizeof base, __LINE__);
    CHECK(n == 12 + 6 + 5 + 6 + 500);

    data[2000] ^= 1;                                        /* a far change: its own run */
    n = roundtrip(data, sizeof data, base, sizeof base, __LINE__);
    CHECK(n == 12 + 6 + 5 + 6 + 1 + 6 + 500);

    data[2999] ^= 1;                                        /* last dynamic byte, abutting the tail run */
    roundtrip(data, sizeof data, base, sizeof base, __LINE__);
    data[0] ^= 1;                                           /* first byte */
    roundtrip(data, sizeof data, base, sizeof base, __LINE__);

    memcpy(data, base, sizeof base);
    roundtrip(data, sizeof base, base, 1500, __LINE__);     /* equal to the array past base_len, which must not count */
    data[10] ^= 1;
    roundtrip(data, 1, base, sizeof base, __LINE__);        /* blob shorter than the base */
    roundtrip(data, 100, base, 0, __LINE__);                /* no base at all */
    roundtrip(data, 3500, base, 1, __LINE__);

    {   /* a single changed run longer than a run can hold (65535) splits into two */
        size_t big = 200000;
        uint8_t *b = calloc(big, 1), *d = malloc(big);
        for (i = 0; i < big; i++) d[i] = (uint8_t)(1 + i % 250);
        n = roundtrip(d, big, b, big, __LINE__);
        CHECK(n == 12 + 4 * 6 + big);                       /* 65535 * 3 + 3395 */
        free(b); free(d);
    }

    for (n = 0; n < 40; n++) {                              /* random blobs, random edits */
        size_t len = 1 + rnd() % 3400, edits = rnd() % 60;
        memcpy(data, base, sizeof base);
        for (i = sizeof base; i < sizeof data; i++) data[i] = (uint8_t)rnd();
        for (i = 0; i < edits; i++) data[rnd() % sizeof data] = (uint8_t)rnd();
        roundtrip(data, len, base, (rnd() % 2) ? sizeof base : sizeof base - rnd() % 500, __LINE__);
    }
}

static void test_capacity(void) {
    uint8_t base[256], data[256], out[512];
    size_t need, i;
    puts("== capacity ==");
    for (i = 0; i < sizeof base; i++) { base[i] = (uint8_t)i; data[i] = (uint8_t)(i ^ 0x80); }
    need = delta_pack(out, sizeof out, data, sizeof data, base, sizeof base);
    CHECK(need == 12 + 6 + 256);
    CHECK(delta_pack(out, need, data, sizeof data, base, sizeof base) == need);          /* exactly enough */
    CHECK(delta_pack(out, need - 1, data, sizeof data, base, sizeof base) == 0);          /* one byte short */
    CHECK(delta_pack(out, 11, data, sizeof data, base, sizeof base) == 0);               /* not even the header */
    CHECK(delta_pack(out, 12, data, sizeof data, base, sizeof base) == 0);               /* header only, run needs room */
    CHECK(delta_pack(out, 12, base, sizeof base, base, sizeof base) == 12);              /* nothing to store fits */
}

static void test_rejects(void) {
    uint8_t base[300], data[300], packed[700], out[300], guard[300];
    size_t n, i, len;
    puts("== malformed input ==");
    for (i = 0; i < sizeof base; i++) { base[i] = (uint8_t)i; data[i] = base[i]; }
    data[5] = 0; data[200] = 0; data[201] = 1;
    n = delta_pack(packed, sizeof packed, data, sizeof data, base, sizeof base);
    CHECK(n == 12 + 6 + 1 + 6 + 2);
    memset(guard, 0x77, sizeof guard);

#define REJECT(call, what) do { memset(out, 0x77, sizeof out); \
        CHECK((call) == 0); CHECK(memcmp(out, guard, sizeof out) == 0); (void)(what); } while (0)

    CHECK(delta_unpack(out, sizeof out, packed, n, base, sizeof base) == 300);

    for (len = 0; len < n; len++) {                                     /* every truncation */
        memset(out, 0x77, sizeof out);
        CHECK(delta_unpack(out, sizeof out, packed, len, base, sizeof base) == 0);
        CHECK(memcmp(out, guard, sizeof out) == 0);
    }
    {   uint8_t bad[700];
        memcpy(bad, packed, n); bad[0] ^= 1;                            /* magic */
        REJECT(delta_unpack(out, sizeof out, bad, n, base, sizeof base), "magic");
        memcpy(bad, packed, n); bad[8] ^= 1;                            /* recorded length */
        REJECT(delta_unpack(out, sizeof out, bad, n, base, sizeof base), "length");
        memcpy(bad, packed, n); bad[4] = 0xFF; bad[5] = 0x7F;           /* total larger than the output */
        REJECT(delta_unpack(out, sizeof out, bad, n, base, sizeof base), "total");
        memcpy(bad, packed, n); bad[12] = 0xFF;                         /* first run's offset past the end */
        bad[13] = 0xFF;
        REJECT(delta_unpack(out, sizeof out, bad, n, base, sizeof base), "offset");
        memcpy(bad, packed, n); bad[16] = 0xFF;                         /* first run claims too many bytes */
        REJECT(delta_unpack(out, sizeof out, bad, n, base, sizeof base), "count");
        memcpy(bad, packed, n); bad[4] = 100; bad[5] = 0;               /* total smaller than the runs reach */
        REJECT(delta_unpack(out, sizeof out, bad, n, base, sizeof base), "total below runs");
    }
    {   /* a run that starts inside the blob but reaches past its end */
        uint8_t one[100], bad[100];
        size_t m = delta_pack(one, sizeof one, data, 6, base, sizeof base);   /* one run: byte 5 */
        CHECK(m == 12 + 6 + 1);
        memcpy(bad, one, m); bad[4] = 5;                                  /* total 5: byte 5 is outside */
        memset(out, 0x77, sizeof out);
        CHECK(delta_unpack(out, sizeof out, bad, m, base, sizeof base) == 0);
        CHECK(memcmp(out, guard, sizeof out) == 0);
    }
    memset(out, 0x77, sizeof out);
    CHECK(delta_unpack(out, 299, packed, n, base, sizeof base) == 0);   /* output one byte too small */
    CHECK(memcmp(out, guard, sizeof out) == 0);
    CHECK(delta_unpack(out, sizeof out, NULL, 0, base, sizeof base) == 0);
}

/* ---- a real save from the core ---- */

#define DYN 0x400
static void test_real_save(void) {
    zvibeContext *ctx = zvibe_create(sink);
    uint8_t *blob, *packed, *rebuilt;
    size_t size, packed_len, i;
    uint8_t ram_before[DYN];
    zWord stack_before[8];
    zDWord pc_before;
    puts("== a core save through pack and unpack ==");
    make_story_static(4, 4096, DYN);
    CHECK(zvibe_load_story_from_memory(ctx, story, 4096) == ZVIBE_OK);

    for (i = 0x40; i < DYN; i += 5) G->memory_state.ram_buffer[i] = (uint8_t)(i * 3);     /* a played-in game */
    for (i = 0; i < 6; i++) G->stack_mem[i] = (zWord)(0x2000 + i);
    G->stack_ptr = G->stack_mem + 6;
    G->logical_pc = 0x2F0;
    G->base_ptr = 2;
    memcpy(ram_before, G->memory_state.ram_buffer, DYN);
    memcpy(stack_before, G->stack_mem, sizeof stack_before);
    pc_before = G->logical_pc;

    size = zvibe_get_save_size(ctx);
    blob = malloc(size);
    CHECK(zvibe_get_save_data(ctx, (char *)blob, size) == size);
    packed = malloc(size);
    packed_len = delta_pack(packed, size, blob, size, story, DYN);       /* the base is the story's dynamic memory */
    CHECK(packed_len != 0 && packed_len < size);
    printf("   save of %zu bytes packs to %zu\n", size, packed_len);

    memset(G->memory_state.ram_buffer + 0x40, 0xAA, DYN - 0x40);           /* play on, then restore */
    G->stack_ptr = G->stack_mem;
    G->logical_pc = 0x999;
    rebuilt = malloc(size);
    CHECK(delta_unpack(rebuilt, size, packed, packed_len, story, DYN) == size);
    CHECK(memcmp(rebuilt, blob, size) == 0);
    CHECK(zvibe_restore_data(ctx, (const char *)rebuilt, size) == ZVIBE_OK);
    CHECK(memcmp(G->memory_state.ram_buffer, ram_before, DYN) == 0);
    CHECK(memcmp(G->stack_mem, stack_before, sizeof stack_before) == 0);
    CHECK(G->stack_ptr - G->stack_mem == 6 && G->logical_pc == pc_before && G->base_ptr == 2);
    free(blob); free(packed); free(rebuilt);
    zvibe_destroy(ctx);
}

int main(void) {
    test_roundtrip();
    test_capacity();
    test_rejects();
    test_real_save();
    printf("\nTests: %d, passed: %d\n", tests_run, tests_passed);
    return tests_run == tests_passed ? 0 : 1;
}
