/* Copyright (c) 2025 Martin R. Raumann */
/* SPDX-License-Identifier: BSD-3-Clause */
/**
 * test_objects.c - Object table, properties, packed addresses and verify, V3 and V4
 *
 * The object-table and property-header layouts used to BUILD the fixtures below
 * are written here straight from the Z-machine standard, independently of the
 * layout macros in src/core/zvibe_object.c, so a mistake in those macros cannot
 * make this test agree with itself.
 *
 * A story is loaded as V3 (the loader still refuses V4) and then
 * G->header.version is set to 4 for the V4 cases, which only exist in a build
 * with ZVIBE_ENABLE_V4=1. The V3 cases run in both builds: a dual build must
 * behave exactly like a V3-only build for a V3 story.
 *
 * Opcode handlers are called directly. The instruction stream is a few bytes
 * placed in dynamic memory: the store variable (0 = push on the stack) and, for
 * branching opcodes, a branch byte whose outcome is read back from the PC.
 */

#include <stdio.h>
#include <setjmp.h>
#include "test_story.h"

#ifndef EXPECT_V4
#error "build with -DEXPECT_V4=0|1"
#endif
#if ZVIBE_ENABLE_V4 != EXPECT_V4
#error "this binary was built with the wrong ZVIBE_ENABLE_V4"
#endif

/* ---- fixture layout, from the standard (NOT from zvibe_object.c) ---- */

typedef struct {
    int version;
    unsigned defaults;     /* property default words: 31 (V3), 63 (V4) */
    unsigned obj_size;     /* 9 or 14 */
    unsigned attr_bytes;   /* 4 or 6 */
    unsigned rel_width;    /* 1 or 2 */
    unsigned prop_field;   /* offset of the properties-address word: 7 or 12 */
} layout_t;

static const layout_t L3 = { 3, 31, 9, 4, 1, 7 };
#if ZVIBE_ENABLE_V4
static const layout_t L4 = { 4, 63, 14, 6, 2, 12 };
#endif

#define STATIC_BASE 0x3C00u         /* 15 KB of dynamic memory */
#define OBJTAB      0x0200u
#define PCAREA      0x01F0u         /* where the test instruction bytes live */
#define PTAB_BASE   0x2800u         /* property tables are laid out from here */

static uint8_t *ram;                /* the story's dynamic memory */
static const layout_t *cur;
static unsigned ptab_next;

static unsigned obj_addr(unsigned id) { return OBJTAB + cur->defaults * 2 + cur->obj_size * (id - 1); }
static unsigned rel_off(int which) { return cur->attr_bytes + which * cur->rel_width; }   /* 0 parent 1 sibling 2 child */

static void ram_put16(unsigned a, unsigned v) { ram[a] = (uint8_t)(v >> 8); ram[a + 1] = (uint8_t)v; }
static unsigned ram_get16(unsigned a) { return ((unsigned)ram[a] << 8) | ram[a + 1]; }

static unsigned get_rel(unsigned id, int which) {
    unsigned a = obj_addr(id) + rel_off(which);
    return cur->rel_width == 1 ? ram[a] : ram_get16(a);
}

/* Property header bytes for (number, size), from the standard. Returns header length. */
static unsigned put_prop_header(unsigned at, unsigned num, unsigned size) {
    if (cur->version == 3) {                         /* one byte: (size-1) in bits 7-5, number in bits 4-0 */
        ram[at] = (uint8_t)(((size - 1) << 5) | num);
        return 1;
    }
    if (size == 1)      { ram[at] = (uint8_t)num; return 1; }                 /* bit 7 = 0, bit 6 = 0 */
    if (size == 2)      { ram[at] = (uint8_t)(0x40 | num); return 1; }         /* bit 7 = 0, bit 6 = 1 */
    ram[at] = (uint8_t)(0x80 | num);                                           /* long form: bit 7 = 1 */
    /* second byte: bit 7 = 1, bit 6 undetermined (vary it), low 6 bits = size, 0 meaning 64 */
    ram[at + 1] = (uint8_t)(((num & 1) ? 0xC0 : 0x80) | (size == 64 ? 0 : size));
    return 2;
}

