/*
 * Copyright (c) 2025 Martin R. Raumann
 * SPDX-License-Identifier: BSD-3-Clause
 */
/**
 * @file zvibe_api_file.c
 * @brief Optional host-side filesystem helpers for the public API
 * @ingroup PublicAPI
 */

#include "zvibe_api.h"
#include "zvibe.h"

#if ZVIBE_ENABLE_FILE_IO
#include <stdio.h>

zvibeResult zvibe_load_story(zvibeContext *ctx, const char *filename) {
    if (!ctx || !filename) return ZVIBE_ERROR;

    FILE *file = fopen(filename, "rb");
    if (!file) return ZVIBE_ERROR;

    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return ZVIBE_ERROR;
    }

    long size = ftell(file);
    if (size <= 0 || size > Z3_STORY_MEM_SIZE) {
        fclose(file);
        return ZVIBE_ERROR;
    }

    if (fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return ZVIBE_ERROR;
    }

    /* Keep the host-side file buffer out of the core API object. */
    static uint8_t file_buffer[Z3_STORY_MEM_SIZE];

    if (fread(file_buffer, 1, (size_t)size, file) != (size_t)size) {
        fclose(file);
        return ZVIBE_ERROR;
    }

    fclose(file);
    return zvibe_load_story_from_memory(ctx, file_buffer, (size_t)size);
}
#endif
