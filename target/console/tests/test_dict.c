/* Copyright (c) 2025 Martin R. Raumann */
/* SPDX-License-Identifier: BSD-3-Clause */
/**
 * test_dict.c - Dictionary word encoding, lookup and tokenizing, V3 and V4
 *
 * A dictionary word is 4 bytes (6 Z-characters) in V3 and 6 bytes (9
 * Z-characters) in V4. The reference encoder below is written from the standard
 * (alphabet tables, padding with Z-character 5, end-of-text bit on the last
 * word) and does not share code with src/core/zvibe_ops_text.c, which this file
 * includes directly in order to reach its static functions.
 *
 * Built twice: V3-only, and V3+V4 (the V3 cases must behave identically in the
 * dual build; the V4 cases set G->header.version to 4 after loading a V3 story,
 * so the dictionary code is tested in isolation from loading).
 */

#include <stdio.h>
#include "test_story.h"
#include "../../../src/core/zvibe_ops_text.c"

#ifndef EXPECT_V4
#error "build with -DEXPECT_V4=0|1"
#endif
#if ZVIBE_ENABLE_V4 != EXPECT_V4
#error "this binary was built with the wrong ZVIBE_ENABLE_V4"
#endif

/* ---- reference encoder, from the standard ---- */

/* Alphabet 2 starts at Z-character 6: 6 is the escape, 7 newline, then 0-9 and punctuation */
static const char A2_REF[] = " \n0123456789.,!?_#'\"/\\-:()";

/* Encode `text` into `nwords` big-endian words (3 Z-characters each). Built the
 * way the standard describes it: write out the Z-characters of the WHOLE text
 * first, then keep only the first 3*nwords of them, pad with 5, and set the
 * end-of-text bit on the last word. (Production code appends incrementally and
 * stops at the limit; this builds first and truncates, so they do not share a
 * structure.) */
static void ref_encode(const char *text, int nwords, unsigned out[3]) {
    unsigned char full[4 * 64];
    int n = 0, limit = nwords * 3, i, w;
    for (i = 0; text[i]; i++) {
        unsigned char c = (unsigned char)text[i];
        const char *p;
        if (c >= 'A' && c <= 'Z') c = (unsigned char)(c + 32);
        if (c >= 'a' && c <= 'z') { full[n++] = (unsigned char)(6 + (c - 'a')); continue; }   /* alphabet 0 */
        p = strchr(A2_REF, c);
        full[n++] = 5;                                                                         /* shift to alphabet 2 */
        if (p && c) { full[n++] = (unsigned char)(6 + (p - A2_REF)); continue; }
        full[n++] = 6;                                                                         /* ZSCII escape */
        full[n++] = (unsigned char)(c >> 5);
        full[n++] = (unsigned char)(c & 31);
    }
    for (i = n; i < 9; i++) full[i] = 5;                                                       /* pad */
    for (w = 0; w < nwords; w++)
        out[w] = (unsigned)((full[3 * w] << 10) | (full[3 * w + 1] << 5) | full[3 * w + 2]);
    (void)limit;
    out[nwords - 1] |= 0x8000;
}

static int words_for(int version) { return version == 3 ? 2 : 3; }

static int cmp_words(const unsigned *a, const unsigned *b, int n) {
    int i;
    for (i = 0; i < n; i++) if (a[i] != b[i]) return a[i] < b[i] ? -1 : 1;
    return 0;
}

/* ---- dictionary fixture in dynamic memory ---- */

#define STATIC_BASE 0x3C00u
#define DICT        0x0300u
#define INBUF       0x0A00u
#define PARSEBUF    0x0B00u

static uint8_t *ram;

typedef struct { const char *text; unsigned enc[3]; } entry_t;

static int entry_sort(const void *x, const void *y, int n) { return cmp_words(((const entry_t *)x)->enc, ((const entry_t *)y)->enc, n); }

/* Build a dictionary of the given words for `version` at DICT; returns entry count.
 * Layout: [n separators][separators][entry length][count, 2 bytes][entries], each
 * entry the encoded text then 3 data bytes. Entries are sorted by encoded value. */
static unsigned g_extra = 3;                                     /* data bytes after each entry's text */

