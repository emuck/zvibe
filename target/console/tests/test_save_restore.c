/* Copyright (c) 2025 Martin R. Raumann */
/* SPDX-License-Identifier: BSD-3-Clause */
/**
 * test_save_restore.c - Restore safety for raw saves and delta saves
 *
 * A save made by a build with a larger stack (ZVIBE_STACK_WORDS) must be
 * rejected, never copied past the end of the stack, and ANY rejected restore
 * must leave the running machine untouched (RAM, stack, PC, base pointer, stack
 * pointer). Covers the core's raw restore (zvibe_restore_data) and the delta
 * codec shared by the SAM E51 and RISC-V targets
 * (target/same51/src/save_delta.c; the RISC-V copy is checked identical by the
 * Makefile).
 */

#include <stdio.h>
#include <stdlib.h>
#include "test_story.h"
#include "save_delta.h"

/* ====================================================================== */
/* Raw restore (core)                                                      */
/* ====================================================================== */

#define STORY_SIZE 4096
#define DYN_SIZE   0x400            /* make_story() static memory base */

typedef struct {
    uint8_t ram[DYN_SIZE];
    zWord stack[ZVIBE_STACK_WORDS];
    size_t stack_words;
    zDWord pc;
    zWord base_ptr;
} live_t;

static void take(live_t *s) {
    memcpy(s->ram, G->memory_state.ram_buffer, DYN_SIZE);
    memset(s->stack, 0, sizeof s->stack);
    s->stack_words = (size_t)(G->stack_ptr - G->stack_mem);
    memcpy(s->stack, G->stack_mem, s->stack_words * sizeof(zWord));
    s->pc = G->logical_pc;
    s->base_ptr = G->base_ptr;
}

static int same(const live_t *a) {
    live_t b;
    take(&b);
    return a->stack_words == b.stack_words && a->pc == b.pc && a->base_ptr == b.base_ptr &&
           memcmp(a->ram, b.ram, DYN_SIZE) == 0 &&
           memcmp(a->stack, b.stack, a->stack_words * sizeof(zWord)) == 0;
}

/* Give the live machine a recognizable state: `words` stack words, a PC, etc.
 * The 64-byte header is left alone: it identifies the story, and a restore refuses
 * a save whose header does not match (see ZVIBE_VERIFY_SAVE_IDENTITY). */
static void set_state(uint8_t ram_seed, size_t words, zDWord pc, zWord bp) {
    size_t i;
    for (i = 0x40; i < DYN_SIZE; i++) G->memory_state.ram_buffer[i] = (uint8_t)(ram_seed + i * 3);
    for (i = 0; i < words; i++) G->stack_mem[i] = (zWord)(0x1000 + ram_seed + i);
    G->stack_ptr = G->stack_mem + words;
    G->logical_pc = pc;
    G->base_ptr = bp;
}

static void test_raw_restore(void) {
    zvibeContext *ctx = zvibe_create(sink);
    live_t good, mutated;
    size_t save_size, i, ram = DYN_SIZE;
    uint8_t *blob, *bad;
    uint32_t big;
    puts("== raw restore ==");
    make_story(3, STORY_SIZE);
    CHECK(ctx != NULL);
    CHECK(zvibe_load_story_from_memory(ctx, story, STORY_SIZE) == ZVIBE_OK);
    CHECK(G->memory_state.ram_size == ram);

    /* a save with a non-empty, in-capacity stack */
    set_state(1, 5, 0x222, 3);
    take(&good);
    save_size = zvibe_get_save_size(ctx);
    blob = malloc(save_size);
    CHECK(blob != NULL);
    CHECK(zvibe_get_save_data(ctx, (char *)blob, save_size) == save_size);
    CHECK(save_size == ram + 4 + 4 + 5 * sizeof(zWord) + sizeof(G->base_ptr));

    /* a different live state, then a valid restore brings back the saved one */
    set_state(77, 2, 0x999, 7);
    CHECK(zvibe_restore_data(ctx, (const char *)blob, save_size) == ZVIBE_OK);
    CHECK(same(&good));

    /* Rejections. Each starts from a different live state and must leave it exactly. */
    set_state(50, 2, 0x999, 7);
    take(&mutated);

    /* (a) stack count above capacity, length consistent with the claim */
    big = ZVIBE_STACK_WORDS + 1;
    bad = malloc(ram + 8 + big * sizeof(zWord) + sizeof(G->base_ptr));
    memcpy(bad, blob, ram + 8);                                  /* RAM, pc, count */
    memcpy(bad + ram + 4, &big, sizeof big);                     /* claim cap+1 words */
    for (i = 0; i < big * sizeof(zWord); i++) bad[ram + 8 + i] = 0x55;
    memset(bad + ram + 8 + big * sizeof(zWord), 0, sizeof(G->base_ptr));
    bad[100] = 0xAA;                                             /* RAM differs from live */
    CHECK(zvibe_restore_data(ctx, (const char *)bad, ram + 8 + big * sizeof(zWord) + sizeof(G->base_ptr)) != ZVIBE_OK);
    CHECK(same(&mutated));
    free(bad);

    /* (b) one byte short: still >= the fixed minimum, so only the LATER
     *     serialized-length check can reject it */
    CHECK(save_size - 1 >= ram + 4 + 4 + sizeof(G->base_ptr));
    CHECK(zvibe_restore_data(ctx, (const char *)blob, save_size - 1) != ZVIBE_OK);
    CHECK(same(&mutated));

    /* (c) cut down to exactly the fixed minimum: the stack words are missing */
    CHECK(zvibe_restore_data(ctx, (const char *)blob, ram + 4 + 4 + sizeof(G->base_ptr)) != ZVIBE_OK);
    CHECK(same(&mutated));

    /* (d) below the fixed minimum */
    CHECK(zvibe_restore_data(ctx, (const char *)blob, ram + 4 + 4 + sizeof(G->base_ptr) - 1) != ZVIBE_OK);
    CHECK(zvibe_restore_data(ctx, (const char *)blob, 0) != ZVIBE_OK);
    CHECK(same(&mutated));
    free(blob);

    /* A save with exactly ZVIBE_STACK_WORDS words restores completely */
    set_state(9, ZVIBE_STACK_WORDS, 0x444, 11);
    take(&good);
    save_size = zvibe_get_save_size(ctx);
    blob = malloc(save_size);
    CHECK(zvibe_get_save_data(ctx, (char *)blob, save_size) == save_size);
    set_state(60, 1, 0x1, 1);
    CHECK(zvibe_restore_data(ctx, (const char *)blob, save_size) == ZVIBE_OK);
    CHECK(same(&good));
    CHECK((size_t)(G->stack_ptr - G->stack_mem) == ZVIBE_STACK_WORDS);
    free(blob);

    /* An empty stack round-trips too */
    set_state(3, 0, 0x100, 0);
    take(&good);
    save_size = zvibe_get_save_size(ctx);
    blob = malloc(save_size);
    CHECK(zvibe_get_save_data(ctx, (char *)blob, save_size) == save_size);
    set_state(40, 3, 0x5, 5);
    CHECK(zvibe_restore_data(ctx, (const char *)blob, save_size) == ZVIBE_OK);
    CHECK(same(&good));
    free(blob);
    zvibe_destroy(ctx);
}

