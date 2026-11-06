/* Copyright (c) 2025 Martin R. Raumann */
/* SPDX-License-Identifier: BSD-3-Clause */
/**
 * test_opcodes.c - Loading, header setup, instruction decoding, call forms and
 *                  save/restore results, V3 and V4
 *
 * Programs are assembled by hand from the Z-machine standard and run through the
 * public API (zvibe_run), so the instruction decoder is exercised end to end.
 * Expected values are worked out from the standard, not read back from the
 * interpreter.
 *
 * Built twice: V3-only, and V3+V4. The V3 cases run in both builds; the V4 cases
 * load real version-4 stories.
 *
 * Memory map used by the fixtures (all dynamic memory):
 *   0x0100  main program        0x0200  globals (global 0x10 = bytes 0x200,0x201)
 *   0x0080  text buffer         0x2000+ routines
 */

#include <stdio.h>
#include <stdlib.h>
#include "test_story.h"

#ifndef EXPECT_V4
#error "build with -DEXPECT_V4=0|1"
#endif
#if ZVIBE_ENABLE_V4 != EXPECT_V4
#error "this binary was built with the wrong ZVIBE_ENABLE_V4"
#endif

#define STATIC_BASE 0x3C00u
#define STORY_SIZE  (32 * 1024)
#define GLOBAL10    0x0200u           /* global variable 0x10 */
#define GLOBAL11    0x0202u           /* global variable 0x11 */

static zvibeContext *ctx;
static uint8_t *ram;

/* Put raw bytes into the (not yet loaded) story image */
static void put(unsigned at, const uint8_t *bytes, unsigned n) { memcpy(story + at, bytes, n); }
#define PUT(at, ...) do { static const uint8_t b_[] = { __VA_ARGS__ }; put((at), b_, sizeof b_); } while (0)

static unsigned global10(void) { return ((unsigned)ram[GLOBAL10] << 8) | ram[GLOBAL10 + 1]; }
static unsigned global11(void) { return ((unsigned)ram[GLOBAL11] << 8) | ram[GLOBAL11 + 1]; }

/* Create a story of the given version, with the caller's code already placed in story[] */
static void begin(int version) {
    make_story_static((uint8_t)version, STORY_SIZE, STATIC_BASE);
}

static zvibeResult finish_load(void) {
    ctx = zvibe_create(sink);
    if (!ctx) return ZVIBE_ERROR;
    if (zvibe_load_story_from_memory(ctx, story, STORY_SIZE) != ZVIBE_OK) { zvibe_destroy(ctx); ctx = NULL; return ZVIBE_ERROR; }
    ram = G->memory_state.ram_buffer;
    return ZVIBE_OK;
}

static void end(void) { if (ctx) zvibe_destroy(ctx); ctx = NULL; }

/* ---- loading and header setup ---- */

static void test_loading(void) {
    puts("== version acceptance ==");
    begin(3);  CHECK(finish_load() == ZVIBE_OK); end();
    begin(4);  CHECK((finish_load() == ZVIBE_OK) == (ZVIBE_ENABLE_V4 != 0)); end();   /* V4 only when compiled in */
    begin(2);  CHECK(finish_load() != ZVIBE_OK); end();
    begin(5);  CHECK(finish_load() != ZVIBE_OK); end();
    begin(0);  CHECK(finish_load() != ZVIBE_OK); end();
    {   /* dynamic memory must hold the 64-byte header the interpreter writes into: refused in V4,
         * and V3 loading is left exactly as it was */
        zvibeContext *c = zvibe_create(sink);
        make_story_static(3, STORY_SIZE, 0x20);
        CHECK(zvibe_load_story_from_memory(c, story, STORY_SIZE) == ZVIBE_OK);
#if ZVIBE_ENABLE_V4
        make_story_static(4, STORY_SIZE, 0x20);
        CHECK(zvibe_load_story_from_memory(c, story, STORY_SIZE) != ZVIBE_OK);
        make_story_static(4, STORY_SIZE, 0x40);
        CHECK(zvibe_load_story_from_memory(c, story, STORY_SIZE) == ZVIBE_OK);   /* exactly the header: fine */
#endif
        zvibe_destroy(c);
    }
#if ZVIBE_ENABLE_V4
    {   /* story size limit follows the version: V4 up to 256 KB, V3 up to 128 KB */
        zvibeContext *c = zvibe_create(sink);
        make_story_static(4, Z4_STORY_LIMIT, STATIC_BASE);
        CHECK(zvibe_load_story_from_memory(c, story, Z4_STORY_LIMIT) == ZVIBE_OK);
        make_story_static(4, Z4_STORY_LIMIT + 1, STATIC_BASE);
        CHECK(zvibe_load_story_from_memory(c, story, Z4_STORY_LIMIT + 1) != ZVIBE_OK);
        make_story_static(3, Z3_STORY_LIMIT + 1, STATIC_BASE);
        CHECK(zvibe_load_story_from_memory(c, story, Z3_STORY_LIMIT + 1) != ZVIBE_OK);
        zvibe_destroy(c);
    }
#endif
}