/* A property table for an object: empty name, then the given properties (already
 * in descending number order), terminated by 0. Returns the table's address and
 * records the data address of each property in data_at[]. */
typedef struct { unsigned num, size; unsigned data_at; uint8_t fill; } prop_t;

static unsigned make_ptab_named(unsigned id, prop_t *props, unsigned count, int name_words, unsigned name_word) {
    unsigned base = ptab_next, at = base, i, j;
    ram[at++] = (uint8_t)name_words;                  /* name length in words */
    if (name_words) { ram_put16(at, name_word); at += 2; }
    for (i = 0; i < count; i++) {
        at += put_prop_header(at, props[i].num, props[i].size);
        props[i].data_at = at;
        for (j = 0; j < props[i].size; j++) ram[at + j] = (uint8_t)(props[i].fill + j);
        at += props[i].size;
    }
    ram[at++] = 0;
    ptab_next = at;
    ram_put16(obj_addr(id) + cur->prop_field, base);
    return base;
}

static unsigned make_ptab(unsigned id, prop_t *props, unsigned count) {
    return make_ptab_named(id, props, count, 0, 0);
}

static void build_world(const layout_t *l, unsigned nobj) {
    unsigned i;
    cur = l;
    ptab_next = PTAB_BASE;
    memset(ram + OBJTAB, 0, (l->defaults * 2) + nobj * l->obj_size);
    for (i = 1; i <= l->defaults; i++) ram_put16(OBJTAB + (i - 1) * 2, 0x1000 + i);   /* default of prop i = 0x1000 + i */
    G->header.objtab_addr = OBJTAB;
#if ZVIBE_ENABLE_V4
    G->header.version = (uint8_t)l->version;
#endif
}

/* ---- running opcodes ---- */

static jmp_buf die_jmp;
static int die_called;
static void test_die(const char *fmt, ...) { (void)fmt; die_called = 1; longjmp(die_jmp, 1); }

static void setup_call(int nops, zWord a, zWord b, zWord c, int store, int branch) {
    unsigned at = PCAREA;
    G->operands[0] = a; G->operands[1] = b; G->operands[2] = c;
    G->operand_count = (zByte)nops;
    G->stack_ptr = G->stack_mem;
    if (store) ram[at++] = 0x00;                      /* store to the stack */
    if (branch) ram[at++] = 0xC4;                     /* branch on true, short, offset 4: taken => PC + 2 */
    G->logical_pc = PCAREA;
}

/* Run a storing opcode; returns the value it stored. */
static unsigned run_store(void (*op)(void), int nops, zWord a, zWord b, zWord c) {
    setup_call(nops, a, b, c, 1, 0);
    op();
    return z_read16_be_impl((const zByte *)(G->stack_ptr - 1));
}

/* Run a branching opcode (no store); returns 1 if the branch was taken. */
static int run_branch(void (*op)(void), int nops, zWord a, zWord b) {
    setup_call(nops, a, b, 0, 0, 1);
    op();
    return G->logical_pc == PCAREA + 1 + 2;
}

/* Run a store-then-branch opcode (get_sibling / get_child). */
static int run_store_branch(void (*op)(void), zWord a, unsigned *stored) {
    setup_call(1, a, 0, 0, 1, 1);
    op();
    *stored = z_read16_be_impl((const zByte *)(G->stack_ptr - 1));
    return G->logical_pc == PCAREA + 2 + 2;
}

/* Does the call die? */
static int dies(void (*op)(void), int nops, zWord a, zWord b) {
    setup_call(nops, a, b, 0, 0, 1);
    die_called = 0;
    if (setjmp(die_jmp) == 0) op();
    return die_called;
}

/* ---- the tests ---- */

