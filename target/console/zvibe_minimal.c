/* Copyright (c) 2025 Martin R. Raumann */
/* SPDX-License-Identifier: BSD-3-Clause */
/**
 * zvibe_minimal.c - Minimal main program using zVibe API
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "zvibe_api.h"

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
        } else {
            fprintf(stderr, "Error: %d\n", result);
            break;
        }
    }
    
    zvibe_destroy(ctx);
    return 0;
}