static void test_header(void) {
    puts("== header setup ==");
    begin(3);
    story[1] = 0x00;
    CHECK(finish_load() == ZVIBE_OK);
    CHECK(ram[1] == 0x10);                                  /* V3: "status line not available" */
    CHECK(ram[0x1E] == 0 && ram[0x20] == 0);                /* a V3 header is not touched elsewhere */
    ram[1] = 0x00;                                          /* a restart redoes the V3 setup too */
    G->restart_requested = 1;
    CHECK(zvibe_restart_completed(ctx) == ZVIBE_OK);
    CHECK(ram[1] == 0x10);
    end();
#if ZVIBE_ENABLE_V4
    begin(4);
    story[1] = 0xFF;                                        /* the story claims every capability bit */
    CHECK(finish_load() == ZVIBE_OK);
    CHECK(ram[1] == 0x63);                                  /* bits 2,3,4,7 (bold, italic, fixed-space, timers) cleared */
    CHECK(ram[0x1E] == 1);                                  /* interpreter number */
    CHECK(ram[0x1F] == 'Z');
    CHECK(ram[0x20] == 24 && ram[0x21] == 80);              /* screen rows, columns */
    CHECK(ram[0x32] == 1 && ram[0x33] == 1);                /* standard 1.1: the V4 opcode set is complete */

    /* a restart restores dynamic memory (which holds the header) and must redo the setup */
    memset(ram + 0x1E, 0xEE, 4);
    ram[1] = 0xFF;
    ram[0x11] = 0xA2;                                       /* the story asked for fixed pitch; other bits are its own */
    G->restart_requested = 1;
    CHECK(zvibe_restart_completed(ctx) == ZVIBE_OK);
    CHECK(ram[1] == 0x63 && ram[0x1E] == 1 && ram[0x20] == 24 && ram[0x21] == 80);
    CHECK((ram[0x11] & 3) == 2 && (ram[0x11] & ~3) == (story[0x11] & ~3));   /* flags 2 bits 0-1 survive, the rest is the story's */
    end();
#endif
}

/* ---- running programs ---- */

/* Run until the story finishes; returns the last result (GAME_FINISHED on success) */
static zvibeResult run_to_end(void) { return zvibe_run(ctx); }

/* A routine with `locals` locals whose body is "ret L<which>", at `at`. Its initial
 * local values (big-endian words, as in a V3/V4 routine header) are given. */
static void routine_ret_local(unsigned at, unsigned locals, const unsigned *init, unsigned which) {
    unsigned i, p = at;
    story[p++] = (uint8_t)locals;
    for (i = 0; i < locals; i++) { story[p++] = (uint8_t)(init ? init[i] >> 8 : 0); story[p++] = (uint8_t)(init ? init[i] : 0); }
    story[p++] = 0xAB;                                      /* ret, one operand of type variable */
    story[p++] = (uint8_t)which;                            /* ... the local variable `which` */
}