/* Relations between objects whose numbers need both bytes in V4 (256 = 0x0100,
 * 270 = 0x010E, 299 = 0x012B): every parent, sibling and child value is wide, so a
 * value truncated to one byte anywhere is caught. */
static void test_tree(const layout_t *l, unsigned a, unsigned b, unsigned c, unsigned p, unsigned q,
                      unsigned hi, const char *name) {
    unsigned st;
    printf("== tree relations, %s (objects %u %u %u under %u %u) ==\n", name, a, b, c, p, q);
    build_world(l, hi);

    CHECK(run_branch(op_jin, 2, a, p) == 0);                       /* parentless to start */
    G->operands[0] = a; G->operands[1] = p; G->operand_count = 2; op_insert_obj();
    CHECK(get_rel(a, 0) == p && get_rel(p, 2) == a && get_rel(a, 1) == 0);
    G->operands[0] = b; G->operands[1] = p; op_insert_obj();       /* b becomes first child, sibling a */
    CHECK(get_rel(p, 2) == b && get_rel(b, 1) == a && get_rel(b, 0) == p);
    G->operands[0] = c; G->operands[1] = p; op_insert_obj();
    CHECK(get_rel(p, 2) == c && get_rel(c, 1) == b && get_rel(b, 1) == a && get_rel(a, 1) == 0);

    CHECK(run_store(op_get_parent, 1, a, 0, 0) == p);
    CHECK(run_store(op_get_parent, 1, p, 0, 0) == 0);
    CHECK(run_store_branch(op_get_child, p, &st) == 1 && st == c);
    CHECK(run_store_branch(op_get_child, a, &st) == 0 && st == 0);
    CHECK(run_store_branch(op_get_sibling, c, &st) == 1 && st == b);
    CHECK(run_store_branch(op_get_sibling, b, &st) == 1 && st == a);
    CHECK(run_store_branch(op_get_sibling, a, &st) == 0 && st == 0);
    CHECK(run_branch(op_jin, 2, a, p) == 1);
    CHECK(run_branch(op_jin, 2, a, b) == 0);

    /* remove the middle sibling: the chain must close up (walks wide sibling values) */
    G->operands[0] = b; op_remove_obj();
    CHECK(get_rel(b, 0) == 0 && get_rel(b, 1) == 0);
    CHECK(get_rel(c, 1) == a && get_rel(p, 2) == c);

    /* move the tail to another parent: it is unparented through c's wide sibling field first */
    G->operands[0] = a; G->operands[1] = q; op_insert_obj();
    CHECK(get_rel(c, 1) == 0 && get_rel(p, 2) == c);
    CHECK(get_rel(q, 2) == a && get_rel(a, 0) == q && get_rel(a, 1) == 0);

    /* remove the only child: the parent's child field is cleared */
    G->operands[0] = c; op_remove_obj();
    CHECK(get_rel(p, 2) == 0 && get_rel(c, 0) == 0);

    /* an object with children keeps them when it is moved or removed */
    G->operands[0] = c; G->operands[1] = b; op_insert_obj();       /* c under b */
    G->operands[0] = b; G->operands[1] = q; op_insert_obj();       /* b under q, ahead of a */
    CHECK(get_rel(q, 2) == b && get_rel(b, 1) == a && get_rel(b, 2) == c && get_rel(c, 0) == b);
    G->operands[0] = b; op_remove_obj();
    CHECK(get_rel(q, 2) == a && get_rel(b, 0) == 0 && get_rel(b, 1) == 0);
    CHECK(get_rel(b, 2) == c && get_rel(c, 0) == b);               /* still b's child */

    /* the last object's fields were not disturbed by writes to its neighbours */
    CHECK(get_rel(hi, 0) == 0 && get_rel(hi, 1) == 0 && get_rel(hi, 2) == 0);
}

/* Attributes, checked against the raw bytes: attribute 0 is the top bit of the
 * first byte (so attribute 8 is the top bit of the second). The neighbouring
 * object entries are filled with a sentinel and must come out unchanged. */
