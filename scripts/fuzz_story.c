/* Copyright (c) 2025 Martin R. Raumann */
/* SPDX-License-Identifier: BSD-3-Clause */
/**
 * fuzz_story.c - run mutated story files under ASan/UBSan
 *
 * Flips random bytes of a story (often in the header), loads it and runs it for a
 * few input rounds in a child process. A refusal to load, a clean exit and the
 * interpreter's own die() (an abort with no sanitizer text) are fine; a sanitizer
 * report is printed (one line each) and a hang counts separately.
 *
 *   cc -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined \
 *      -DZVIBE_ENABLE_V4=1 -DZVIBE_MEMORY_BUFFER_SIZE=73728 -Iinclude \
 *      -o fuzz_story scripts/fuzz_story.c src/core/zvibe_*.c src/api/zvibe_api.c src/api/zvibe_api_file.c
 *   ASAN_OPTIONS=detect_leaks=0 ./fuzz_story games/catalog/czech.z4 500 1 | sort | uniq -c | sort -rn
 *
 * Arguments: story file, number of mutations, random seed. Not part of `make test`:
 * a corrupt story can still reach null pointers the interpreter does not check (see
 * docs/V4_DESIGN.md, "Corrupt stories"). Sorting the output by location shows which
 * kinds of report remain. A story that makes AddressSanitizer fire is saved as
 * /tmp/zvibe_fuzz_asan.z (the last one) so it can be replayed.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include "zvibe_api.h"

static void sink(const char *t, size_t n) { (void)t; (void)n; }

static void run_story(const uint8_t *story, size_t n, const char *errfile) {
    zvibeContext *c;
    int k;
    if (!freopen(errfile, "w", stderr)) _exit(3);
    alarm(3);
    c = zvibe_create(sink);
    zvibe_set_random_seed(c, 1);
    if (zvibe_load_story_from_memory(c, story, n) != ZVIBE_OK) _exit(0);
    for (k = 0; k < 20; k++) {
        zvibeResult r = zvibe_run(c);
        if (r == ZVIBE_WAIT_FOR_INPUT) zvibe_input(c, "look");
#if ZVIBE_ENABLE_V4
        else if (r == ZVIBE_WAIT_FOR_CHAR) zvibe_input_char(c, 'a');
#endif
        else if (r == ZVIBE_RESTART_REQUESTED) zvibe_restart_completed(c);
        else if (r == ZVIBE_SAVE_REQUESTED) zvibe_save_completed(c, 0);
        else if (r == ZVIBE_RESTORE_REQUESTED) zvibe_restore_completed(c, 0);
        else break;
    }
    _exit(0);
}

int main(int argc, char **argv) {
    static uint8_t orig[200000], buf[200000];
    char errfile[] = "/tmp/zvibe_fuzz_XXXXXX";
    FILE *f;
    size_t n;
    int iters, i, clean = 0, died = 0, hung = 0, reports = 0;

    if (argc < 4) { fprintf(stderr, "usage: %s story iterations seed\n", argv[0]); return 2; }
    f = fopen(argv[1], "rb");
    if (!f) { perror(argv[1]); return 2; }
    n = fread(orig, 1, sizeof orig, f);
    fclose(f);
    iters = atoi(argv[2]);
    srand((unsigned)atoi(argv[3]));
    close(mkstemp(errfile));

    for (i = 0; i < iters; i++) {
        int m = 1 + rand() % 12, status;
        pid_t p;
        memcpy(buf, orig, n);
        while (m--) buf[(rand() % 4 == 0) ? (size_t)(rand() % 64) : (size_t)rand() % n] = (uint8_t)rand();
        fflush(NULL);
        p = fork();
        if (p == 0) run_story(buf, n, errfile);
        waitpid(p, &status, 0);
        if (WIFEXITED(status) && WEXITSTATUS(status) == 0) { clean++; continue; }
        if (WIFSIGNALED(status) && WTERMSIG(status) == SIGALRM) { hung++; continue; }
        {
            static char text[8192];
            size_t len = 0;
            char *hit, *start;
            FILE *e = fopen(errfile, "r");
            if (e) { len = fread(text, 1, sizeof text - 1, e); fclose(e); }
            text[len] = 0;
            hit = strstr(text, "runtime error");
            if (!hit) hit = strstr(text, "ERROR: AddressSanitizer");
            if (!hit) { died++; continue; }               /* the interpreter's own die() */
            if (strstr(hit, "AddressSanitizer")) {
                FILE *o = fopen("/tmp/zvibe_fuzz_asan.z", "wb");
                if (o) { fwrite(buf, 1, n, o); fclose(o); }
                fputs(text, stderr);
            }
            for (start = hit; start > text && start[-1] != '\n'; start--) {}
            if ((hit = strchr(start, '\n'))) *hit = 0;
            printf("%s\n", start);
            reports++;
        }
    }
    unlink(errfile);
    fprintf(stderr, "%s: %d runs: %d clean, %d died (die), %d hung, %d sanitizer reports\n",
            argv[1], iters, clean, died, hung, reports);
    return 0;
}