static void test_call_v3_form(int version) {
    /* call (VAR:0 = 0xE0, a "call_vs" in V4): operand types large, small, then omitted;
     * the routine at 0x2200 returns its first local; the result goes to global 0x10. */
    unsigned packed = (version == 3) ? 0x1100 : 0x0880;      /* 0x2200 = packed * 2 (V3) or * 4 (V4) */
    unsigned init[1] = { 0x0123 };
    printf("== call (VAR:0), V%d ==\n", version);
    begin(version);
    routine_ret_local(0x2200, 1, init, 1);
    PUT(0x100, 0xE0, 0x1F, 0, 0, 0x55, 0x10, 0xBA);          /* call R, 0x55 -> g10 ; quit */
    story[0x102] = (uint8_t)(packed >> 8); story[0x103] = (uint8_t)packed;
    CHECK(finish_load() == ZVIBE_OK);
    CHECK(run_to_end() == ZVIBE_GAME_FINISHED);
    CHECK(global10() == 0x55);                               /* the argument replaced the default */
    end();

    begin(version);
    routine_ret_local(0x2200, 1, init, 1);
    PUT(0x100, 0xE0, 0x3F, 0, 0, 0x10, 0xBA);                /* call R -> g10 ; quit: one operand, no arguments */
    story[0x102] = (uint8_t)(packed >> 8); story[0x103] = (uint8_t)packed;
    CHECK(finish_load() == ZVIBE_OK);
    CHECK(run_to_end() == ZVIBE_GAME_FINISHED);
    CHECK(global10() == 0x0123);                             /* no argument: the header default */
    end();

    begin(version);
    PUT(0x100, 0xE0, 0x3F, 0, 0, 0x10, 0xBA);                /* call 0 -> g10: calling address 0 stores 0 */
    CHECK(finish_load() == ZVIBE_OK);
    ram[GLOBAL10] = 0xFF; ram[GLOBAL10 + 1] = 0xFF;
    CHECK(run_to_end() == ZVIBE_GAME_FINISHED);
    CHECK(global10() == 0);
    end();
}