/* ====================================================================== */
/* Delta restore (shared codec)                                            */
/* ====================================================================== */

#define DYN 256
#define CAP 8                       /* stack capacity of the "receiving build" */
#define SENTINEL 0xA5
#define SENT_STACK 0xBEEF

typedef struct {
    uint8_t ram[DYN];
    uint16_t stack[CAP];
    uint32_t pc, bp;
    size_t sw;
} out_t;

static void arm(out_t *o) {
    size_t i;
    memset(o->ram, SENTINEL, sizeof o->ram);
    for (i = 0; i < CAP; i++) o->stack[i] = SENT_STACK;
    o->pc = 0xDEADBEEF; o->bp = 0xFEEDFACE; o->sw = 99;
}

static int is_armed(const out_t *o) {
    size_t i;
    for (i = 0; i < DYN; i++) if (o->ram[i] != SENTINEL) return 0;
    for (i = 0; i < CAP; i++) if (o->stack[i] != SENT_STACK) return 0;
    return o->pc == 0xDEADBEEF && o->bp == 0xFEEDFACE && o->sw == 99;
}

/* Decode from an exact-size heap copy of the first n bytes, so a decoder that
 * reads past the end of its input is caught by AddressSanitizer
 * (test_save_restore_san) instead of quietly reading the rest of a larger array. */
static int decode(const uint8_t *blob, size_t n, const uint8_t *orig, out_t *o) {
    uint8_t *exact = malloc(n ? n : 1);
    int r;
    memcpy(exact, blob, n);
    r = delta_decode(exact, n, o->ram, orig, DYN, &o->pc, o->stack, CAP, &o->sw, &o->bp);
    free(exact);
    return r;
}

static void get_hdr(const uint8_t *blob, delta_header_t *h) { memcpy(h, blob, sizeof *h); }
static void put_hdr(uint8_t *blob, const delta_header_t *h) { memcpy(blob, h, sizeof *h); }

/* decode must fail with `want` (or any negative if want == 0) and leave all outputs armed */
static void expect_reject(const char *what, const uint8_t *blob, size_t n, const uint8_t *orig, int want) {
    out_t o;
    int r;
    arm(&o);
    r = decode(blob, n, orig, &o);
    if (!(want ? r == want : r < 0) || !is_armed(&o))
        printf("  reject case failed: %s (rc=%d, expected %d, untouched=%d)\n", what, r, want, is_armed(&o));
    CHECK(want ? r == want : r < 0);
    CHECK(is_armed(&o));
}

