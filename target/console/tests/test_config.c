/* Copyright (c) 2025 Martin R. Raumann */
/* SPDX-License-Identifier: BSD-3-Clause */
/**
 * test_config.c - Tests for the V3/V4 build configuration (zvibe_config.h)
 *
 * Built several times by the Makefile (V3-only, V3+V4, and a larger stack),
 * each with -DEXPECT_V4 and -DEXPECT_STACK_WORDS stating what that binary must
 * be, so the test does not just agree with whatever the macros say.
 *
 * Checks that the capacity constants follow the switch; that Z_IS_V4() is a
 * constant 0 in a V3-only build and follows the story header in a dual build;
 * that story loading enforces size limits and keeps the loaded machine intact
 * when an oversize story is rejected; that V4 stories are still refused (the
 * core cannot run them yet: when V4 loading is enabled, update the "V4
 * refused" checks); and that the stack setting does not shrink the dynamic
 * memory a story may have.
 */

#include <stdio.h>
#include "test_story.h"

#if !defined(EXPECT_V4) || !defined(EXPECT_STACK_WORDS)
#error "build with -DEXPECT_V4=0|1 -DEXPECT_STACK_WORDS=<n>"
#endif
#if ZVIBE_ENABLE_V4 != EXPECT_V4
#error "this binary was built with the wrong ZVIBE_ENABLE_V4"
#endif
#if ZVIBE_STACK_WORDS != EXPECT_STACK_WORDS
#error "this binary was built with the wrong ZVIBE_STACK_WORDS"
#endif

/* ---- Compile-time checks (negative array size = build error) ---- */

#define STATIC_CHECK(name, cond) typedef char static_check_##name[(cond) ? 1 : -1]

#if ZVIBE_ENABLE_V4
STATIC_CHECK(max_operands, Z_MAX_OPERANDS == 8);
STATIC_CHECK(story_mem, Z_STORY_MEM_SIZE == 256 * 1024);
#else
STATIC_CHECK(max_operands, Z_MAX_OPERANDS == 4);
STATIC_CHECK(story_mem, Z_STORY_MEM_SIZE == 128 * 1024);
/* In a V3-only build the V4 test is a literal 0, usable in constant expressions */
STATIC_CHECK(is_v4_constant, Z_IS_V4() == 0);
#endif
STATIC_CHECK(operands_array,
             sizeof(((zState *)0)->operands) / sizeof(zWord) == Z_MAX_OPERANDS);
STATIC_CHECK(stack_array,
             sizeof(((zState *)0)->stack_mem) / sizeof(zWord) == ZVIBE_STACK_WORDS);

/* ---- Tests ---- */

static zvibeResult try_load_static(uint8_t version, size_t size, unsigned static_base) {
    zvibeContext *ctx = zvibe_create(sink);
    zvibeResult r;
    make_story_static(version, size, static_base);
    if (!ctx) return ZVIBE_ERROR;
    r = zvibe_load_story_from_memory(ctx, story, size);
    zvibe_destroy(ctx);
    return r;
}

static zvibeResult try_load(uint8_t version, size_t size) {
    return try_load_static(version, size, 0x0400);
}

static void test_loading(void) {
    puts("== story loading ==");
    CHECK(try_load(3, 4096) == ZVIBE_OK);                       /* ordinary V3 story */
    CHECK(try_load(3, 128 * 1024) == ZVIBE_OK);                 /* V3 maximum */
    CHECK(try_load(3, 128 * 1024 + 1) == ZVIBE_ERROR);          /* one byte over, both builds */
    CHECK(try_load(3, 200 * 1024) == ZVIBE_ERROR);              /* a dual build must not accept a V4-sized V3 story */
    CHECK(try_load(3, Z4_STORY_LIMIT + 1) == ZVIBE_ERROR);      /* over everything */
    CHECK(try_load(4, 4096) == ZVIBE_ERROR);                    /* V4 refused: core cannot run it yet */
    CHECK(try_load(2, 4096) == ZVIBE_ERROR);
    CHECK(try_load(5, 4096) == ZVIBE_ERROR);
    CHECK(try_load(0, 4096) == ZVIBE_ERROR);
    CHECK(try_load(3, 4096) == ZVIBE_OK);                       /* a fresh context still works afterwards */
}