#if ZVIBE_ENABLE_V4
static void test_call_v4_forms(void) {
    unsigned init7[7] = {0};
    unsigned init1[1] = { 0x0123 };
    puts("== call_1s, call_2s, call_vs2 (V4) ==");

    /* call_1s (1OP:8, opcode 0x88 for a large-constant operand): routine 0x2200 returns its default local */
    begin(4);
    routine_ret_local(0x2200, 1, init1, 1);
    PUT(0x100, 0x88, 0x08, 0x80, 0x10, 0xBA);                /* call_1s 0x0880 -> g10 ; quit */
    CHECK(finish_load() == ZVIBE_OK);
    CHECK(run_to_end() == ZVIBE_GAME_FINISHED);
    CHECK(global10() == 0x0123);
    end();

    /* call_2s (2OP:25 in variable form = 0xD9): one argument replaces the default */
    begin(4);
    routine_ret_local(0x2200, 1, init1, 1);
    PUT(0x100, 0xD9, 0x1F, 0x08, 0x80, 0x55, 0x10, 0xBA);    /* call_2s 0x0880, 0x55 -> g10 ; quit */
    CHECK(finish_load() == ZVIBE_OK);
    CHECK(run_to_end() == ZVIBE_GAME_FINISHED);
    CHECK(global10() == 0x55);
    end();

    /* call_vs2 (VAR:12 = 0xEC): the routine plus SEVEN arguments, with two operand-type bytes.
     * Types byte 1: large, small, small, small = 00 01 01 01 = 0x15; byte 2: four smalls = 0x55.
     * The routine has seven locals and returns the last one. */
    begin(4);
    routine_ret_local(0x2000, 7, init7, 7);
    PUT(0x100, 0xEC, 0x15, 0x55, 0x08, 0x00, 11, 22, 33, 44, 55, 66, 77, 0x10, 0xBA);
    CHECK(finish_load() == ZVIBE_OK);
    CHECK(run_to_end() == ZVIBE_GAME_FINISHED);
    CHECK(global10() == 77);
    end();

    /* the same, returning the first and the fourth local: arguments land in order */
    begin(4);
    routine_ret_local(0x2000, 7, init7, 4);
    PUT(0x100, 0xEC, 0x15, 0x55, 0x08, 0x00, 11, 22, 33, 44, 55, 66, 77, 0x10, 0xBA);
    CHECK(finish_load() == ZVIBE_OK);
    CHECK(run_to_end() == ZVIBE_GAME_FINISHED);
    CHECK(global10() == 44);
    end();

    /* operand TYPES differ across the two bytes, so a decoder that picks the wrong byte for
     * any operand is caught. Types: byte 1 = large, small, large, variable = 00 01 00 10 = 0x12;
     * byte 2 = small, large, variable, omitted = 01 00 10 11 = 0x4B. Arguments: 11, 0x0016 (22),
     * variable 0x10 (global g10, preset to 33), 44, 0x0037 (55), variable 0x10 (33). */
    {
        static const unsigned want_local[] = { 1, 2, 3, 4, 5, 6 };
        static const unsigned want_value[] = { 11, 22, 33, 44, 55, 33 };
        unsigned i;
        for (i = 0; i < sizeof want_local / sizeof want_local[0]; i++) {
            begin(4);
            routine_ret_local(0x2000, 7, init7, want_local[i]);
            PUT(0x100, 0xEC, 0x12, 0x4B, 0x08, 0x00, 11, 0x00, 0x16, 0x10, 44, 0x00, 0x37, 0x10, 0x10, 0xBA);
            CHECK(finish_load() == ZVIBE_OK);
            ram[GLOBAL10] = 0; ram[GLOBAL10 + 1] = 33;
            CHECK(run_to_end() == ZVIBE_GAME_FINISHED);
            CHECK(global10() == want_value[i]);
            end();
        }
    }

    /* stack-variable operands (variable 0) on both sides of the type-byte boundary: each one pops
     * the stack, in operand order, so the first gets the top value. push 100; push 200 leaves 200 on
     * top. Types: byte 1 = large, small, small, variable = 00 01 01 10 = 0x16; byte 2 = variable,
     * omitted... = 10 11 11 11 = 0xBF. Operand 3 is the first pop (200); operand 4 the second (100). */
    {
        static const unsigned local_of[] = { 3, 4 };
        static const unsigned value_of[] = { 200, 100 };
        unsigned i;
        for (i = 0; i < 2; i++) {
            begin(4);
            routine_ret_local(0x2000, 7, init7, local_of[i]);
            PUT(0x100, 0xE8, 0x7F, 100,  0xE8, 0x7F, 200,                 /* push 100 ; push 200 */
                       0xEC, 0x16, 0xBF, 0x08, 0x00, 11, 22, 0x00, 0x00,  /* call_vs2 R, 11, 22, pop, pop */
                       0x10, 0xBA);                                       /* -> g10 ; quit */
            CHECK(finish_load() == ZVIBE_OK);
            CHECK(run_to_end() == ZVIBE_GAME_FINISHED);
            CHECK(global10() == value_of[i]);
            CHECK(G->stack_ptr == G->stack_mem);                         /* both values were popped */
            end();
        }
    }

    /* the second type byte is ALWAYS read, even when the first already ends in "omitted":
     * types 00 01 11 11 = 0x1F then 0xFF; if 0xFF were not consumed the following bytes
     * would be decoded wrongly and the quit would never be reached. */
    begin(4);
    routine_ret_local(0x2200, 1, init1, 1);
    PUT(0x100, 0xEC, 0x1F, 0xFF, 0x08, 0x80, 0x66, 0x10, 0xBA);   /* call_vs2 R, 0x66 -> g10 ; quit */
    CHECK(finish_load() == ZVIBE_OK);
    CHECK(run_to_end() == ZVIBE_GAME_FINISHED);
    CHECK(global10() == 0x66);
    end();

    /* only the routine, no arguments, both type bytes mostly omitted */
    begin(4);
    routine_ret_local(0x2200, 1, init1, 1);
    PUT(0x100, 0xEC, 0x3F, 0xFF, 0x08, 0x80, 0x10, 0xBA);
    CHECK(finish_load() == ZVIBE_OK);
    CHECK(run_to_end() == ZVIBE_GAME_FINISHED);
    CHECK(global10() == 0x0123);
    end();
}
#endif