static void test_attributes(const layout_t *l, unsigned nattr, const char *name) {
    static const unsigned pattern[] = { 0, 1, 7, 8, 15, 16, 31, 32, 40, 47 };
    unsigned o = 4, i, k, a;
    uint8_t expect[8], before3[16], before5[16];
    printf("== attributes, %s (%u) ==\n", name, nattr);
    build_world(l, 10);
    memset(ram + obj_addr(3), 0xA5, l->obj_size);                 /* sentinel neighbours */
    memset(ram + obj_addr(5), 0xA5, l->obj_size);
    memcpy(before3, ram + obj_addr(3), l->obj_size);
    memcpy(before5, ram + obj_addr(5), l->obj_size);
    memset(expect, 0, sizeof expect);

    CHECK(run_branch(op_test_attr, 2, o, 0) == 0);
    for (k = 0; k < sizeof pattern / sizeof pattern[0]; k++) {
        a = pattern[k];
        if (a >= nattr) continue;
        G->operands[0] = o; G->operands[1] = a; G->operand_count = 2; op_set_attr();
        expect[a / 8] |= (uint8_t)(0x80 >> (a % 8));
        CHECK(memcmp(ram + obj_addr(o), expect, l->attr_bytes) == 0);       /* raw bytes, not via test_attr */
    }
    for (i = 0; i < nattr; i++) {                                           /* test_attr agrees for every number */
        int want = (expect[i / 8] & (0x80 >> (i % 8))) != 0;
        CHECK(run_branch(op_test_attr, 2, o, i) == want);
    }
    /* clear a few, high and low */
    for (k = 0; k < sizeof pattern / sizeof pattern[0]; k += 3) {
        a = pattern[k];
        if (a >= nattr) continue;
        G->operands[0] = o; G->operands[1] = a; op_clear_attr();
        expect[a / 8] &= (uint8_t)~(0x80 >> (a % 8));
        CHECK(memcmp(ram + obj_addr(o), expect, l->attr_bytes) == 0);
    }
    /* nothing outside the attribute bytes of object 4 changed */
    CHECK(memcmp(ram + obj_addr(3), before3, l->obj_size) == 0);
    CHECK(memcmp(ram + obj_addr(5), before5, l->obj_size) == 0);
    for (i = l->attr_bytes; i < l->obj_size; i++) CHECK(ram[obj_addr(o) + i] == 0);
    CHECK(dies(op_test_attr, 2, o, nattr));                                 /* first invalid attribute */
    CHECK(dies(op_set_attr, 2, o, nattr + 7));
    CHECK(dies(op_clear_attr, 2, o, nattr));
    CHECK(!dies(op_test_attr, 2, o, nattr - 1));                            /* last valid attribute */
}