static unsigned build_dict(int version, const char *const *words, unsigned count, const char *seps) {
    int nw = words_for(version);
    unsigned i, j, at = DICT, elen = (unsigned)nw * 2 + g_extra;
    entry_t e[16];
    for (i = 0; i < count; i++) { e[i].text = words[i]; ref_encode(words[i], nw, e[i].enc); }
    for (i = 1; i < count; i++)                                   /* insertion sort */
        for (j = i; j > 0 && entry_sort(&e[j - 1], &e[j], nw) > 0; j--) { entry_t t = e[j]; e[j] = e[j - 1]; e[j - 1] = t; }
    ram[at++] = (uint8_t)strlen(seps);
    memcpy(&ram[at], seps, strlen(seps)); at += (unsigned)strlen(seps);
    ram[at++] = (uint8_t)elen;
    ram[at++] = (uint8_t)(count >> 8); ram[at++] = (uint8_t)count;
    for (i = 0; i < count; i++) {
        for (j = 0; j < (unsigned)nw; j++) { ram[at++] = (uint8_t)(e[i].enc[j] >> 8); ram[at++] = (uint8_t)e[i].enc[j]; }
        for (j = 0; j < g_extra; j++) ram[at++] = (uint8_t)(0xD0 + j);
    }
    G->header.dict_addr = DICT;
    return count;
}

static unsigned entry_addr(int version, const char *const *words, unsigned count, const char *seps, const char *w) {
    /* address of the entry for word w (by encoded sort order), from the layout above */
    int nw = words_for(version);
    unsigned i, rank = 0, elen = (unsigned)nw * 2 + g_extra;
    unsigned target[3], other[3];
    ref_encode(w, nw, target);
    for (i = 0; i < count; i++) { ref_encode(words[i], nw, other); if (cmp_words(other, target, nw) < 0) rank++; }
    return DICT + 1 + (unsigned)strlen(seps) + 1 + 2 + rank * elen;
}

/* ---- tests ---- */

static void test_encode(int version) {
    static const char *const samples[] = { "look", "a", "x", "elephant", "elephantine", "zzzzzzzzzz", "it's", "3d", "n.e.", "(a)", "",
                                           "a@b", "x[y]", "{z}", "~~~~", "abc@def", "abcdef@", "!!!!!!!!!!", "a|b|c|d|e" };
    unsigned i, w;
    printf("== encoding, V%d ==\n", version);
#if ZVIBE_ENABLE_V4
    G->header.version = (uint8_t)version;
#endif
    for (i = 0; i < sizeof samples / sizeof samples[0]; i++) {
        unsigned want[3] = {0, 0, 0};
        zWord got[Z_MAX_DICT_WORDS] = {0};
        int nw = words_for(version);
        zByte len = (zByte)strlen(samples[i]);
        ref_encode(samples[i], nw, want);
        z_encode_zchars((const zByte *)samples[i], len, got);
        for (w = 0; w < (unsigned)nw; w++) {
            if (got[w] != want[w]) printf("  '%s' word %u: got %04X want %04X\n", samples[i], w, got[w], want[w]);
            CHECK(got[w] == want[w]);
        }
    }
    {   /* upper case folds to lower case */
        zWord a[Z_MAX_DICT_WORDS], b[Z_MAX_DICT_WORDS];
        z_encode_zchars((const zByte *)"LOOK", 4, a);
        z_encode_zchars((const zByte *)"look", 4, b);
        CHECK(memcmp(a, b, sizeof(zWord) * words_for(version)) == 0);
    }
}

/* Expected words written out by hand from the standard, NOT produced by either
 * encoder. Z-characters: letters a=6..z=31; shift to alphabet 2 = 5; in alphabet 2
 * Z-character 6 = ZSCII escape, then the 10-bit code as two 5-bit halves; '!' is
 * Z-character 20 (6 + its index 14 in alphabet 2); padding is 5. Three per word,
 * the last word gets 0x8000. */
