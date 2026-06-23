/*
 * Copyright (c) 2025 Martin R. Raumann
 * SPDX-License-Identifier: BSD-3-Clause
 */
/**
 * @file zvibe_api.c
 * @brief Public API implementation
 * @ingroup PublicAPI
 *
 * Implements the application-facing API layer. Wraps core interpreter
 * functions with a small explicit drive loop and conditional features.
 *
 * **Embedded Constraints:**
 * - Uses static context allocation (no malloc for context)
 * - Single context instance supported
 * - Output/status callbacks invoked synchronously during zvibe_run()
 *
 * **Implementation Notes:**
 * - Global g_ctx pointer for output callback access
 * - Status line change detection to minimize callback overhead
 * - Optional filesystem loader compiled separately from other API features
 */

#include "zvibe_api.h"
#include "zvibe.h"
#if !ZVIBE_FREESTANDING
#include <stdlib.h>
#endif
#include <string.h>
#if ZVIBE_ENABLE_STATUS_LINE || ZVIBE_ENABLE_DIAGNOSTICS
#include <stdio.h>
#endif
#if ZVIBE_ENABLE_DIAGNOSTICS
#include <stdarg.h>
#endif

/**
 * @brief Internal context structure
 *
 * Wraps zState with API-specific data.
 * Allocated statically in zvibe_create().
 */
struct zvibeContext_s {
    zState z_state;                 /**< Core interpreter state */
    zvibeOutputFunc output_func;    /**< Text output callback */
#if ZVIBE_ENABLE_STATUS_LINE
    zvibeStatusFunc status_func;    /**< Status line callback (optional) */
    zvibeStatus status;             /**< Current status */
    int status_initialized;         /**< First status sent flag */
#endif
};

/** @brief Global context for callback access (single instance) */
static zvibeContext *g_ctx = NULL;

/* Forward declarations */
extern void z_process_input(const char *input_text);
extern int z_load_story_data(const void *data, size_t size);
extern void z_random_set_seed(zSDWord seed);

#if ZVIBE_ENABLE_STATUS_LINE
/**
 * @brief Compare status structures to detect changes
 *
 * Used to minimize status callback invocations.
 *
 * @return 1 if status changed, 0 if identical
 */
static int status_has_changed(const zvibeStatus *new_status, const zvibeStatus *prev_status) {
    if (!new_status || !prev_status) return 1;  /* Always update if either is NULL */

    return (strcmp(new_status->location, prev_status->location) != 0 ||
            strcmp(new_status->status, prev_status->status) != 0 ||
            new_status->is_time != prev_status->is_time ||
            new_status->score != prev_status->score ||
            new_status->turns != prev_status->turns ||
            new_status->hours != prev_status->hours ||
            new_status->minutes != prev_status->minutes);
}
#endif

/* Core callbacks */
static void output_callback(const char *text, size_t length) {
    if (g_ctx && g_ctx->output_func) {
        g_ctx->output_func(text, length);
    }
}

static void error_callback(const char *fmt, ...) {
#if ZVIBE_ENABLE_DIAGNOSTICS
    va_list args;
    va_start(args, fmt);
    fprintf(stderr, "ERROR: ");
    vfprintf(stderr, fmt, args);
    fprintf(stderr, "\n");
    va_end(args);
#else
    (void)fmt;
#endif
#if !ZVIBE_FREESTANDING
    abort();
#else
    for (;;) {
    }
#endif
}

/* API Implementation */
zvibeContext* zvibe_create(zvibeOutputFunc output_func) {
    if (!output_func) return NULL;
    
    static zvibeContext ctx;
    g_ctx = &ctx;
    
    memset(&ctx, 0, sizeof(ctx));
    ctx.output_func = output_func;
    
    /* Initialize Z-machine */
    G = &ctx.z_state;
    z_init_machine(G);
    G->writestr = output_callback;
    G->die = error_callback;
    
    return &ctx;
}

void zvibe_destroy(zvibeContext *ctx) {
    if (ctx == g_ctx) {
        g_ctx = NULL;
        G = NULL;
    }
}