static void test_bad_arguments(void) {
    zvibeContext *ctx = zvibe_create(sink);
    puts("== bad arguments and tiny sizes ==");
    CHECK(ctx != NULL);
    make_story(3, 4096);
    CHECK(zvibe_load_story_from_memory(ctx, NULL, 4096) == ZVIBE_ERROR);
    CHECK(zvibe_load_story_from_memory(ctx, story, 0) == ZVIBE_ERROR);
    CHECK(zvibe_load_story_from_memory(NULL, story, 4096) == ZVIBE_ERROR);
    CHECK(try_load(3, 1) == ZVIBE_ERROR);
    CHECK(try_load(3, 63) == ZVIBE_ERROR);                      /* smaller than a header */
    CHECK(try_load(3, 64) == ZVIBE_ERROR);                      /* header only: addresses point past the end */
    zvibe_destroy(ctx);
}

/* A rejected oversize load must leave the loaded machine exactly as it was.
 * In a dual build the oversize V3 story passes the first (V4-sized) limit, so
 * this is the case where the per-version check runs; it must not run after
 * state has been reset. */
static void test_rejected_load_keeps_state(void) {
    zvibeContext *ctx = zvibe_create(sink);
    zDWord pc;
    zSize size, ram;
    puts("== rejected oversize load keeps loaded state ==");
    make_story(3, 4096);
    CHECK(ctx != NULL);
    CHECK(zvibe_load_story_from_memory(ctx, story, 4096) == ZVIBE_OK);
    pc = G->logical_pc;
    size = G->story_size;
    ram = G->memory_state.ram_size;
    CHECK(pc == 0x0100);

    make_story(3, 200 * 1024);
    CHECK(zvibe_load_story_from_memory(ctx, story, 200 * 1024) == ZVIBE_ERROR);
    CHECK(G->logical_pc == pc);
    CHECK(G->story_size == size);
    CHECK(G->memory_state.ram_size == ram);

    make_story(3, 4096);                                        /* and the same context can load again */
    CHECK(zvibe_load_story_from_memory(ctx, story, 4096) == ZVIBE_OK);
    CHECK(G->logical_pc == 0x0100);
    zvibe_destroy(ctx);
}

/* The load check keeps a fixed 1KB reserve in the 16KB buffer (historical
 * behavior). It must not follow ZVIBE_STACK_WORDS: the stack is separate. */
static void test_dynamic_memory_limit_ignores_stack_setting(void) {
    puts("== dynamic memory limit does not depend on stack size ==");
    CHECK(try_load_static(3, 20 * 1024, 15 * 1024) == ZVIBE_OK);       /* 15KB + 1KB reserve fits 16KB exactly */
    CHECK(try_load_static(3, 20 * 1024, 15 * 1024 + 1) == ZVIBE_ERROR); /* one byte more does not */
}

static void test_version_predicate(void) {
    zvibeContext *ctx;
    puts("== Z_IS_V4 / Z_VSEL ==");
    make_story(3, 4096);
    ctx = zvibe_create(sink);
    CHECK(ctx != NULL);
    CHECK(zvibe_load_story_from_memory(ctx, story, 4096) == ZVIBE_OK);
    CHECK(Z_IS_V4() == 0);
    CHECK(Z_VSEL(31, 63) == 31);                                /* V3 value selected */
#if ZVIBE_ENABLE_V4
    /* A dual build reads the version from the loaded header */
    G->header.version = 4;
    CHECK(Z_IS_V4() == 1);
    CHECK(Z_VSEL(31, 63) == 63);
    G->header.version = 3;
    CHECK(Z_IS_V4() == 0);
#else
    /* A V3-only build ignores the header: the predicate is always 0 */
    G->header.version = 4;
    CHECK(Z_IS_V4() == 0);
    CHECK(Z_VSEL(31, 63) == 31);
    G->header.version = 3;
#endif
    zvibe_destroy(ctx);
}

int main(void) {
    printf("config: ZVIBE_ENABLE_V4=%d operands=%d story limit=%d stack words=%d\n",
           ZVIBE_ENABLE_V4, Z_MAX_OPERANDS, (int)Z_STORY_MEM_SIZE, ZVIBE_STACK_WORDS);
    test_loading();
    test_bad_arguments();
    test_rejected_load_keeps_state();
    test_dynamic_memory_limit_ignores_stack_setting();
    test_version_predicate();
    printf("\n%d/%d checks passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