static void test_properties(const layout_t *l, const char *name) {
    prop_t props[10];
    unsigned id = 3, i, n = 0;
    unsigned top = (l->version == 3) ? 31 : 63;
    printf("== properties, %s ==\n", name);
    build_world(l, 10);

    /* descending numbers; sizes 1, 2, 3 and 5 (V3: up to 8), and in V4 also 40 and 64 bytes */
    props[n++] = (prop_t){ top,  1, 0, 0x41 };
    props[n++] = (prop_t){ top - 5, 2, 0, 0x51 };
    props[n++] = (prop_t){ top - 11, l->version == 3 ? 8 : 5, 0, 0x61 };
    if (l->version == 4) props[n++] = (prop_t){ 45, 63, 0, 0x31 };     /* longest explicit size */
    if (l->version == 4) props[n++] = (prop_t){ 30, 40, 0, 0x21 };     /* size with bit 5 set: a 5-bit mask would read 8 */
    if (l->version == 4) props[n++] = (prop_t){ 25, 64, 0, 0x11 };     /* 64 (stored as 0), second byte with bit 6 set */
    if (l->version == 4) props[n++] = (prop_t){ 20, 64, 0, 0x01 };     /* 64, second byte with bit 6 clear */
    props[n++] = (prop_t){ 3, 3, 0, 0x71 };
    make_ptab(id, props, n);

    for (i = 0; i < n; i++) {                          /* addresses and lengths, looked up backwards from the data */
        unsigned addr = run_store(op_get_prop_addr, 2, id, props[i].num, 0);
        CHECK(addr == props[i].data_at);
        CHECK(run_store(op_get_prop_len, 1, addr, 0, 0) == props[i].size);
    }
    CHECK(run_store(op_get_prop_len, 1, 0, 0, 0) == 0);        /* address 0 has length 0 */

    CHECK(run_store(op_get_prop, 2, id, props[0].num, 0) == props[0].fill);                     /* size 1: the byte */
    CHECK(run_store(op_get_prop, 2, id, props[1].num, 0) == (unsigned)((props[1].fill << 8) | (props[1].fill + 1)));
    G->operands[0] = id; G->operands[1] = props[0].num; G->operands[2] = 0x1234; G->operand_count = 3; op_put_prop();
    CHECK(run_store(op_get_prop, 2, id, props[0].num, 0) == 0x34);                              /* size 1 keeps the low byte */
    G->operands[1] = props[1].num; G->operands[2] = 0xBEEF; op_put_prop();
    CHECK(run_store(op_get_prop, 2, id, props[1].num, 0) == 0xBEEF);

    /* absent property: the default table, including entries beyond number 31 in V4 */
    CHECK(run_store(op_get_prop_addr, 2, id, 2, 0) == 0);
    CHECK(run_store(op_get_prop, 2, id, 2, 0) == 0x1002);
    if (l->version == 4) {
        CHECK(run_store(op_get_prop, 2, id, 33, 0) == 0x1000 + 33);
        CHECK(run_store(op_get_prop, 2, id, 63 - 1, 0) == 0x1000 + 62);
    }

    /* enumeration: first, each next, and the end */
    CHECK(run_store(op_get_next_prop, 2, id, 0, 0) == props[0].num);
    for (i = 0; i + 1 < n; i++)
        CHECK(run_store(op_get_next_prop, 2, id, props[i].num, 0) == props[i + 1].num);
    CHECK(run_store(op_get_next_prop, 2, id, props[n - 1].num, 0) == 0);

    /* an object with no properties */
    props[0] = (prop_t){ 0, 0, 0, 0 };
    make_ptab(4, props, 0);
    CHECK(run_store(op_get_next_prop, 2, 4, 0, 0) == 0);
    CHECK(run_store(op_get_prop_addr, 2, 4, 5, 0) == 0);
}

static char outbuf[64];
static size_t outlen;
static void capture(const char *text, size_t length) {
    size_t i;
    for (i = 0; i < length && outlen + 1 < sizeof outbuf; i++) outbuf[outlen++] = text[i];
    outbuf[outlen] = 0;
}

/* print_obj reads the name through the properties-address field; print_paddr
 * prints a string at a packed address (x2 or x4). Both print "hi" here. */
static void test_printing(const layout_t *l, const char *name) {
    void (*old)(const char *, size_t) = G->writestr;
    prop_t none[1];
    printf("== printing, %s ==\n", name);
    build_world(l, 10);
    G->writestr = capture;

    make_ptab_named(5, none, 0, 1, 0xB5C5);                    /* name "hi": (h=13, i=14, pad 5) with the end bit */
    outlen = 0; outbuf[0] = 0;
    G->operands[0] = 5; G->operand_count = 1; op_print_obj();
    CHECK(strcmp(outbuf, "hi") == 0);
    ram[0x2400] = 0xB5; ram[0x2401] = 0xC5;                     /* the string "hi" at byte address 0x2400 */
    outlen = 0; outbuf[0] = 0;
    G->operands[0] = (l->version == 3) ? 0x1200 : 0x0900;       /* packed: 0x2400 / 2, or / 4 */
    op_print_paddr();
    CHECK(strcmp(outbuf, "hi") == 0);
    G->writestr = old;
}