static void test_delta_restore(void) {
    uint8_t orig[DYN], cur[DYN];
    uint16_t stack_in[CAP];
    uint8_t blob[512];
    size_t n, i, hs = sizeof(delta_header_t), stack_bytes = CAP * sizeof(uint16_t);
    delta_header_t h;
    out_t o;
    puts("== delta restore ==");
    for (i = 0; i < DYN; i++) { orig[i] = (uint8_t)i; cur[i] = (uint8_t)i; }
    for (i = 0; i < CAP; i++) stack_in[i] = (uint16_t)(0x100 + i);
    cur[10] = 0xEE; cur[11] = 0xEF; cur[200] = 0x77;            /* two delta runs */

    /* round trip at exactly the stack capacity */
    n = delta_encode(blob, sizeof blob, cur, orig, DYN, 0x1234, stack_in, CAP, 7);
    CHECK(n == hs + stack_bytes + (3 + 2) + (3 + 1));
    arm(&o);
    CHECK(decode(blob, n, orig, &o) == 0);
    CHECK(memcmp(o.ram, cur, DYN) == 0);
    CHECK(o.pc == 0x1234 && o.sw == CAP && o.bp == 7);
    CHECK(memcmp(o.stack, stack_in, sizeof stack_in) == 0);

    /* the same save at an odd address: the header must not be read through a
     * typed pointer (UBSan flags a misaligned uint32_t load in test_save_restore_san) */
    {
        uint8_t *odd = malloc(n + 1);
        arm(&o);
        memcpy(odd + 1, blob, n);
        CHECK(delta_decode(odd + 1, n, o.ram, orig, DYN, &o.pc, o.stack, CAP, &o.sw, &o.bp) == 0);
        CHECK(memcmp(o.ram, cur, DYN) == 0 && o.pc == 0x1234 && o.sw == CAP && o.bp == 7);
        free(odd);
    }

    /* empty stack and no deltas */
    n = delta_encode(blob, sizeof blob, orig, orig, DYN, 0x10, stack_in, 0, 1);
    CHECK(n == hs);
    arm(&o);
    CHECK(decode(blob, n, orig, &o) == 0);
    CHECK(memcmp(o.ram, orig, DYN) == 0 && o.sw == 0 && o.pc == 0x10);

    /* a zero-length delta entry is accepted and changes nothing (policy: harmless) */
    {
        delta_header_t z;
        uint8_t zb[64];
        memset(&z, 0, sizeof z);
        z.magic = DELTA_MAGIC; z.pc = 0x20; z.stack_words = 0; z.delta_count = 1; z.base_ptr = 2;
        put_hdr(zb, &z);
        zb[hs] = 0x00; zb[hs + 1] = 0x10; zb[hs + 2] = 0x00;     /* offset 16, length 0 */
        arm(&o);
        CHECK(decode(zb, hs + 3, orig, &o) == 0);
        CHECK(memcmp(o.ram, orig, DYN) == 0);
    }

    /* ---- rejections: every one must leave every output untouched ---- */
    n = delta_encode(blob, sizeof blob, cur, orig, DYN, 0x1234, stack_in, CAP, 7);
    get_hdr(blob, &h);

    expect_reject("input shorter than a header", blob, hs - 1, orig, -1);
    expect_reject("empty input", blob, 0, orig, -1);

    h.magic = 0; put_hdr(blob, &h);
    expect_reject("bad magic", blob, n, orig, -2);
    h.magic = DELTA_MAGIC; put_hdr(blob, &h);

    h.stack_words = CAP + 1; put_hdr(blob, &h);                  /* a save from a bigger-stack build */
    expect_reject("stack larger than capacity", blob, sizeof blob, orig, -7);
    h.stack_words = 0xFFFF; put_hdr(blob, &h);
    expect_reject("stack count 65535", blob, sizeof blob, orig, -7);
    h.stack_words = CAP; put_hdr(blob, &h);

    expect_reject("stack cut short", blob, hs + stack_bytes - 2, orig, -3);
    expect_reject("entry header cut short", blob, hs + stack_bytes + 2, orig, -4);
    expect_reject("entry data cut short", blob, hs + stack_bytes + 3 + 1, orig, -5);
    expect_reject("last entry cut short", blob, n - 1, orig, -5);

    h.delta_count = 5000; put_hdr(blob, &h);                     /* claims far more entries than present */
    expect_reject("excessive delta count", blob, n, orig, 0);
    h.delta_count = 2; put_hdr(blob, &h);

    {
        uint8_t *e = blob + hs + stack_bytes;                    /* first entry: offset 10, length 2 */
        uint8_t keep0 = e[0], keep1 = e[1];
        e[0] = 0x00; e[1] = 0xFF;                                /* offset 255 + length 2 > 256 */
        expect_reject("delta past end of dynamic memory", blob, n, orig, -6);
        e[0] = keep0; e[1] = keep1;
    }

    /* the repaired blob still decodes: the cases above did not depend on a corrupted fixture */
    arm(&o);
    CHECK(decode(blob, n, orig, &o) == 0);
    CHECK(memcmp(o.ram, cur, DYN) == 0);
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);                         /* show progress even if a test crashes */
    printf("save/restore: ZVIBE_ENABLE_V4=%d stack words=%d\n", ZVIBE_ENABLE_V4, ZVIBE_STACK_WORDS);
    test_raw_restore();
    test_delta_restore();
    printf("\n%d/%d checks passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
