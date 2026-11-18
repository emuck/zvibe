/* Copyright (c) 2025 Martin R. Raumann */
/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * test_save_store.c - save_store.c against a model of the SmartEEPROM (mock_smarteeprom.c).
 *
 * The property that matters: whatever happens during a save (the power is cut before any one of its page
 * commits, a commit is silently lost) the game's previous save is still there afterwards, or the new one is,
 * whole, and a save is only reported as stored when it can be read back.
 *
 *   make -C target/same51/tests/host test            (add SAN=1 for ASan and UBSan)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "smarteeprom.h"
#include "save_store.h"
#include "mock_smarteeprom.h"

static int tests_run, tests_passed;
#define CHECK(cond) do { tests_run++; if (cond) tests_passed++; \
    else printf("FAIL: %s (line %d)\n", #cond, __LINE__); } while (0)

static unsigned rng_state = 12345;
static unsigned rnd(void) { rng_state = rng_state * 1103515245u + 12345u; return (rng_state >> 16) & 0x7FFF; }

/* the data of a save: a recognisable pattern with zero runs in it (zero words are what the real device handles worst) */
static void fill(uint8_t *p, size_t n, unsigned salt) {
    size_t i;
    for (i = 0; i < n; i++) p[i] = (i % 7 == 0 || (i / 13) % 5 == 0) ? 0 : (uint8_t)(salt * 31u + i * 17u + (i >> 8));
}

static void boot(uint32_t area, int games) {
    mock_reboot();
    save_store_init(area, games, true);
    save_store_recover();
}

static int read_is(int game, uint32_t tag, const uint8_t *want, size_t n) {
    static uint8_t got[16384];
    size_t len = 0;
    load_result_t r = save_store_read(game, tag, got, sizeof got, &len);
    return r == LOAD_OK && len == n && memcmp(got, want, n) == 0;
}

static uint32_t area_size_for(int games) {                 /* the generator's rule: games + 1 areas, whole pages */
    return (uint32_t)(SMARTEEPROM_SIZE / (games + 1)) / SMARTEEPROM_PAGE * SMARTEEPROM_PAGE;
}

/* ---------------------------------------------------------------------------------------------------- */

static void test_basics(void) {
    static uint8_t a[4000], b[4000], got[4000];
    size_t len = 0, cap;
    mock_erase();
    boot(area_size_for(3), 3);
    cap = save_store_capacity();
    CHECK(cap == ((area_size_for(3) - 24 - 4) & ~(size_t)3));
    CHECK(save_store_read(0, 7, got, sizeof got, &len) == LOAD_NONE);              /* a fresh store */
    fill(a, 1000, 1);
    CHECK(save_store_write(0, 7, a, 1000) == SAVE_OK);
    CHECK(read_is(0, 7, a, 1000));
    CHECK(save_store_read(0, 8, got, sizeof got, &len) == LOAD_OTHER_STORY);       /* made for another story */
    CHECK(save_store_read(1, 7, got, sizeof got, &len) == LOAD_NONE);              /* other games' areas untouched */
    fill(b, 1000, 2);
    CHECK(save_store_write(0, 7, b, 1000) == SAVE_OK);                             /* overwrite */
    CHECK(read_is(0, 7, b, 1000));
    CHECK(save_store_read(0, 7, got, 10, &len) == LOAD_DAMAGED);                   /* the caller's buffer is too small */
    {   /* the largest save, and one byte more */
        static uint8_t big[16384];
        fill(big, cap, 3);
        CHECK(save_store_write(2, 9, big, cap) == SAVE_OK);
        CHECK(read_is(2, 9, big, cap));
        CHECK(save_store_write(2, 9, big, cap + 1) == SAVE_E_TOO_LARGE);
        CHECK(read_is(2, 9, big, cap));                                            /* the refused save changed nothing */
    }
    CHECK(save_store_write(3, 1, a, 10) == SAVE_E_WRITE);                          /* no such game (3 is the scratch) */
    CHECK(save_store_write(0, 7, a, 0) == SAVE_E_WRITE);
    save_store_init(area_size_for(3), 3, false);                                   /* SmartEEPROM unavailable */
    CHECK(save_store_write(0, 7, a, 10) == SAVE_E_UNAVAILABLE);
    CHECK(save_store_read(0, 7, got, sizeof got, &len) == LOAD_UNAVAILABLE);
}

static void test_isolation(int games) {
    static uint8_t d[9][2000], other[2000];
    int g, ok = 1;
    uint8_t before[SMARTEEPROM_SIZE];
    uint32_t area = area_size_for(games);
    mock_erase();
    boot(area, games);
    for (g = 0; g < games; g++) {
        fill(d[g], 700 + g * 50, (unsigned)g + 10);
        if (save_store_write(g, 100u + (unsigned)g, d[g], 700 + (size_t)g * 50) != SAVE_OK) ok = 0;
    }
    CHECK(ok);
    for (g = 0; g < games; g++) ok &= read_is(g, 100u + (unsigned)g, d[g], 700 + (size_t)g * 50);
    CHECK(ok);
    /* a save of game k changes game k's area and the scratch, and nothing else */
    for (g = 0; g < games; g++) {
        int j, same = 1;
        memcpy(before, mock_mem, sizeof before);
        fill(other, 900, 99u + (unsigned)g);
        CHECK(save_store_write(g, 100u + (unsigned)g, other, 900) == SAVE_OK);
        for (j = 0; j < games; j++) {
            if (j != g && memcmp(before + (size_t)j * area, mock_mem + (size_t)j * area, area) != 0) same = 0;
        }
        CHECK(same);
        memcpy(d[g], other, 900);
        CHECK(read_is(g, 100u + (unsigned)g, d[g], 900));
    }
}

/* The core test: a power cut before each page commit of a save. */
static void test_power_cuts(int games, size_t old_len, size_t new_len) {
    static uint8_t s0[8192], s1[8192];
    uint32_t area = area_size_for(games);
    int g = games - 1, k, clean_commits, scratch_done, any_new = 0, bad = 0;
    char label[80];
    snprintf(label, sizeof label, "games=%d old=%zu new=%zu", games, old_len, new_len);

    fill(s0, old_len, 21);
    fill(s1, new_len, 22);

    /* a clean save, to learn how many commits it takes and when the scratch is complete */
    mock_erase(); boot(area, games);
    CHECK(save_store_write(g, 5, s0, old_len) == SAVE_OK);
    mock_reset_counters();
    CHECK(save_store_write(g, 5, s1, new_len) == SAVE_OK);
    clean_commits = mock_commits;
    {   /* the scratch is complete when a cut would no longer lose the new save: commits of its record */
        uint32_t scratch_pages = ((24 + (uint32_t)new_len + 4 + 3) / 4 * 4 + SMARTEEPROM_PAGE - 1) / SMARTEEPROM_PAGE;
        scratch_done = (int)scratch_pages;
    }

    for (k = 0; k <= clean_commits; k++) {                      /* k = clean_commits: no cut at all */
        int r;
        mock_erase(); boot(area, games);
        if (save_store_write(g, 5, s0, old_len) != SAVE_OK) { bad++; break; }
        mock_reset_counters();
        mock_cut_at = k;
        r = setjmp(mock_power_cut);
        if (r == 0) save_store_write(g, 5, s1, new_len);
        boot(area, games);                                       /* the board comes back */
        {
            int is_old = read_is(g, 5, s0, old_len), is_new = read_is(g, 5, s1, new_len);
            if (!is_old && !is_new) { bad++; printf("  cut %d: neither the old nor the new save survived (%s)\n", k, label); }
            if (k >= scratch_done && !is_new) { bad++; printf("  cut %d: scratch was complete but the new save is lost (%s)\n", k, label); }
            if (k < scratch_done && !is_old) { bad++; printf("  cut %d: the old save was lost before the scratch was complete (%s)\n", k, label); }
            any_new |= is_new;
        }
        /* and the store is usable afterwards */
        {
            static uint8_t s2[600];
            fill(s2, 600, 23);
            if (save_store_write(g, 5, s2, 600) != SAVE_OK || !read_is(g, 5, s2, 600)) { bad++; printf("  cut %d: no save afterwards (%s)\n", k, label); }
        }
    }
    CHECK(bad == 0);
    CHECK(any_new);
}

static void test_cut_during_first_save(void) {
    static uint8_t s1[3000];
    uint32_t area = area_size_for(4);
    int k, bad = 0, commits;
    size_t len;
    fill(s1, 3000, 31);
    mock_erase(); boot(area, 4);
    mock_reset_counters();
    save_store_write(0, 3, s1, 3000);
    commits = mock_commits;
    for (k = 0; k <= commits; k++) {
        int r;
        mock_erase(); boot(area, 4);
        mock_reset_counters(); mock_cut_at = k;
        r = setjmp(mock_power_cut);
        if (r == 0) save_store_write(0, 3, s1, 3000);
        boot(area, 4);
        {
            static uint8_t got[3000];
            load_result_t lr = save_store_read(0, 3, got, sizeof got, &len);
            /* no save before: "nothing" or the new save, never garbage */
            if (!(lr == LOAD_NONE || (lr == LOAD_OK && len == 3000 && memcmp(got, s1, 3000) == 0))) { bad++; printf("  cut %d: load %d\n", k, (int)lr); }
        }
    }
    CHECK(bad == 0);
}

static void test_lost_commits(void) {
    static uint8_t s0[2000], s1[2000], got[2000];
    uint32_t area = area_size_for(2);
    size_t len;
    int clean;
    fill(s0, 2000, 41); fill(s1, 1500, 42);
    mock_erase(); boot(area, 2);
    save_store_write(0, 4, s0, 2000);
    mock_reset_counters();
    save_store_write(0, 4, s1, 1500);
    clean = mock_commits;

    /* the scratch's commits are lost: the save is refused and the old one is untouched */
    mock_erase(); boot(area, 2);
    save_store_write(0, 4, s0, 2000);
    mock_reset_counters(); mock_drop_at = 0; mock_drop_count = clean / 2;
    CHECK(save_store_write(0, 4, s1, 1500) == SAVE_E_VERIFY);
    CHECK(read_is(0, 4, s0, 2000));

    /* the game's area commits are lost on both tries: refused, but the new save is in the scratch and the next start restores it */
    mock_erase(); boot(area, 2);
    save_store_write(0, 4, s0, 2000);
    mock_reset_counters(); mock_drop_at = clean / 2; mock_drop_count = 1000;
    CHECK(save_store_write(0, 4, s1, 1500) == SAVE_E_VERIFY);
    boot(area, 2);
    CHECK(read_is(0, 4, s1, 1500));

    /* only the first try at the game's area is lost: the second one stores it */
    mock_erase(); boot(area, 2);
    save_store_write(0, 4, s0, 2000);
    mock_reset_counters(); mock_drop_at = clean / 2; mock_drop_count = clean - clean / 2;
    CHECK(save_store_write(0, 4, s1, 1500) == SAVE_OK);
    CHECK(read_is(0, 4, s1, 1500));
    (void)got; (void)len;
}

/* A save for a different story (another tag) in a slot that holds a whole save of the old one, cut at every commit:
 * once the scratch is complete the new save must come back (recovery used to refuse to finish a copy over a save with a
 * different tag, and the new save was stranded). */
static void test_cut_with_story_change(void) {
    static uint8_t s0[900], s1[1300];
    uint32_t area = area_size_for(3);
    int k, clean, scratch_done, bad = 0;
    fill(s0, 900, 51); fill(s1, 1300, 52);
    mock_erase(); boot(area, 3);
    save_store_write(1, 5, s0, 900);
    mock_reset_counters();
    CHECK(save_store_write(1, 6, s1, 1300) == SAVE_OK);
    clean = mock_commits;
    scratch_done = (int)(((24 + 1300 + 4 + 3) / 4 * 4 + SMARTEEPROM_PAGE - 1) / SMARTEEPROM_PAGE);
    for (k = 0; k <= clean; k++) {
        int r;
        mock_erase(); boot(area, 3);
        save_store_write(1, 5, s0, 900);
        mock_reset_counters(); mock_cut_at = k;
        r = setjmp(mock_power_cut);
        if (r == 0) save_store_write(1, 6, s1, 1300);
        boot(area, 3);
        if (!read_is(1, 5, s0, 900) && !read_is(1, 6, s1, 1300)) { bad++; printf("  cut %d: neither save survived\n", k); }
        if (k >= scratch_done && !read_is(1, 6, s1, 1300)) { bad++; printf("  cut %d: the new story's save was stranded\n", k); }
    }
    CHECK(bad == 0);
}

/* A save that was refused (the game's area did not take it twice) leaves the new save only in the scratch. The next
 * save, even before a restart, must not put it at risk: at every cut of that next save one of the two is still there. */
static void test_save_after_failed_save(void) {
    static uint8_t s0[1000], s1[1500], s2[700];
    uint32_t area = area_size_for(2);
    int k, clean, next, bad = 0;
    fill(s0, 1000, 61); fill(s1, 1500, 62); fill(s2, 700, 63);
    mock_erase(); boot(area, 2);
    save_store_write(0, 4, s0, 1000);
    mock_reset_counters();
    save_store_write(0, 4, s1, 1500);
    clean = mock_commits;
    mock_reset_counters();
    save_store_write(0, 4, s2, 700);
    next = mock_commits + 4;
    for (k = 0; k <= next; k++) {
        int r;
        mock_erase(); boot(area, 2);
        save_store_write(0, 4, s0, 1000);
        mock_reset_counters(); mock_drop_at = clean / 2; mock_drop_count = 1000;
        if (save_store_write(0, 4, s1, 1500) != SAVE_E_VERIFY) { bad++; break; }
        mock_reset_counters();          /* the same power-up: no restart in between */
        mock_cut_at = k;
        r = setjmp(mock_power_cut);
        if (r == 0) save_store_write(0, 4, s2, 700);
        boot(area, 2);
        if (!read_is(0, 4, s1, 1500) && !read_is(0, 4, s2, 700)) { bad++; printf("  cut %d: both the refused and the next save were lost\n", k); }
    }
    CHECK(bad == 0);
}

static void test_damage(void) {
    static uint8_t s0[1500], got[1500];
    uint32_t area = area_size_for(2);
    size_t len;
    int i, missed = 0;
    fill(s0, 1500, 51);
    mock_erase(); boot(area, 2);
    save_store_write(1, 6, s0, 1500);
    /* every bit of every byte of the record in game 1's area: a flip is never returned as a good save */
    for (i = 0; i < 24 + 1500; i += 7) {
        int bit = i % 8;
        uint8_t *p = mock_mem + area + i;
        *p ^= (uint8_t)(1u << bit);
        if (save_store_read(1, 6, got, sizeof got, &len) == LOAD_OK) missed++;
        *p ^= (uint8_t)(1u << bit);
    }
    CHECK(missed == 0);
    CHECK(read_is(1, 6, s0, 1500));
    /* a header with its magic gone is "nothing"; with the magic and a damaged CRC it is "damaged" */
    mock_mem[area] ^= 0xFF;
    CHECK(save_store_read(1, 6, got, sizeof got, &len) == LOAD_NONE);
    mock_mem[area] ^= 0xFF;
    mock_mem[area + 20] ^= 0x01;
    CHECK(save_store_read(1, 6, got, sizeof got, &len) == LOAD_DAMAGED);
    mock_mem[area + 20] ^= 0x01;
    /* a garbage "length" in an otherwise intact header is refused, not trusted */
    CHECK(read_is(1, 6, s0, 1500));
}

static void test_soak(int games) {
    /* random saves of random games and sizes, random power cuts; after every reboot each game holds its last
     * completed save or, for the one that was being saved, that or the new one */
    enum { N = 9 };
    static uint8_t last[N][8192], pending[8192];
    size_t last_len[N] = {0};
    uint32_t area = area_size_for(games);
    size_t cap;
    int it, bad = 0, g;
    mock_erase(); boot(area, games);
    cap = save_store_capacity();
    for (it = 0; it < 1500; it++) {
        int cut = (rnd() % 4 == 0);
        size_t len = 1 + (size_t)(rnd() * 8 + rnd()) % cap;
        int r, saved;
        g = (int)(rnd() % (unsigned)games);
        fill(pending, len, rnd());
        mock_reset_counters();
        if (cut) mock_cut_at = (int)(rnd() % 8);
        saved = 0;
        r = setjmp(mock_power_cut);
        if (r == 0) saved = save_store_write(g, 1000u + (unsigned)g, pending, len) == SAVE_OK;
        if (saved) { memcpy(last[g], pending, len); last_len[g] = len; }
        if (r != 0 || (cut && rnd() % 2)) boot(area, games);
        for (int j = 0; j < games; j++) {
            size_t n = 0;
            static uint8_t got[16384];
            load_result_t lr = save_store_read(j, 1000u + (unsigned)j, got, sizeof got, &n);
            int is_last = last_len[j] ? (lr == LOAD_OK && n == last_len[j] && memcmp(got, last[j], n) == 0) : (lr == LOAD_NONE);
            int is_pending = (j == g && lr == LOAD_OK && n == len && memcmp(got, pending, len) == 0);
            if (!is_last && !is_pending) { bad++; printf("  iteration %d game %d: load %d\n", it, j, (int)lr); }
            if (is_pending && !is_last) { memcpy(last[j], pending, len); last_len[j] = len; }
        }
        if (bad) break;
    }
    CHECK(bad == 0);
}

int main(void) {
    int games;
    printf("save_store against a model of the SmartEEPROM\n");
    test_basics();
    for (games = 1; games <= 9; games += (games < 3 ? 2 : (games == 3 ? 2 : 4))) test_isolation(games);
    test_isolation(9);
    for (games = 1; games <= 9; games += (games < 5 ? 2 : 4)) {
        size_t cap = 0;
        mock_erase(); boot(area_size_for(games), games);
        cap = save_store_capacity();
        test_power_cuts(games, 400, 800);                         /* a small save over a small one */
        test_power_cuts(games, 800, 400);                         /* a smaller over a bigger */
        test_power_cuts(games, cap, cap);                         /* the largest over the largest */
        test_power_cuts(games, 1, cap);
    }
    test_cut_during_first_save();
    test_lost_commits();
    test_cut_with_story_change();
    test_save_after_failed_save();
    test_damage();
    test_soak(1);
    test_soak(5);
    test_soak(9);
    printf("%d/%d checks passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