static void test_literal_vectors(int version) {
    static const struct { const char *text; int ver; unsigned w[3]; const char *why; } v[] = {
        /* V3: 6 Z-characters, 2 words */
        { "look",    3, { 0x4694, 0xC0A5 }, "l o o k + 2 pad: (17,20,20) (16,5,5)|0x8000" },
        { "abcde!f", 3, { 0x18E8, 0xA545 }, "a b c d e then the shift fills slot 6; '!' and 'f' are cut" },
        { "a@b",     3, { 0x18A6, 0x8807 }, "a, shift, 6, 2, 0 (ZSCII 64), b" },
        { "abc@",    3, { 0x18E8, 0x94C2 }, "a b c shift 6 2: the escape's last half (0) is cut" },
        { "abcd@",   3, { 0x18E8, 0xA4A6 }, "a b c d shift 6: escape marker is the 6th slot" },
        { "",        3, { 0x14A5, 0x94A5 }, "all padding" },
        /* V4: 9 Z-characters, 3 words */
        { "look",       4, { 0x4694, 0x40A5, 0x94A5 }, "(17,20,20) (16,5,5) (5,5,5)|0x8000" },
        { "abcdefgh!i", 4, { 0x18E8, 0x254B, 0xB1A5 }, "8 letters, then the shift takes slot 9; 'i' is cut" },
        { "elephantine", 4, { 0x2A2A, 0x55A6, 0xCF2E }, "elephanti: (10,17,10) (21,13,6) (19,25,14)|0x8000" },
    };
    unsigned i, w;
    printf("== literal vectors, V%d ==\n", version);
#if ZVIBE_ENABLE_V4
    G->header.version = (uint8_t)version;
#endif
    for (i = 0; i < sizeof v / sizeof v[0]; i++) {
        zWord got[Z_MAX_DICT_WORDS] = {0};
        if (v[i].ver != version) continue;
        z_encode_zchars((const zByte *)v[i].text, (zByte)strlen(v[i].text), got);
        for (w = 0; w < (unsigned)words_for(version); w++) {
            if (got[w] != v[i].w[w]) printf("  '%s' (%s) word %u: got %04X want %04X\n", v[i].text, v[i].why, w, got[w], v[i].w[w]);
            CHECK(got[w] == v[i].w[w]);
        }
    }
}

static void test_truncation(int version) {
    /* V3 keeps 6 Z-characters, V4 keeps 9: words sharing a prefix of that length collide */
    zWord a[Z_MAX_DICT_WORDS], b[Z_MAX_DICT_WORDS], c[Z_MAX_DICT_WORDS];
    size_t bytes = sizeof(zWord) * (size_t)words_for(version);
    printf("== truncation, V%d ==\n", version);
#if ZVIBE_ENABLE_V4
    G->header.version = (uint8_t)version;
#endif
    z_encode_zchars((const zByte *)"ambulance", 9, a);
    z_encode_zchars((const zByte *)"ambulate", 8, b);              /* shares "ambula" (6 letters) with the first */
    z_encode_zchars((const zByte *)"ambulances", 10, c);           /* the 10th letter is beyond both limits */
    if (version == 3) CHECK(memcmp(a, b, bytes) == 0);             /* identical after 6 letters */
    else              CHECK(memcmp(a, b, bytes) != 0);             /* the 7th letter now matters */
    CHECK(memcmp(a, c, bytes) == 0);                               /* 9 letters equal, 10th ignored in both */
}

/* Words for a dictionary; V3 omits "ambulate" because it would encode identically to "ambulance" */
static unsigned dict_words(int version, const char *const **out) {
    static const char *const v3[] = { "a", "ask", "go", "look", "lamp", "take", "ambulance", "x" };
    static const char *const v4[] = { "a", "ask", "go", "look", "lamp", "take", "ambulance", "ambulate", "x" };
    *out = (version == 3) ? v3 : v4;
    return (version == 3) ? sizeof v3 / sizeof v3[0] : sizeof v4 / sizeof v4[0];
}