zvibeResult zvibe_load_story_from_memory(zvibeContext *ctx, const uint8_t *data, size_t size) {
    if (!ctx || !data || size == 0) return ZVIBE_ERROR;
    
    if (size > Z3_STORY_MEM_SIZE) {
        return ZVIBE_ERROR;
    }
    
    /* Load story data using static buffers (no malloc) - unified implementation */
    int result = z_load_story_data(data, size);
    return (result == 0) ? ZVIBE_OK : ZVIBE_ERROR;
}

zvibeResult zvibe_run(zvibeContext *ctx) {
    if (!ctx) return ZVIBE_ERROR;
    
    zState *G = &ctx->z_state;
    
    while (1) {
        if (G->input_requested) {
#if ZVIBE_ENABLE_STATUS_LINE
            /* Update status line once per command cycle, right before waiting for input */
            if (ctx->status_func) {
                zvibeStatus new_status;
                zvibe_get_status(ctx, &new_status);

                /* Only call status callback if status actually changed or first time */
                if (!ctx->status_initialized || status_has_changed(&new_status, &ctx->status)) {
                    ctx->status = new_status;        /* Update current status */
                    ctx->status_func(&ctx->status);  /* Notify callback */
                    ctx->status_initialized = 1;     /* Mark as initialized */
                }
            }
#endif
            return ZVIBE_WAIT_FOR_INPUT;
        }
        
        if (G->quit_flag) {
            return ZVIBE_GAME_FINISHED;
        }
        
#if ZVIBE_ENABLE_SAVE_RESTORE
        /* Handle save/restore requests */
        if (G->save_requested) {
            return ZVIBE_SAVE_REQUESTED;
        }
        
        if (G->restore_requested) {
            return ZVIBE_RESTORE_REQUESTED;
        }
        
        if (G->restart_requested) {
            return ZVIBE_RESTART_REQUESTED;
        }
#else
        /* Minimal version - skip save/restore silently */
        if (G->save_requested) {
            G->save_requested = 0;
            z_do_branch(0); /* Fail save */
            continue;
        }
        
        if (G->restore_requested) {
            G->restore_requested = 0;
            z_do_branch(0); /* Fail restore */
            continue;
        }
        
        if (G->restart_requested) {
            G->restart_requested = 0;
            if (G->memory_state.flash_data && G->memory_state.ram_buffer && G->memory_state.ram_size > 0) {
                memcpy(G->memory_state.ram_buffer, G->memory_state.flash_data, G->memory_state.ram_size);
                zmem_write_byte(&G->memory_state, 1, (zmem_byte_t)(G->header.flags1 | (1U << 4)));
            }
            G->logical_pc = G->header.pc_start;
            G->quit_flag = 0;
            G->instr_count = 0;
            G->input_requested = 0;
            memset(G->stack_mem, 0, sizeof(G->stack_mem));
            G->stack_ptr = G->stack_mem;
            G->base_ptr = 0;
            continue;
        }
#endif
        
        z_run_instruction();
    }
}

zvibeResult zvibe_input(zvibeContext *ctx, const char *input) {
    if (!ctx || !input) return ZVIBE_ERROR;
    
    zState *G = &ctx->z_state;
    if (!G->input_requested) return ZVIBE_ERROR;
    
    z_process_input(input);
    
    return ZVIBE_OK;
}

void zvibe_set_random_seed(zvibeContext *ctx, int seed) {
    (void)ctx;  /* seed is global state; ctx kept for API consistency */
    z_random_set_seed((zSDWord)seed);
}

#if ZVIBE_ENABLE_STATUS_LINE
zvibeResult zvibe_set_status_callback(zvibeContext *ctx, zvibeStatusFunc status_func) {
    if (!ctx) return ZVIBE_ERROR;
    ctx->status_func = status_func;
    return ZVIBE_OK;
}

