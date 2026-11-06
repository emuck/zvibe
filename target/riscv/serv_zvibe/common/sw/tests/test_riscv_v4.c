/* Copyright (c) 2025 Martin R. Raumann */
/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * test_riscv_v4.c - the RISC-V version 4 player (zvibe_riscv_v4.c) on the host.
 *
 * Runs the firmware's own player code with the real interpreter, the real ANSI screen
 * layer, the firmware's own flash save layer (flash_save.c, with the Arty's slot
 * configuration) on a mock flash that enforces erase-before-program, and a VT100 emulator
 * for the terminal. What it cannot cover is the board: the UART, the timing of the
 * busy-wait loops (RV_SPIN_PER_MS) and the real QSPI flash.
 *
 * A scenario is one "boot": a script of terminal input (the terminal's answer to the size
 * probe first, then keys). When the player waits for input and the script is used up,
 * the run stops and the screen is checked. The flash survives from one boot to the next,
 * like the real flash.
 *
 * The test stories are the console tests' (target/console/tests/data, the .z4 files; sources beside them).
 * Built with ZVIBE_ENABLE_V4=1; see tests/Makefile.
 */

#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "zvibe_api.h"
#include "zvibe_screen.h"
#include "zvibe_riscv_v4.h"
#include "flash_save.h"
#include "flash_save_config.h"
#include "flash_hal.h"
#include "vt100_emu.h"

#ifndef DATA_DIR
#define DATA_DIR "../../../../../console/tests/data"
#endif