static void test_packed(void) {
    zByte *base;
    printf("== packed addresses ==\n");
#if ZVIBE_ENABLE_V4
    G->header.version = 3;
#endif
    base = zmem_get_ptr(&G->memory_state, 0, 0);
    CHECK(z_packed_to_byte(3) == 6);
    CHECK(z_unpack_addr(0x1000) - base == 0x2000);
#if ZVIBE_ENABLE_V4
    G->header.version = 4;
    CHECK(z_packed_to_byte(3) == 12);
    CHECK(z_unpack_addr(0x0800) - base == 0x2000);
    CHECK(z_packed_to_byte(0xFFFF) == 0x3FFFC);            /* reaches the top of a 256 KB story */
    G->header.version = 3;
#endif
}

/* call: the routine's byte address follows the packed-address scale */
static void test_call_address(void) {
    printf("== call uses the packed-address scale ==\n");
    ram[0x2000] = 0;                                        /* routine with no locals */
#if ZVIBE_ENABLE_V4
    G->header.version = 3;
#endif
    setup_call(1, 0x1000, 0, 0, 1, 0);                       /* V3: 0x1000 * 2 = 0x2000 */
    op_call();
    CHECK(G->logical_pc == 0x2001);
#if ZVIBE_ENABLE_V4
    G->header.version = 4;
    setup_call(1, 0x0800, 0, 0, 1, 0);                       /* V4: 0x0800 * 4 = 0x2000 */
    op_call();
    CHECK(G->logical_pc == 0x2001);
    G->header.version = 3;
#endif
}

/* verify: the header's length field is scaled x2 in V3 and x4 in V4 */
static void test_verify_scale(void) {
    printf("== verify length scale ==\n");
    /* the story has one non-zero byte at 100; its sum is 5 */
    G->header.story_len = 0x20;                              /* 0x20 words = 64 bytes (V3), 128 bytes (V4) */
    G->header.story_checksum = 5;
#if ZVIBE_ENABLE_V4
    G->header.version = 3;
#endif
    CHECK(run_branch(op_verify, 0, 0, 0) == 0);             /* V3 covers 0x40..0x3F: nothing, sum 0, not 5 */
#if ZVIBE_ENABLE_V4
    G->header.version = 4;
    CHECK(run_branch(op_verify, 0, 0, 0) == 1);             /* V4 covers 0x40..0x7F and finds the byte */
    G->header.version = 3;
#endif
    G->header.story_checksum = 0;
    CHECK(run_branch(op_verify, 0, 0, 0) == 1);             /* V3: sum 0 matches */
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);                         /* show progress even if a test crashes */
    zvibeContext *ctx;
    printf("objects: ZVIBE_ENABLE_V4=%d\n", ZVIBE_ENABLE_V4);
    make_story_static(3, 32 * 1024, STATIC_BASE);
    story[100] = 5;                                          /* for the verify test */
    ctx = zvibe_create(sink);
    CHECK(ctx != NULL);
    CHECK(zvibe_load_story_from_memory(ctx, story, 32 * 1024) == ZVIBE_OK);
    ram = G->memory_state.ram_buffer;
    CHECK(G->memory_state.ram_size == STATIC_BASE);
    G->die = test_die;

    test_tree(&L3, 5, 6, 7, 38, 9, 40, "V3");
    test_attributes(&L3, 32, "V3");
    test_properties(&L3, "V3");
    test_printing(&L3, "V3");
    test_packed();
    test_call_address();
    test_verify_scale();
#if ZVIBE_ENABLE_V4
    test_tree(&L4, 256, 270, 299, 298, 257, 300, "V4");
    test_attributes(&L4, 48, "V4");
    test_properties(&L4, "V4");
    test_printing(&L4, "V4");
    test_packed();
    test_call_address();
    test_verify_scale();
    build_world(&L3, 10);                                    /* leave the machine as a V3 one */
#endif
    zvibe_destroy(ctx);
    printf("\n%d/%d checks passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