/* ---- save and restore ---- */

/* Program (both versions): push 7 ; push 9 ; save ; restore ; quit, so the stack is NOT empty at the
 * save. V3 uses "branch on FALSE" so success falls through; V4 stores the save result in global 0x10
 * and the restore result in global 0x11 (different variables, so a result stored in the wrong one is
 * noticed).
 *   0x100 E8 7F 07   push 7        0x103 E8 7F 09   push 9
 *   0x106 B5 ..      save          0x108 B6 ..      restore        0x10A BA quit */
static void load_save_program(int version) {
    begin(version);
    PUT(0x100, 0xE8, 0x7F, 7, 0xE8, 0x7F, 9);
    if (version == 3) { PUT(0x106, 0xB5, 0x44, 0xB6, 0x44, 0xBA, 0xBA, 0xBA, 0xBA); }
    else              { PUT(0x106, 0xB5, 0x10, 0xB6, 0x11, 0xBA); }
}

static unsigned stack_word(unsigned i) { return z_read16_be_impl((const zByte *)&G->stack_mem[i]); }

/* Run to the save, capture the image, return it malloc'd. Leaves the context at the save request. */
static uint8_t *run_to_save(int version, size_t *size) {
    uint8_t *blob;
    load_save_program(version);
    if (finish_load() != ZVIBE_OK) return NULL;
    if (zvibe_run(ctx) != ZVIBE_SAVE_REQUESTED) return NULL;
    *size = zvibe_get_save_size(ctx);
    blob = malloc(*size);
    if (zvibe_get_save_data(ctx, (char *)blob, *size) != *size) { free(blob); return NULL; }
    return blob;
}

static void test_save_restore(int version) {
    uint8_t *blob, *odd;
    size_t size;
    printf("== save / restore results, V%d ==\n", version);

    /* save succeeds */
    blob = run_to_save(version, &size);
    CHECK(blob != NULL);
    CHECK(size >= STATIC_BASE + 8 + 2 * sizeof(zWord));                       /* the two pushed words are in the image */
    CHECK(zvibe_save_completed(ctx, 1) == ZVIBE_OK);
    if (version == 3) CHECK(G->logical_pc == 0x108);                          /* success: no branch, on to the restore */
    else              CHECK(global10() == 1 && global11() == 0);              /* 1 into the save's variable only */
    CHECK(G->stack_ptr - G->stack_mem == 2);                                  /* the stack was not disturbed */
    CHECK(stack_word(0) == 7 && stack_word(1) == 9);

    /* the restore instruction: the frontend loads the snapshot into a SHALLOWER current stack */
    CHECK(zvibe_run(ctx) == ZVIBE_RESTORE_REQUESTED);
    G->stack_ptr = G->stack_mem;                                              /* current stack: empty */
    odd = malloc(size);
    memcpy(odd, blob, size);
    ram[0x11] = (uint8_t)((ram[0x11] & ~3) | 2);                              /* live: fixed pitch on, transcript off */
    odd[0x11] = (uint8_t)((odd[0x11] & ~3) | 1);                              /* saved: the other way round */
    if (version == 4) { odd[0x1E] = 77; odd[0x20] = 99; odd[0x21] = 99; }     /* saved by an interpreter with other settings */
    else              { odd[1] = 0x00; }                                      /* saved flags1 without the status-line bit */
    CHECK(zvibe_restore_data(ctx, (const char *)odd, size) == ZVIBE_OK);
    free(odd);
    CHECK(G->stack_ptr - G->stack_mem == 2);                                  /* the DEEPER saved stack is back */
    CHECK(stack_word(0) == 7 && stack_word(1) == 9);
    if (version == 4) CHECK(ram[0x1E] == 1 && ram[0x20] == 24 && ram[0x21] == 80);   /* header describes THIS interpreter */
    else              CHECK(ram[1] == 0x10);
    if (version == 4) CHECK((ram[0x11] & 3) == 2);                            /* live flags 2 bits win over the saved ones */
    else              CHECK((ram[0x11] & 3) == 1);                            /* V3 behaviour unchanged */
    CHECK(zvibe_restore_completed(ctx, 1) == ZVIBE_OK);
    if (version == 3) CHECK(G->logical_pc == 0x108);                          /* at the save, which now "succeeded" */
    else              CHECK(global10() == 2 && global11() == 0);              /* the game sees "restored" in the SAVE's variable */
    CHECK(G->stack_ptr - G->stack_mem == 2);

    /* the restore instruction runs again; this time it fails */
    CHECK(zvibe_run(ctx) == ZVIBE_RESTORE_REQUESTED);
    if (version == 4) { ram[GLOBAL11] = 0xFF; ram[GLOBAL11 + 1] = 0xFF; }
    CHECK(zvibe_restore_completed(ctx, 0) == ZVIBE_OK);
    if (version == 3) CHECK(G->logical_pc == 0x10C);                          /* failure: the branch is taken */
    else              CHECK(global11() == 0 && global10() == 2);              /* 0 into the RESTORE's variable only */
    free(blob);
    end();

    /* save fails */
    blob = run_to_save(version, &size);
    CHECK(blob != NULL);
    if (version == 4) { ram[GLOBAL10] = 0xFF; ram[GLOBAL10 + 1] = 0xFF; }
    CHECK(zvibe_save_completed(ctx, 0) == ZVIBE_OK);
    if (version == 3) CHECK(G->logical_pc == 0x10A);                          /* failure: the branch is taken */
    else              CHECK(global10() == 0);
    free(blob);
    end();
}