static int tests_run, tests_passed;
#define CHECK(cond) do { tests_run++; if (cond) tests_passed++; \
    else printf("FAIL: %s (line %d)\n", #cond, __LINE__); } while (0)

/* ---- mock flash: NOR semantics, as the S25FL128S ------------------------------------ */

#define NUM_SLOTS 4
static uint8_t flash_img[NUM_SLOTS * SLOT_SIZE];
static int flash_violations;           /* programming a bit from 0 back to 1, or a write outside the image */
static int flash_fail_program;         /* make flash_program_bytes fail (a worn or write-protected part) */
static int flash_erases, flash_programs;

static void flash_wipe(void) { memset(flash_img, 0xFF, sizeof flash_img); }

void flash_read_bytes(uint32_t addr, void *buf, size_t len) {
    if (addr + len > sizeof flash_img) { flash_violations++; memset(buf, 0xFF, len); return; }
    memcpy(buf, flash_img + addr, len);
}

int flash_erase_sector(uint32_t addr) {
    if (addr % SECTOR_SIZE || addr + SECTOR_SIZE > sizeof flash_img) { flash_violations++; return -1; }
    memset(flash_img + addr, 0xFF, SECTOR_SIZE);
    flash_erases++;
    return 0;
}

int flash_program_bytes(uint32_t addr, const void *data, size_t len) {
    const uint8_t *p = (const uint8_t *)data;
    size_t i;
    if (flash_fail_program) return -1;
    if (addr + len > sizeof flash_img) { flash_violations++; return -1; }
    for (i = 0; i < len; i++) {
        if ((flash_img[addr + i] & p[i]) != p[i]) flash_violations++;   /* needs an erase first */
        flash_img[addr + i] &= p[i];
    }
    flash_programs++;
    return 0;
}

/* ---- the terminal -------------------------------------------------------------------- */

static const uint8_t *in_buf;
static size_t in_len, in_pos;
static unsigned idle_calls;
static jmp_buf stop;

void rv_put_byte(uint8_t b) { em_put(b); }

int rv_rx_get(void) {
    if (in_pos < in_len) { idle_calls = 0; return in_buf[in_pos++]; }
    return -1;
}

/* The player is waiting and the script is used up. The size probe waits about 20000 idle
 * calls for an answer before it gives up; stop only after that. */
void rv_idle(void) {
    if (in_pos >= in_len && ++idle_calls > 40000) longjmp(stop, 1);
}

#define SIZE_REPLY "\033[24;80R"           /* what a terminal answers to the probe */

static int row_has(int r, const char *s) {
    char line[COLS + 1];
    memcpy(line, grid[r], COLS); line[COLS] = 0;
    return strstr(line, s) != NULL;
}

static int screen_has(const char *s) {
    int r;
    for (r = 0; r < ROWS; r++) if (row_has(r, s)) return 1;
    return 0;
}

static void dump_screen(void) {
    int r;
    for (r = 0; r < ROWS; r++) {
        char line[COLS + 1];
        int n = COLS;
        memcpy(line, grid[r], COLS);
        while (n > 0 && line[n - 1] == ' ') n--;
        line[n] = 0;
        printf("  |%s\n", line);
    }
}

/* ---- stories ---------------------------------------------------------------------------- */

static uint8_t *load_story(const char *name, size_t *size) {
    char path[512];
    FILE *f;
    uint8_t *buf;
    snprintf(path, sizeof path, "%s/%s", DATA_DIR, name);
    f = fopen(path, "rb");
    if (!f) { printf("cannot open %s\n", path); exit(2); }
    fseek(f, 0, SEEK_END);
    *size = (size_t)ftell(f);
    fseek(f, 0, SEEK_SET);
    buf = (uint8_t *)malloc(*size);
    if (!buf || fread(buf, 1, *size, f) != *size) { printf("cannot read %s\n", path); exit(2); }
    fclose(f);
    return buf;
}

static uint8_t *story_save, *story_big, *story_keys, *story_lines, *story_screen;
static size_t   size_save, size_big, size_keys, size_lines, size_screen;

/* One boot of the firmware: reset the terminal, mount the save slots, play `story` with
 * `script` as the terminal's input. Returns how the player ended, or -1 if the script ran out. */
static int boot(const uint8_t *story, size_t size, const char *script, size_t script_len) {
    int r;
    em_reset();
    in_buf = (const uint8_t *)script;
    in_len = script_len;
    in_pos = 0;
    idle_calls = 0;
    flash_save_init(0, NUM_SLOTS);
    if (setjmp(stop)) return -1;
    r = (int)rv_v4_play(story, size);
    return r;
}

/* ---- scenarios -------------------------------------------------------------------------- */

static void test_basic_keys_and_quit(void) {
    /* v4save: i (count), i, p, then q: the story says bye and quits */
    int r = boot(story_save, size_save, SIZE_REPLY "iipq", sizeof(SIZE_REPLY "iipq") - 1);
    CHECK(r == RV_V4_FINISHED);
    CHECK(screen_has("v4save ready"));
    CHECK(screen_has("counter=2"));
    CHECK(screen_has("bye"));
    CHECK(screen_has("[Game finished]"));
    if (tests_passed != tests_run) dump_screen();
}

static void test_save_then_restore_after_reboot(void) {
    int r;
    flash_wipe();
    flash_violations = 0;
    r = boot(story_save, size_save, SIZE_REPLY "iiis", sizeof(SIZE_REPLY "iiis") - 1);
    CHECK(r == -1);                                  /* waiting for the next key */
    CHECK(screen_has("counter=3"));
    CHECK(screen_has("save result 1"));

    /* power cycle: a fresh boot, the counter is back at zero until the restore */
    r = boot(story_save, size_save, SIZE_REPLY "pr" "p", sizeof(SIZE_REPLY "pr" "p") - 1);
    CHECK(r == -1);
    CHECK(screen_has("counter=0"));
    CHECK(screen_has("save result 2"));                 /* a restore resumes at the save, with 2 */
    CHECK(screen_has("counter=3"));
    CHECK(flash_violations == 0);
}

static void test_restore_with_no_save(void) {
    int r;
    flash_wipe();
    r = boot(story_save, size_save, SIZE_REPLY "r", sizeof(SIZE_REPLY "r") - 1);
    CHECK(r == -1);
    CHECK(screen_has("[No saved game found]"));
    CHECK(screen_has("restore result 0"));
}

static void test_a_save_from_another_story_is_refused(void) {
    int r;
    flash_wipe();
    r = boot(story_save, size_save, SIZE_REPLY "is", sizeof(SIZE_REPLY "is") - 1);
    CHECK(r == -1);
    CHECK(screen_has("save result 1"));
    /* v4bigsave takes the same keys: its restore must not accept v4save's save */
    r = boot(story_big, size_big, SIZE_REPLY "r", sizeof(SIZE_REPLY "r") - 1);
    CHECK(r == -1);
    CHECK(screen_has("[That save is from a different story or release]"));
    CHECK(screen_has("restore result 0"));
    /* and the right story still gets it */
    r = boot(story_save, size_save, SIZE_REPLY "rp", sizeof(SIZE_REPLY "rp") - 1);
    CHECK(r == -1);
    CHECK(screen_has("save result 2"));
    CHECK(screen_has("counter=1"));
}

/* A save made for a story must not load against a story that differs in any one of the
 * identity fields: release (header 2-3), serial number (0x12-0x17), checksum (0x1C-0x1D) */
static void test_every_identity_field_is_checked(void) {
    static const size_t field[] = { 3, 2, 0x12, 0x15, 0x17, 0x1C, 0x1D };
    size_t i;
    int refused = 0, r;
    uint8_t *other = (uint8_t *)malloc(size_save);
    flash_wipe();
    r = boot(story_save, size_save, SIZE_REPLY "is", sizeof(SIZE_REPLY "is") - 1);
    CHECK(r == -1);
    CHECK(screen_has("save result 1"));
    for (i = 0; i < sizeof field / sizeof field[0]; i++) {
        memcpy(other, story_save, size_save);
        other[field[i]] ^= 1;
        r = boot(other, size_save, SIZE_REPLY "r", sizeof(SIZE_REPLY "r") - 1);
        if (r == -1 && screen_has("[That save is from a different story or release]") &&
            screen_has("restore result 0")) refused++;
        else printf("  identity byte 0x%02zX not checked\n", field[i]);
    }
    CHECK(refused == (int)(sizeof field / sizeof field[0]));
    free(other);
}

static void test_a_damaged_save_is_refused(void) {
    int r, i, hit = 0;
    flash_wipe();
    r = boot(story_save, size_save, SIZE_REPLY "iis", sizeof(SIZE_REPLY "iis") - 1);
    CHECK(r == -1);
    CHECK(screen_has("save result 1"));
    /* flip one bit inside the saved data (past the 12-byte slot header), in each of a few places */
    for (i = 0; i < 3; i++) {
        uint8_t keep[NUM_SLOTS * SLOT_SIZE];
        size_t at = SLOT_SIZE * 0 + SAVE_HEADER_SIZE + 4 + (size_t)i * 9;
        memcpy(keep, flash_img, sizeof keep);
        flash_img[at] ^= 0x04;
        r = boot(story_save, size_save, SIZE_REPLY "r", sizeof(SIZE_REPLY "r") - 1);
        if (screen_has("damaged") && screen_has("restore result 0")) hit++;
        memcpy(flash_img, keep, sizeof keep);
    }
    CHECK(hit == 3);
    /* the untouched save still restores */
    r = boot(story_save, size_save, SIZE_REPLY "rp", sizeof(SIZE_REPLY "rp") - 1);
    CHECK(r == -1);
    CHECK(screen_has("save result 2"));
    CHECK(screen_has("counter=2"));
}

static int sum_on_screen(void) {
    int r;
    for (r = ROWS - 1; r >= 0; r--) {
        char line[COLS + 1];
        char *p;
        memcpy(line, grid[r], COLS); line[COLS] = 0;
        p = strstr(line, "sum=");
        if (p) return atoi(p + 4);
    }
    return -1;
}

static void test_a_big_story_packs_into_a_slot(void) {
    int r, before, after;
    flash_wipe();
    flash_violations = 0;
    /* v4bigsave keeps a 20,000-byte array in dynamic memory; f fills 1000 bytes of it */
    r = boot(story_big, size_big, SIZE_REPLY "ffcs", sizeof(SIZE_REPLY "ffcs") - 1);
    CHECK(r == -1);
    CHECK(screen_has("filled 2"));
    CHECK(screen_has("save result 1"));
    before = sum_on_screen();
    CHECK(before > 0);
    r = boot(story_big, size_big, SIZE_REPLY "rc", sizeof(SIZE_REPLY "rc") - 1);
    CHECK(r == -1);
    after = sum_on_screen();
    CHECK(screen_has("save result 2"));
    CHECK(after == before);
    CHECK(flash_violations == 0);
}

static void test_a_save_too_large_for_a_slot_fails_cleanly(void) {
    int r;
    char script[64];
    int n = 0, i;
    flash_wipe();
    memcpy(script, SIZE_REPLY, sizeof(SIZE_REPLY) - 1);
    n = (int)sizeof(SIZE_REPLY) - 1;
    for (i = 0; i < 19; i++) script[n++] = 'f';       /* 19,000 changed bytes: more than RV_V4_PACKED_MAX */
    script[n++] = 's';
    r = boot(story_big, size_big, script, (size_t)n);
    CHECK(r == -1);
    CHECK(screen_has("[Save failed: the changes do not fit a save slot]"));
    CHECK(screen_has("save result 0"));
    /* the game goes on, and a smaller state can still be saved after it */
    r = boot(story_big, size_big, SIZE_REPLY "fs", sizeof(SIZE_REPLY "fs") - 1);
    CHECK(r == -1);
    CHECK(screen_has("save result 1"));
}

static void test_a_flash_write_error_is_reported(void) {
    int r;
    flash_wipe();
    flash_fail_program = 1;
    r = boot(story_save, size_save, SIZE_REPLY "is", sizeof(SIZE_REPLY "is") - 1);
    flash_fail_program = 0;
    CHECK(r == -1);
    CHECK(screen_has("[Save failed: flash write error]"));
    CHECK(screen_has("save result 0"));
    CHECK(!screen_has("save result 1"));
}

static void test_the_newest_of_several_saves_wins(void) {
    int r;
    flash_wipe();
    r = boot(story_save, size_save, SIZE_REPLY "is", sizeof(SIZE_REPLY "is") - 1);
    CHECK(r == -1);
    r = boot(story_save, size_save, SIZE_REPLY "iiis", sizeof(SIZE_REPLY "iiis") - 1);
    CHECK(r == -1);
    r = boot(story_save, size_save, SIZE_REPLY "iiiiis", sizeof(SIZE_REPLY "iiiiis") - 1);
    CHECK(r == -1);
    r = boot(story_save, size_save, SIZE_REPLY "rp", sizeof(SIZE_REPLY "rp") - 1);
    CHECK(r == -1);
    CHECK(screen_has("save result 2"));
    CHECK(screen_has("counter=5"));
}

static void test_line_input(void) {
    int r;
    /* typing, backspace, a Return that ends the line, and a command typed over the row's end */
    r = boot(story_lines, size_lines, SIZE_REPLY "look around\rabc\b\bd\r", sizeof(SIZE_REPLY "look around\rabc\b\bd\r") - 1);
    CHECK(r == -1);
    CHECK(screen_has("you said: look around"));
    CHECK(screen_has("you said: ad"));
    /* CR LF is one Return; an arrow key typed into a line is ignored */
    r = boot(story_lines, size_lines, SIZE_REPLY "x\r\n\033[Ay\r", sizeof(SIZE_REPLY "x\r\n\033[Ay\r") - 1);
    CHECK(r == -1);
    CHECK(screen_has("you said: x"));
    CHECK(screen_has("you said: y"));
}

static void test_line_longer_than_the_story_buffer(void) {
    /* v4lines has a 40-byte text buffer: 39 characters and the end mark */
    char script[200];
    int n = (int)sizeof(SIZE_REPLY) - 1, i, r;
    memcpy(script, SIZE_REPLY, (size_t)n);
    for (i = 0; i < 60; i++) script[n++] = (char)('a' + i % 26);
    script[n++] = '\r';
    r = boot(story_lines, size_lines, script, (size_t)n);
    CHECK(r == -1);
    CHECK(screen_has("you said: abcdefghijklmnopqrstuvwxyzabcdefghijklm"));   /* 39: the buffer's 40 less the end mark */
    CHECK(!screen_has("lmnopqrstuvwxyzabcdefghijklmn"));
}

static void test_reset_command_returns_to_the_caller(void) {
    int r = boot(story_lines, size_lines, SIZE_REPLY "~reset~\r", sizeof(SIZE_REPLY "~reset~\r") - 1);
    CHECK(r == RV_V4_RESET);
}

static void test_read_char_keys(void) {
    int r;
    /* v4keys prints the ZSCII code of every key: Return, an arrow, Escape followed by x,
     * Delete, then q ends the keys and a line is read */
    r = boot(story_keys, size_keys,
             SIZE_REPLY "\r\033[A\033x\177q" "hi\r",
             sizeof(SIZE_REPLY "\r\033[A\033x\177q" "hi\r") - 1);
    CHECK(r == RV_V4_FINISHED);
    CHECK(screen_has("key=13"));
    CHECK(screen_has("key=129"));
    CHECK(screen_has("key=27"));
    CHECK(screen_has("key=120"));
    CHECK(screen_has("key=8"));
    CHECK(screen_has("key=113"));
    CHECK(screen_has("line=[hi]"));
}

static void test_cr_lf_is_one_key(void) {
    int r = boot(story_keys, size_keys, SIZE_REPLY "a\r\nb", sizeof(SIZE_REPLY "a\r\nb") - 1);
    int rows13 = 0, i;
    CHECK(r == -1);
    for (i = 0; i < ROWS; i++) if (row_has(i, "key=13")) rows13++;
    CHECK(rows13 == 1);
    CHECK(screen_has("key=97"));
    CHECK(screen_has("key=98"));
}

static void test_no_answer_to_the_size_probe(void) {
    /* a terminal that does not answer: the player falls back to 24 x 80 and plays on */
    int r = boot(story_save, size_save, "", 0);
    CHECK(r == -1);
    CHECK(screen_has("v4save ready"));
    CHECK(screen_has("key?"));
}

static void test_probe_ignores_noise_before_the_answer(void) {
    int r = boot(story_save, size_save, "zz\033[x" SIZE_REPLY "ip", sizeof("zz\033[x" SIZE_REPLY "ip") - 1);
    CHECK(r == -1);
    CHECK(screen_has("counter=1"));
}

static void test_size_probe_answers(void) {
    /* v4screen prints the screen size the interpreter was given (header bytes 32 and 33) */
    int r = boot(story_screen, size_screen, "\033[24;60Rx", sizeof("\033[24;60Rx") - 1);
    CHECK(r == -1);
    CHECK(screen_has("size 24x60"));
    /* an answer too small to play on is not believed: the size from before stays */
    r = boot(story_screen, size_screen, "\033[5;20Rx", sizeof("\033[5;20Rx") - 1);
    CHECK(r == -1);
    CHECK(screen_has("size 24x60"));
    /* a good answer sets it again (the other tests expect 24 x 80) */
    r = boot(story_screen, size_screen, SIZE_REPLY "x", sizeof(SIZE_REPLY "x") - 1);
    CHECK(r == -1);
    CHECK(screen_has("size 24x80"));
}

static void test_screen_story_draws_a_split_screen(void) {
    /* v4screen splits the window, draws a reverse-video status bar, writes positioned and bold
     * text in the upper window, scrolls the lower window and reads a key and a line. The player
     * only supplies the terminal. First stop while the story waits for its line, with the
     * screen as the story drew it. */
    int r = boot(story_screen, size_screen, SIZE_REPLY "x", sizeof(SIZE_REPLY "x") - 1);
    int i;
    CHECK(r == -1);
    for (i = 0; i < 79; i++) CHECK(attr[0][i] == 1);          /* the reverse-video bar, but not column 80 */
    CHECK(attr[0][79] == 0);
    CHECK(row_has(0, "ZVIBE V4"));
    CHECK(row_has(0, "Score: 7"));
    CHECK(row_has(1, "Row two"));
    CHECK(row_has(2, "    bold"));
    for (i = 4; i < 8; i++) CHECK(attr[2][i] == 2);           /* bold */
    CHECK(screen_has("size 24x80"));                           /* the size the player measured, in the header */
    CHECK(screen_has("press a key: key=120"));
    CHECK(row_has(23, "> "));                                  /* the prompt is on the last row */

    /* then the whole story, which ends: the player restores the terminal and the screen scrolls up */
    r = boot(story_screen, size_screen, SIZE_REPLY "x" "hi\r", sizeof(SIZE_REPLY "x" "hi\r") - 1);
    CHECK(r == RV_V4_FINISHED);
    CHECK(screen_has("got: hi"));
    CHECK(screen_has("[Game finished]"));
    CHECK(cur_attr == 0);                                      /* the terminal is left in its plain rendition */
    CHECK(reg_top == 0 && reg_bot == ROWS - 1);                /* and without a scroll region */
    CHECK(autowrap == 1);
}

static void test_a_v3_story_is_not_a_v4_story(void) {
    uint8_t v3[0x60] = {3};
    CHECK(!rv_v4_is_v4_story(v3, sizeof v3));
    CHECK(rv_v4_is_v4_story(story_save, size_save));
    CHECK(!rv_v4_is_v4_story(story_save, 8));
    CHECK(!rv_v4_is_v4_story(NULL, 0));
}

int main(void) {
    story_save   = load_story("v4save.z4", &size_save);
    story_big    = load_story("v4bigsave.z4", &size_big);
    story_keys   = load_story("v4keys.z4", &size_keys);
    story_lines  = load_story("v4lines.z4", &size_lines);
    story_screen = load_story("v4screen.z4", &size_screen);

    printf("RISC-V V4 player, host build (packed save max %d B, buffers %zu B)\n",
           RV_V4_PACKED_MAX, rv_v4_save_buffers_bytes());

    test_basic_keys_and_quit();
    test_save_then_restore_after_reboot();
    test_restore_with_no_save();
    test_a_save_from_another_story_is_refused();
    test_every_identity_field_is_checked();
    test_a_damaged_save_is_refused();
    test_a_big_story_packs_into_a_slot();
    test_a_save_too_large_for_a_slot_fails_cleanly();
    test_a_flash_write_error_is_reported();
    test_the_newest_of_several_saves_wins();
    test_line_input();
    test_line_longer_than_the_story_buffer();
    test_reset_command_returns_to_the_caller();
    test_read_char_keys();
    test_cr_lf_is_one_key();
    test_no_answer_to_the_size_probe();
    test_probe_ignores_noise_before_the_answer();
    test_size_probe_answers();
    test_screen_story_draws_a_split_screen();
    test_a_v3_story_is_not_a_v4_story();

    printf("%d/%d checks passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
