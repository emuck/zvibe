/* Copyright (c) 2025 Martin R. Raumann */
/* SPDX-License-Identifier: BSD-3-Clause */
/**
 * zvibe_minimal.c - Minimal main program using zVibe API
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <limits.h>
#include "zvibe_api.h"

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

static char input_buffer[256];

static void output_func(const char *text, size_t length) {
    fwrite(text, 1, length, stdout);
    fflush(stdout);
}

static size_t read_input(char *buffer, size_t max_length) {
    fflush(stdout);

    if (fgets(buffer, (int)max_length, stdin) == NULL) {
        return 0;
    }

    size_t len = strlen(buffer);
    if (len > 0 && buffer[len - 1] == '\n') {
        buffer[len - 1] = '\0';
        len--;
    }

    return len;
}

#if ZVIBE_ENABLE_SAVE_RESTORE
/* Saves go to a file beside the story, named as the console player names it (game.z3 -> game.sav, any other
 * name gets .sav appended), so a save made by either player restores in the other. */
static char save_filename[PATH_MAX];

static void setup_save_filename(const char *story) {
    char *dot;
    size_t len;
    strncpy(save_filename, story, PATH_MAX - 1);
    save_filename[PATH_MAX - 1] = '\0';
    dot = strrchr(save_filename, '.');
    if (dot && (strcmp(dot, ".z3") == 0 || strcmp(dot, ".Z3") == 0)) {
        strcpy(dot, ".sav");
    } else {
        len = strlen(save_filename);
        if (len < PATH_MAX - 5) strcat(save_filename, ".sav");
        else save_filename[0] = '\0';          /* path too long: no saving */
    }
}

static void do_save(zvibeContext *ctx) {
    size_t size = zvibe_get_save_size(ctx), got;
    int ok = 0;
    void *buf = size ? malloc(size) : NULL;
    if (save_filename[0] == '\0') {
        printf("\n[Error: No save filename set]\n");
    } else if (!buf) {
        printf("\n[Error: Cannot save]\n");
    } else if ((got = zvibe_get_save_data(ctx, buf, size)) == 0) {
        printf("\n[Error: Cannot save]\n");
    } else {
        FILE *f = fopen(save_filename, "wb");
        if (!f) {
            printf("\n[Error: Cannot create save file %s]\n", save_filename);
        } else {
            ok = fwrite(buf, 1, got, f) == got;
            ok = (fclose(f) == 0) && ok;
            if (ok) printf("\n[Game saved to %s]\n", save_filename);
            else printf("\n[Error: Failed to write save data]\n");
        }
    }
    free(buf);
    zvibe_save_completed(ctx, ok);
}

static void do_restore(zvibeContext *ctx) {
    int ok = 0;
    FILE *f = save_filename[0] ? fopen(save_filename, "rb") : NULL;
    if (!f) {
        printf("\n[No save file found: %s]\n", save_filename);
    } else {
        long size;
        fseek(f, 0, SEEK_END);
        size = ftell(f);
        fseek(f, 0, SEEK_SET);
        if (size <= 0) {
            printf("\n[Error: Invalid save file size]\n");
        } else {
            void *buf = malloc((size_t)size);
            if (buf && fread(buf, 1, (size_t)size, f) == (size_t)size) {
                ok = zvibe_restore_data(ctx, buf, (size_t)size) == ZVIBE_OK;
                if (ok) printf("\n[Game restored from %s]\n", save_filename);
                else printf("\n[Error: That save does not fit this game]\n");
            } else {
                printf("\n[Error: Failed to read save data]\n");
            }
            free(buf);
        }
        fclose(f);
    }
    zvibe_restore_completed(ctx, ok);
}
#endif

/* Records the build configuration inside the binary, so tests can verify which
 * configuration they are exercising (see `verify-console-config` in
 * tests/Makefile). Host frontends only; costs nothing on embedded targets. */
#define ZV_STR_(x) #x
#define ZV_STR(x) ZV_STR_(x)
__attribute__((used)) static const char zvibe_build_marker[] =
    "zvibe-build:v4=" ZV_STR(ZVIBE_ENABLE_V4) ":stack=" ZV_STR(ZVIBE_STACK_WORDS);

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <story.z3>\n", argv[0]);
        return 1;
    }
    
    zvibeContext *ctx = zvibe_create(output_func);
    if (!ctx) {
        fprintf(stderr, "Failed to create context\n");
        return 1;
    }

    zvibe_set_random_seed(ctx, (int)time(NULL));
#if ZVIBE_ENABLE_SAVE_RESTORE
    setup_save_filename(argv[1]);
#endif
    
    if (zvibe_load_story(ctx, argv[1]) != ZVIBE_OK) {
        fprintf(stderr, "Failed to load story\n");
        zvibe_destroy(ctx);
        return 1;
    }
    
    while (1) {
        zvibeResult result = zvibe_run(ctx);
        
        if (result == ZVIBE_WAIT_FOR_INPUT) {
            size_t input_len = read_input(input_buffer, sizeof(input_buffer));
            if (input_len > 0 || strlen(input_buffer) == 0) {
                zvibe_input(ctx, input_buffer);
            } else {
                break;
            }
        } else if (result == ZVIBE_GAME_FINISHED) {
            break;
#if ZVIBE_ENABLE_SAVE_RESTORE
        } else if (result == ZVIBE_SAVE_REQUESTED) {
            do_save(ctx);
        } else if (result == ZVIBE_RESTORE_REQUESTED) {
            do_restore(ctx);
        } else if (result == ZVIBE_RESTART_REQUESTED) {
            printf("\n[Restarting game]\n");
            zvibe_restart_completed(ctx);
#endif
        } else {
            fprintf(stderr, "Error: %d\n", result);
            break;
        }
    }
    
    zvibe_destroy(ctx);
    return 0;
}
