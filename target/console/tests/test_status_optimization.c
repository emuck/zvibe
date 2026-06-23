/* Copyright (c) 2025 Martin R. Raumann */
/* SPDX-License-Identifier: BSD-3-Clause */
/**
 * test_status_optimization.c - Test status change detection optimization
 *
 * This test verifies that the status callback is only called when status actually changes.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "zvibe_api.h"

static int status_callback_count = 0;
static zvibeStatus last_received_status;

/* Test output function */
static void test_output(const char *text, size_t length) {
    printf("%.*s", (int)length, text);
}

/* Test input function */
static size_t test_input(char *buffer, size_t max_length) {
    if (fgets(buffer, max_length, stdin)) {
        size_t len = strlen(buffer);
        if (len > 0 && buffer[len-1] == '\n') {
            buffer[len-1] = '\0';  /* Remove newline */
            len--;
        }
        return len;
    }
    return 0;
}

/* Status callback with counting */
static void test_status_callback(const zvibeStatus *status) {
    status_callback_count++;
    last_received_status = *status;  /* Copy status */

    printf("\n[DEBUG] Status callback #%d: Location='%s', Status='%s', Score=%d, Turns=%d\n",
           status_callback_count, status->location, status->status, status->score, status->turns);
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <story_file>\n", argv[0]);
        return 1;
    }

    printf("Testing status change detection optimization...\n");
    printf("Status callbacks will only be called when status actually changes.\n\n");

    /* Create context */
    zvibeContext *ctx = zvibe_create(test_output);
    if (!ctx) {
        fprintf(stderr, "Failed to create context\n");
        return 1;
    }

    /* Set status callback */
    zvibe_set_status_callback(ctx, test_status_callback);

    /* Load story */
    if (zvibe_load_story(ctx, argv[1]) != ZVIBE_OK) {
        fprintf(stderr, "Failed to load story: %s\n", argv[1]);
        zvibe_destroy(ctx);
        return 1;
    }

    printf("Game loaded. Enter commands to test status optimization.\n");
    printf("Watch the [DEBUG] messages - they should only appear when status changes.\n\n");

    /* Main game loop */
    zvibeResult result;
    char input_buffer[256];

    do {
        result = zvibe_run(ctx);

        switch (result) {
            case ZVIBE_WAIT_FOR_INPUT:
                printf("\n> ");
                fflush(stdout);
                if (test_input(input_buffer, sizeof(input_buffer)) > 0) {
                    zvibe_input(ctx, input_buffer);
                }
                break;

            case ZVIBE_GAME_FINISHED:
                printf("\nGame finished.\n");
                break;

            case ZVIBE_SAVE_REQUESTED:
                printf("\n[Save requested - not implemented in test]\n");
                break;

            case ZVIBE_RESTORE_REQUESTED:
                printf("\n[Restore requested - not implemented in test]\n");
                break;

            default:
                printf("\nUnexpected result: %d\n", result);
                break;
        }
    } while (result == ZVIBE_WAIT_FOR_INPUT);

    printf("\nTest completed. Status callback was called %d times total.\n", status_callback_count);

    zvibe_destroy(ctx);
    return 0;
}