static void test_lookup(int version) {
    const char *const *words;
    const unsigned count = dict_words(version, &words);
    int nw = words_for(version);
    const unsigned elen = (unsigned)nw * 2 + 3, hdr = 1 + 1 + 2;    /* no separators: count byte, entry length, 2-byte count */
    const zByte *dict = &ram[DICT + hdr];
    const zWord base = (zWord)(DICT + hdr);
    zWord enc[Z_MAX_DICT_WORDS];
    unsigned i;
    printf("== lookup, V%d ==\n", version);
#if ZVIBE_ENABLE_V4
    G->header.version = (uint8_t)version;
#endif
    build_dict(version, words, count, "");

    for (i = 0; i < count; i++) {                                  /* every word is found at its sorted position */
        zWord got;
        z_encode_zchars((const zByte *)words[i], (zByte)strlen(words[i]), enc);
        got = z_dict_lookup(dict, (zWord)count, (zByte)elen, base, enc);
        CHECK(got != 0 && got == entry_addr(version, words, count, "", words[i]));
    }
    z_encode_zchars((const zByte *)"zebra", 5, enc);                /* sorts after everything */
    CHECK(z_dict_lookup(dict, (zWord)count, (zByte)elen, base, enc) == 0);
    z_encode_zchars((const zByte *)"aa", 2, enc);                   /* sorts between "a" and "ask" */
    CHECK(z_dict_lookup(dict, (zWord)count, (zByte)elen, base, enc) == 0);
    z_encode_zchars((const zByte *)"loo", 3, enc);                  /* a prefix of an entry is not that entry */
    CHECK(z_dict_lookup(dict, (zWord)count, (zByte)elen, base, enc) == 0);

    if (version == 3) {                                             /* V3: the longer word finds the same entry */
        zWord x, y;
        z_encode_zchars((const zByte *)"ambulance", 9, enc);
        x = z_dict_lookup(dict, (zWord)count, (zByte)elen, base, enc);
        z_encode_zchars((const zByte *)"ambulate", 8, enc);
        y = z_dict_lookup(dict, (zWord)count, (zByte)elen, base, enc);
        CHECK(x != 0 && x == y);
    } else {                                                        /* V4: they are two entries */
        zWord x, y;
        z_encode_zchars((const zByte *)"ambulance", 9, enc);
        x = z_dict_lookup(dict, (zWord)count, (zByte)elen, base, enc);
        z_encode_zchars((const zByte *)"ambulate", 8, enc);
        y = z_dict_lookup(dict, (zWord)count, (zByte)elen, base, enc);
        CHECK(x != 0 && y != 0 && x != y);
    }

    {   /* a one-entry dictionary: the search must not run off either end */
        static const char *const one[] = { "only" };
        build_dict(version, one, 1, "");
        z_encode_zchars((const zByte *)"only", 4, enc);
        CHECK(z_dict_lookup(&ram[DICT + hdr], 1, (zByte)elen, base, enc) == base);
        z_encode_zchars((const zByte *)"onl", 3, enc);
        CHECK(z_dict_lookup(&ram[DICT + hdr], 1, (zByte)elen, base, enc) == 0);
        z_encode_zchars((const zByte *)"zzz", 3, enc);
        CHECK(z_dict_lookup(&ram[DICT + hdr], 1, (zByte)elen, base, enc) == 0);
    }
}

static void test_tokenize(int version) {
    static const char *const words[] = { "look", "at", "lamp", "get" };
    const unsigned count = sizeof words / sizeof words[0];
    const char *line = "look at  lamp,xyz";                          /* five tokens: look at lamp , xyz */
    static const struct { const char *word; unsigned len, pos; } want[] = {
        { "look", 4, 1 }, { "at", 2, 6 }, { "lamp", 4, 10 }, { NULL, 1, 14 } /* "," */, { NULL, 3, 15 } /* "xyz" */
    };
    unsigned i, n = (unsigned)strlen(line);
    const zByte *parse;
    printf("== tokenizing, V%d ==\n", version);
#if ZVIBE_ENABLE_V4
    G->header.version = (uint8_t)version;
#endif
    build_dict(version, words, count, ",");
    ram[INBUF] = 60;                                               /* max input length */
    for (i = 0; i < n; i++) ram[INBUF + 1 + i] = (uint8_t)line[i];
    ram[INBUF + 1 + n] = 0;
    ram[PARSEBUF] = 8;                                             /* max tokens */
    z_tokenize_input(INBUF, PARSEBUF);
    parse = &ram[PARSEBUF];
    CHECK(parse[1] == 5);
    for (i = 0; i < 5; i++) {
        const zByte *t = &parse[2 + i * 4];
        unsigned addr = ((unsigned)t[0] << 8) | t[1];
        CHECK(addr == (want[i].word ? entry_addr(version, words, count, ",", want[i].word) : 0));
        CHECK(t[2] == want[i].len);
        CHECK(t[3] == want[i].pos);
    }
}