zvibeResult zvibe_get_status(zvibeContext *ctx, zvibeStatus *status) {
    if (!ctx || !status) return ZVIBE_ERROR;
    
    zState *G = &ctx->z_state;
    memset(status, 0, sizeof(zvibeStatus));
    
    /* Get global variables for score and turns */
    const zByte *globals = zmem_get_ptr(&G->memory_state, G->header.globals_addr, 0);
    if (!globals) return ZVIBE_ERROR;
    
    /* In Version 3, score is global variable 1, turns is global variable 2 */
    status->score = (globals[2] << 8) | globals[3];
    status->turns = (globals[4] << 8) | globals[5];
    
    /* Check if game uses time or score */
    status->is_time = (G->header.flags1 & 0x02) != 0;
    
    if (status->is_time) {
        status->hours = status->score;
        status->minutes = status->turns;
    }
    
    /* Get current location name */
    /* In Z-machine Version 3, the player location is stored in global variable 0 (first global) */
    zWord player_location = (globals[0] << 8) | globals[1];
    
    if (player_location > 0 && player_location <= 255) {
        /* Get the object name for the current location */
        const zByte *location_name = z_get_obj_name(player_location);
        if (location_name) {
            /* Decode the ZSCII string to text */
            char temp_buf[64];
            memset(temp_buf, 0, sizeof(temp_buf)); /* Clear buffer first */
            zSize temp_len = sizeof(temp_buf) - 1; /* Leave room for null terminator */
            z_decode_zscii(location_name, 0, temp_buf, &temp_len);
            
            /* Ensure null termination */
            temp_buf[temp_len < sizeof(temp_buf) - 1 ? temp_len : sizeof(temp_buf) - 1] = '\0';
            
            /* Copy to status location field, ensuring null termination */
            strncpy(status->location, temp_buf, sizeof(status->location) - 1);
            status->location[sizeof(status->location) - 1] = '\0';
        } else {
            strcpy(status->location, "Unknown Location");
        }
    } else {
        /* Fallback for invalid location */
        strcpy(status->location, "Nowhere");
    }
    
    /* Format status string */
    if (status->is_time) {
        snprintf(status->status, sizeof(status->status), 
                "Time: %d:%02d", status->hours, status->minutes);
    } else {
        snprintf(status->status, sizeof(status->status), 
                "Score: %d     Moves: %d", status->score, status->turns);
    }
    
    return ZVIBE_OK;
}

#endif

#if ZVIBE_ENABLE_SAVE_RESTORE
zvibeResult zvibe_save_completed(zvibeContext *ctx, int success) {
    if (!ctx) return ZVIBE_ERROR;
    
    zState *G = &ctx->z_state;
    if (!G->save_requested) return ZVIBE_ERROR;
    
    G->save_requested = 0;
    z_do_branch(success ? 1 : 0);
    return ZVIBE_OK;
}

zvibeResult zvibe_restore_completed(zvibeContext *ctx, int success) {
    if (!ctx) return ZVIBE_ERROR;
    
    zState *G = &ctx->z_state;
    if (!G->restore_requested) return ZVIBE_ERROR;
    
    G->restore_requested = 0;
    z_do_branch(success ? 1 : 0);
    return ZVIBE_OK;
}

zvibeResult zvibe_restore_data(zvibeContext *ctx, const void *data, size_t length) {
    if (!ctx || !data) return ZVIBE_ERROR;
    
    /* Use shared restore function */
    int result = z_restore_save_data(data, length);
    
    return (result == 0) ? ZVIBE_OK : ZVIBE_ERROR;
}

zvibeResult zvibe_restart_completed(zvibeContext *ctx) {
    if (!ctx) return ZVIBE_ERROR;

    zState *G = &ctx->z_state;
    if (!G->restart_requested) return ZVIBE_ERROR;

    G->restart_requested = 0;

    /* Restore dynamic memory from original story data */
    zmem_state_t *mem = &G->memory_state;
    if (mem->flash_data && mem->ram_buffer && mem->ram_size > 0) {
        memcpy(mem->ram_buffer, mem->flash_data, mem->ram_size);
        zmem_write_byte(mem, 1, (zmem_byte_t)(G->header.flags1 | (1U << 4)));
    }

    /* Reset interpreter state */
    G->logical_pc = G->header.pc_start;
    G->quit_flag = 0;
    G->instr_count = 0;
    G->input_requested = 0;
    G->save_requested = 0;
    G->restore_requested = 0;
    memset(G->stack_mem, 0, sizeof(G->stack_mem));
    G->stack_ptr = G->stack_mem;
    G->base_ptr = 0;

    return ZVIBE_OK;
}

size_t zvibe_get_save_size(zvibeContext *ctx) {
    if (!ctx) return 0;
    
    return z_get_save_data_size();
}

size_t zvibe_get_save_data(zvibeContext *ctx, void *buffer, size_t buffer_size) {
    if (!ctx || !buffer || buffer_size == 0) return 0;

    return z_get_save_data(buffer, buffer_size);
}
#endif