/* A save belongs to one story: another version, or another release of the same version, is refused
 * and leaves the running machine exactly as it was. */
static void restore_must_fail(const uint8_t *blob, size_t size, const char *why) {
    static uint8_t before[STATIC_BASE];
    unsigned pc, depth;
    memcpy(before, ram, STATIC_BASE);
    pc = (unsigned)G->logical_pc;
    depth = (unsigned)(G->stack_ptr - G->stack_mem);
    if (zvibe_restore_data(ctx, (const char *)blob, size) == ZVIBE_OK) printf("  accepted: %s\n", why);
    CHECK(zvibe_restore_data(ctx, (const char *)blob, size) != ZVIBE_OK);
    CHECK(memcmp(before, ram, STATIC_BASE) == 0);
    CHECK((unsigned)G->logical_pc == pc && (unsigned)(G->stack_ptr - G->stack_mem) == depth);
}

static void test_restore_identity(void) {
    uint8_t *blob;
    size_t size;
    printf("== a save only restores into the story it came from ==\n");

    /* same version, different release, serial or checksum */
    begin(3);
    PUT(0x100, 0xE8, 0x7F, 7, 0xE8, 0x7F, 9, 0xB5, 0x44, 0xB6, 0x44, 0xBA);
    story[2] = 0; story[3] = 1;                       /* release 1 */
    CHECK(finish_load() == ZVIBE_OK);
    CHECK(zvibe_run(ctx) == ZVIBE_SAVE_REQUESTED);
    size = zvibe_get_save_size(ctx);
    blob = malloc(size);
    CHECK(zvibe_get_save_data(ctx, (char *)blob, size) == size);
    CHECK(zvibe_restore_data(ctx, (const char *)blob, size) == ZVIBE_OK);       /* its own story: fine */
    end();

    begin(3);
    PUT(0x100, 0xE8, 0x7F, 7, 0xE8, 0x7F, 9, 0xB5, 0x44, 0xB6, 0x44, 0xBA);
    story[2] = 0; story[3] = 2;                       /* release 2: a different story */
    CHECK(finish_load() == ZVIBE_OK);
    restore_must_fail(blob, size, "other release");
    end();
    begin(3);
    PUT(0x100, 0xE8, 0x7F, 7, 0xE8, 0x7F, 9, 0xB5, 0x44, 0xB6, 0x44, 0xBA);
    story[0x12] = '9';                                 /* same release, different serial number */
    story[2] = 0; story[3] = 1;
    CHECK(finish_load() == ZVIBE_OK);
    restore_must_fail(blob, size, "other serial");
    end();
    begin(3);
    PUT(0x100, 0xE8, 0x7F, 7, 0xE8, 0x7F, 9, 0xB5, 0x44, 0xB6, 0x44, 0xBA);
    story[0x1C] = 0x12;                                /* same release and serial, different checksum */
    story[2] = 0; story[3] = 1;
    CHECK(finish_load() == ZVIBE_OK);
    restore_must_fail(blob, size, "other checksum");
    end();
    free(blob);

#if ZVIBE_ENABLE_V4
    {   /* across versions, both directions */
        uint8_t *v3, *v4;
        size_t s3, s4;
        v3 = run_to_save(3, &s3);  CHECK(v3 != NULL); end();
        v4 = run_to_save(4, &s4);  CHECK(v4 != NULL); end();

        load_save_program(4);
        CHECK(finish_load() == ZVIBE_OK);
        restore_must_fail(v3, s3, "V3 save into a V4 story");
        end();

        load_save_program(3);
        CHECK(finish_load() == ZVIBE_OK);
        restore_must_fail(v4, s4, "V4 save into a V3 story");
        end();
        free(v3); free(v4);
    }
#endif
}