static void test_empty_and_entry_lengths(int version) {
    static const char *const words[] = { "look", "lamp", "take", "ask" };
    int nw = words_for(version);
    unsigned extra, i, hdr = 4;
    zWord enc[Z_MAX_DICT_WORDS];
    printf("== empty dictionary and entry lengths, V%d ==\n", version);
#if ZVIBE_ENABLE_V4
    G->header.version = (uint8_t)version;
#endif
    z_encode_zchars((const zByte *)"look", 4, enc);
    CHECK(z_dict_lookup(&ram[DICT + hdr], 0, (zByte)(nw * 2 + 3), (zWord)(DICT + hdr), enc) == 0);   /* zero entries */

    /* tokenizing against an empty dictionary finds nothing and stays in bounds */
    g_extra = 3;
    build_dict(version, words, 0, "");
    ram[INBUF] = 60; memcpy(&ram[INBUF + 1], "look lamp", 10);
    ram[PARSEBUF] = 8;
    z_tokenize_input(INBUF, PARSEBUF);
    CHECK(ram[PARSEBUF + 1] == 2);
    CHECK(ram[PARSEBUF + 2] == 0 && ram[PARSEBUF + 3] == 0 && ram[PARSEBUF + 6] == 0 && ram[PARSEBUF + 7] == 0);

    /* the entry length comes from the dictionary header, not from the version */
    for (extra = 1; extra <= 7; extra += 3) {
        const unsigned count = sizeof words / sizeof words[0];
        const unsigned elen = (unsigned)nw * 2 + extra;
        g_extra = extra;
        build_dict(version, words, count, "");
        for (i = 0; i < count; i++) {
            zWord got;
            z_encode_zchars((const zByte *)words[i], (zByte)strlen(words[i]), enc);
            got = z_dict_lookup(&ram[DICT + hdr], (zWord)count, (zByte)elen, (zWord)(DICT + hdr), enc);
            CHECK(got != 0 && got == entry_addr(version, words, count, "", words[i]));
        }
    }
    g_extra = 3;
}

static void test_separators(int version) {
    static const char *const words[] = { "a", "b", "go" };
    const char *line = "a,.b;go";                                   /* adjacent separators , . ; are tokens of their own */
    static const struct { const char *word; unsigned len, pos; } want[] = {
        { "a", 1, 1 }, { NULL, 1, 2 }, { NULL, 1, 3 }, { "b", 1, 4 }, { NULL, 1, 5 }, { "go", 2, 6 }
    };
    unsigned i, n = (unsigned)strlen(line);
    printf("== multiple adjacent separators, V%d ==\n", version);
#if ZVIBE_ENABLE_V4
    G->header.version = (uint8_t)version;
#endif
    g_extra = 3;
    build_dict(version, words, 3, ",.;");
    ram[INBUF] = 60;
    for (i = 0; i < n; i++) ram[INBUF + 1 + i] = (uint8_t)line[i];
    ram[INBUF + 1 + n] = 0;
    ram[PARSEBUF] = 10;
    z_tokenize_input(INBUF, PARSEBUF);
    CHECK(ram[PARSEBUF + 1] == 6);
    for (i = 0; i < 6; i++) {
        const uint8_t *t = &ram[PARSEBUF + 2 + i * 4];
        unsigned addr = ((unsigned)t[0] << 8) | t[1];
        CHECK(addr == (want[i].word ? entry_addr(version, words, 3, ",.;", want[i].word) : 0));
        CHECK(t[2] == want[i].len && t[3] == want[i].pos);
    }
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);                         /* show progress even if a test crashes */
    zvibeContext *ctx;
    printf("dict: ZVIBE_ENABLE_V4=%d\n", ZVIBE_ENABLE_V4);
    make_story_static(3, 16 * 1024, STATIC_BASE);
    ctx = zvibe_create(sink);
    CHECK(ctx != NULL);
    CHECK(zvibe_load_story_from_memory(ctx, story, 16 * 1024) == ZVIBE_OK);
    ram = G->memory_state.ram_buffer;

    test_literal_vectors(3);
    test_encode(3);
    test_truncation(3);
    test_empty_and_entry_lengths(3);
    test_separators(3);
    test_lookup(3);
    test_tokenize(3);
#if ZVIBE_ENABLE_V4
    test_literal_vectors(4);
    test_encode(4);
    test_truncation(4);
    test_empty_and_entry_lengths(4);
    test_separators(4);
    test_lookup(4);
    test_tokenize(4);
    G->header.version = 3;
#endif
    zvibe_destroy(ctx);
    printf("\n%d/%d checks passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