/* ---- the status line belongs to the host only for V3 ---- */

static int status_calls;
static void status_cb(const zvibeStatus *st) { (void)st; status_calls++; }

static void test_status_and_read(int version) {
    const zByte *t;
    printf("== status line and read, V%d ==\n", version);
    begin(version);
    /* an empty dictionary (no separators, entry length 4, no entries) so the parse buffer is real */
    story[8] = 0x03; story[9] = 0x00;
    PUT(0x300, 0x00, 0x04, 0x00, 0x00);
    if (version == 3) PUT(0x100, 0xE4, 0x5F, 0x80, 0xA0, 0xBA);                 /* read 0x80, 0xA0 */
    else              PUT(0x100, 0xE4, 0x55, 0x80, 0xA0, 0x05, 0x00, 0xBA);     /* read 0x80, 0xA0, time 5, routine 0 */
    story[0x80] = 20;                                                           /* text buffer: 20 characters */
    story[0xA0] = 5;                                                            /* parse buffer: 5 tokens */
    CHECK(finish_load() == ZVIBE_OK);
    status_calls = 0;
    CHECK(zvibe_set_status_callback(ctx, status_cb) == ZVIBE_OK);
    CHECK(zvibe_run(ctx) == ZVIBE_WAIT_FOR_INPUT);
    CHECK(status_calls == (version == 3 ? 1 : 0));                              /* a V4 story draws its own status line */
    CHECK(zvibe_input(ctx, "hi there") == ZVIBE_OK);                            /* V4 ignores the time and routine operands */
    CHECK(ram[0x81] == 'h' && ram[0x82] == 'i' && ram[0x89] == 0);              /* zero-terminated text in the buffer */
    t = &ram[0xA0];
    CHECK(t[1] == 2);                                                           /* two tokens... */
    CHECK(t[2] == 0 && t[3] == 0 && t[4] == 2 && t[5] == 1);                    /* ...unknown words: address 0, length 2, position 1 */
    CHECK(t[6] == 0 && t[7] == 0 && t[8] == 5 && t[9] == 4);                    /* ...and length 5, position 4 */
    CHECK(zvibe_run(ctx) == ZVIBE_GAME_FINISHED);
    end();

    /* show_status (0OP:12) is only meaningful in V3; in V4 it must do nothing and not fail */
    begin(version);
    PUT(0x100, 0xBC, 0xBA);
    CHECK(finish_load() == ZVIBE_OK);
    CHECK(run_to_end() == ZVIBE_GAME_FINISHED);
    end();
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);                         /* show progress even if a test crashes */
    printf("opcodes: ZVIBE_ENABLE_V4=%d\n", ZVIBE_ENABLE_V4);
    test_loading();
    test_header();
    test_call_v3_form(3);
    test_save_restore(3);
    test_restore_identity();
    test_status_and_read(3);
#if ZVIBE_ENABLE_V4
    test_call_v3_form(4);
    test_call_v4_forms();
    test_save_restore(4);
    test_status_and_read(4);
#endif
    printf("\n%d/%d checks passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
