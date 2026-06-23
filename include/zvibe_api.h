/*
 * Copyright (c) 2025 Martin R. Raumann
 * SPDX-License-Identifier: BSD-3-Clause
 */
/**
 * @file zvibe_api.h
 * @brief Public API for ZVibe Z-machine interpreter
 * @ingroup PublicAPI
 *
 * Explicit request/response API for embedding the Z-machine interpreter in
 * desktop and embedded applications. Supports conditional compilation for
 * optional file I/O, save/restore, and status-line features.
 *
 * **Embedded Constraints:**
 * - RAM: Context uses static buffers (16KB default, configurable)
 * - No malloc: All allocations are static
 * - Thread safety: Not thread-safe, external synchronization required
 *
 * **Typical Usage:**
 * 1. zvibe_create() with an output callback
 * 2. zvibe_load_story() or zvibe_load_story_from_memory()
 * 3. Loop on zvibe_run()
 * 4. When ZVIBE_WAIT_FOR_INPUT is returned, gather input and call zvibe_input()
 * 5. When save/restore/restart is requested, complete it explicitly
 * 6. zvibe_destroy() when done
 */

#ifndef ZVIBE_API_H
#define ZVIBE_API_H

#include <stdint.h>
#include <stddef.h>

/*
 * Compile-time feature flags:
 *
 * ZVIBE_MINIMAL_FEATURES - Legacy convenience switch for embedded builds
 *   Sets save/restore, status line, and file I/O defaults to 0
 *
 * ZVIBE_FREESTANDING - Select freestanding fatal-error handling
 *   Default: 0 (hosted) unless ZVIBE_MINIMAL_FEATURES defined
 *
 * ZVIBE_ENABLE_DIAGNOSTICS - Enable internal stdio-based diagnostics
 *   Defined in zvibe_memory.h and also used by the API fatal-error path
 *
 * ZVIBE_ENABLE_SAVE_RESTORE - Enable save/restore functionality
 *   Default: 1 (enabled) unless ZVIBE_MINIMAL_FEATURES defined
 *
 * ZVIBE_ENABLE_STATUS_LINE - Enable status line updates
 *   Default: 1 (enabled) unless ZVIBE_MINIMAL_FEATURES defined
 *
 * ZVIBE_ENABLE_FILE_IO - Enable zvibe_load_story() filesystem helper
 *   Default: 1 (enabled) unless ZVIBE_MINIMAL_FEATURES defined
 */
#ifndef ZVIBE_MINIMAL_FEATURES
  #ifndef ZVIBE_FREESTANDING
    /** @brief Use hosted fatal-error handling (default: 0) */
    #define ZVIBE_FREESTANDING 0
  #endif
  #ifndef ZVIBE_ENABLE_FILE_IO
    /** @brief Enable filesystem story loading (default: 1) */
    #define ZVIBE_ENABLE_FILE_IO 1
  #endif
  #ifndef ZVIBE_ENABLE_SAVE_RESTORE
    /** @brief Enable save/restore functionality (default: 1) */
    #define ZVIBE_ENABLE_SAVE_RESTORE 1
  #endif
  #ifndef ZVIBE_ENABLE_STATUS_LINE
    /** @brief Enable status line updates (default: 1) */
    #define ZVIBE_ENABLE_STATUS_LINE 1
  #endif
#else
  #ifndef ZVIBE_FREESTANDING
    /** @brief Use freestanding fatal-error handling (minimal build: 1) */
    #define ZVIBE_FREESTANDING 1
  #endif
  #ifndef ZVIBE_ENABLE_FILE_IO
    /** @brief Enable filesystem story loading (minimal build: 0) */
    #define ZVIBE_ENABLE_FILE_IO 0
  #endif
  #ifndef ZVIBE_ENABLE_SAVE_RESTORE
    /** @brief Enable save/restore functionality (minimal build: 0) */
    #define ZVIBE_ENABLE_SAVE_RESTORE 0
  #endif
  #ifndef ZVIBE_ENABLE_STATUS_LINE
    /** @brief Enable status line updates (minimal build: 0) */
    #define ZVIBE_ENABLE_STATUS_LINE 0
  #endif
#endif

/** @brief Public API version string */
#define ZVIBE_VERSION "0.1.0"

/**
 * @brief Result codes for API functions
 */
typedef enum {
    ZVIBE_OK = 0,               /**< Operation successful */
    ZVIBE_ERROR,                /**< Operation failed */
    ZVIBE_WAIT_FOR_INPUT,       /**< Waiting for user input via zvibe_input() */
    ZVIBE_GAME_FINISHED,        /**< Game ended (QUIT opcode executed) */
#if ZVIBE_ENABLE_SAVE_RESTORE
    ZVIBE_SAVE_REQUESTED,       /**< Save requested, handle and resume with zvibe_save_completed() */
    ZVIBE_RESTORE_REQUESTED,    /**< Restore requested, handle and resume with zvibe_restore_completed() */
    ZVIBE_RESTART_REQUESTED     /**< Restart requested, handle and resume with zvibe_restart_completed() */
#endif
} zvibeResult;

/**
 * @brief Opaque Z-machine context
 *
 * Created by zvibe_create(), destroyed by zvibe_destroy().
 * Contains all interpreter state including memory and stack.
 */
typedef struct zvibeContext_s zvibeContext;

/**
 * @brief Text output callback
 *
 * Called when the Z-machine produces text output. Text is ZSCII-encoded
 * (ASCII subset plus extended characters).
 *
 * @param[in] text   Output text (not null-terminated, may contain embedded nulls)
 * @param[in] length Number of bytes in text
 */
typedef void (*zvibeOutputFunc)(const char *text, size_t length);

#if ZVIBE_ENABLE_STATUS_LINE
/**
 * @brief Status line information
 *
 * Queried from the current game state when the interpreter reaches an
 * input boundary. Format depends on game:
 * - Score/turns mode: is_time=0, score/turns valid
 * - Time mode: is_time=1, hours/minutes valid
 */
typedef struct {
    char location[80];  /**< Current location name (null-terminated) */
    char status[80];    /**< Status text (null-terminated) */
    int is_time;        /**< 0=score/turns, 1=time */
    int score;          /**< Current score (if !is_time) */
    int turns;          /**< Turn count (if !is_time) */
    int hours;          /**< Hours (if is_time) */
    int minutes;        /**< Minutes (if is_time) */
} zvibeStatus;

/**
 * @brief Status line update callback
 *
 * Called when status line changes. Application should update display.
 *
 * @param[in] status Pointer to current status (valid until next update)
 */
typedef void (*zvibeStatusFunc)(const zvibeStatus *status);
#endif

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Create a Z-machine context
 *
 * Allocates and initializes interpreter state using static buffers.
 * Must be called before any other API functions.
 *
 * @param[in] output_func Text output callback (must not be NULL)
 *
 * @return Pointer to context on success, NULL on failure
 *
 * @post Context is ready for zvibe_load_story() or zvibe_load_story_from_memory()
 *
 * @note Not thread-safe, context assumes single-threaded access
 * @see zvibe_destroy()
 */
zvibeContext* zvibe_create(zvibeOutputFunc output_func);

/**
 * @brief Destroy Z-machine context
 *
 * Frees resources and invalidates context. Context pointer must not be
 * used after this call.
 *
 * @param[in] ctx Context to destroy (may be NULL, no-op if NULL)
 *
 * @post ctx is invalid and must not be used
 *
 * @see zvibe_create()
 */
void zvibe_destroy(zvibeContext *ctx);

/**
 * @brief Load story file from filesystem
 *
 * Reads Z3 story file from disk into a static buffer. Disable with
 * `ZVIBE_ENABLE_FILE_IO=0` for embedded builds that provide story data
 * from memory instead.
 *
 * @param[in] ctx      Context (must not be NULL)
 * @param[in] filename Path to .z3 story file (must not be NULL)
 *
 * @return ZVIBE_OK on success, ZVIBE_ERROR on failure
 *
 * @pre ctx initialized via zvibe_create()
 * @post Story loaded and ready for zvibe_run()
 *
 * @note Desktop/server builds only (requires filesystem access)
 * @note Maximum file size: 128KB (Z-machine V3 limit)
 */
#if ZVIBE_ENABLE_FILE_IO
zvibeResult zvibe_load_story(zvibeContext *ctx, const char *filename);
#endif

/**
 * @brief Load story from memory buffer
 *
 * Uses provided memory buffer as story data. Buffer must remain valid
 * for lifetime of context (until zvibe_destroy()).
 *
 * @param[in] ctx  Context (must not be NULL)
 * @param[in] data Story file data (must not be NULL, must remain valid)
 * @param[in] size Story file size in bytes (must be > 64 and <= 128KB)
 *
 * @return ZVIBE_OK on success, ZVIBE_ERROR on failure
 *
 * @pre ctx initialized via zvibe_create()
 * @pre data contains valid Z3 story file
 * @post Story loaded and ready for zvibe_run()
 *
 * @warning data buffer must remain valid until zvibe_destroy()
 * @note Preferred method for embedded targets (no file I/O required)
 */
zvibeResult zvibe_load_story_from_memory(zvibeContext *ctx, const uint8_t *data, size_t size);

/**
 * @brief Run interpreter until input needed or game ends
 *
 * Executes Z-machine instructions until:
 * - Input needed (READ opcode): returns ZVIBE_WAIT_FOR_INPUT
 * - Game ended (QUIT opcode): returns ZVIBE_GAME_FINISHED
 * - Save/restore requested (if enabled): returns ZVIBE_SAVE_REQUESTED, etc.
 * - Error: returns ZVIBE_ERROR
 *
 * @param[in] ctx Context (must not be NULL)
 *
 * @return Result code indicating next required action
 *
 * @pre Story loaded via zvibe_load_story() or zvibe_load_story_from_memory()
 * @post Context in consistent state, ready for next action
 *
 * @note May invoke output_func callback during execution
 * @note May invoke status_func callback if status line enabled
 */
zvibeResult zvibe_run(zvibeContext *ctx);

/**
 * @brief Provide user input to interpreter
 *
 * Call after zvibe_run() returns ZVIBE_WAIT_FOR_INPUT. Provides input
 * text to pending READ opcode and resumes execution.
 *
 * @param[in] ctx   Context (must not be NULL)
 * @param[in] input Input text (null-terminated, must not be NULL)
 *
 * @return ZVIBE_OK on success, ZVIBE_ERROR on failure
 *
 * @pre zvibe_run() returned ZVIBE_WAIT_FOR_INPUT
 * @post Input buffered, call zvibe_run() to resume execution
 *
 * @note Input is copied internally, buffer need not remain valid
 */
zvibeResult zvibe_input(zvibeContext *ctx, const char *input);

/**
 * @brief Seed the random number generator
 *
 * Seeds the Z-machine's LCG before story execution begins.
 * Call after zvibe_create() and before zvibe_run().
 *
 * @param[in] ctx  Context (must not be NULL)
 * @param[in] seed Seed value; 0 seeds from system time
 */
void zvibe_set_random_seed(zvibeContext *ctx, int seed);

#if ZVIBE_ENABLE_STATUS_LINE
/**
 * @brief Set status line update callback
 *
 * Register callback to receive status line updates. The callback is
 * evaluated at input boundaries and is only invoked when the status
 * contents change.
 *
 * @param[in] ctx         Context (must not be NULL)
 * @param[in] status_func Callback function (NULL to disable)
 *
 * @return ZVIBE_OK on success, ZVIBE_ERROR on failure
 *
 * @note Callback may be invoked during zvibe_run()
 */
zvibeResult zvibe_set_status_callback(zvibeContext *ctx, zvibeStatusFunc status_func);

/**
 * @brief Get current status line data
 *
 * Query current status without waiting for callback. Useful for
 * initial display or polling.
 *
 * @param[in]  ctx    Context (must not be NULL)
 * @param[out] status Buffer to receive status (must not be NULL)
 *
 * @return ZVIBE_OK on success, ZVIBE_ERROR on failure
 *
 * @post status contains current status line data
 */
zvibeResult zvibe_get_status(zvibeContext *ctx, zvibeStatus *status);
#endif

#if ZVIBE_ENABLE_SAVE_RESTORE
/**
 * @brief Complete save operation
 *
 * Call after handling ZVIBE_SAVE_REQUESTED to resume execution.
 *
 * @param[in] ctx     Context (must not be NULL)
 * @param[in] success 1 if save succeeded, 0 if failed
 *
 * @return ZVIBE_OK on success, ZVIBE_ERROR on failure
 *
 * @pre zvibe_run() returned ZVIBE_SAVE_REQUESTED
 * @post Game informed of save result, call zvibe_run() to continue
 */
zvibeResult zvibe_save_completed(zvibeContext *ctx, int success);

/**
 * @brief Complete restore operation
 *
 * Call after handling ZVIBE_RESTORE_REQUESTED to resume execution.
 *
 * @param[in] ctx     Context (must not be NULL)
 * @param[in] success 1 if restore succeeded, 0 if failed
 *
 * @return ZVIBE_OK on success, ZVIBE_ERROR on failure
 *
 * @pre zvibe_run() returned ZVIBE_RESTORE_REQUESTED
 * @post Game informed of restore result, call zvibe_run() to continue
 */
zvibeResult zvibe_restore_completed(zvibeContext *ctx, int success);

/**
 * @brief Complete restart operation
 *
 * Call after handling ZVIBE_RESTART_REQUESTED to reload game state.
 *
 * @param[in] ctx Context (must not be NULL)
 *
 * @return ZVIBE_OK on success, ZVIBE_ERROR on failure
 *
 * @pre zvibe_run() returned ZVIBE_RESTART_REQUESTED
 * @post Game state reset to initial conditions, call zvibe_run() to continue
 */
zvibeResult zvibe_restart_completed(zvibeContext *ctx);

/**
 * @brief Get required save data buffer size
 *
 * Query size needed for save data buffer before calling zvibe_get_save_data().
 *
 * @param[in] ctx Context (must not be NULL)
 *
 * @return Size in bytes, 0 on error
 *
 * @note Size varies by game (2-14KB for known V3 games)
 */
size_t zvibe_get_save_size(zvibeContext *ctx);

/**
 * @brief Get save data for persistence
 *
 * Extract current game state into a caller-provided buffer for the
 * host application's persistence layer.
 *
 * @param[in]  ctx         Context (must not be NULL)
 * @param[out] buffer      Buffer to store save data (must not be NULL)
 * @param[in]  buffer_size Size of buffer (should be >= zvibe_get_save_size())
 *
 * @return Number of bytes written, 0 on error
 *
 * @pre buffer_size >= zvibe_get_save_size(ctx)
 * @post buffer contains binary save data
 *
 * @warning Save data format is a native-endian binary blob and is only
 * portable across builds that share the same ABI and story file
 */
size_t zvibe_get_save_data(zvibeContext *ctx, void *buffer, size_t buffer_size);

/**
 * @brief Restore game state from save data
 *
 * Load previously saved game state from the host application's
 * persistence layer.
 *
 * @param[in] ctx    Context (must not be NULL)
 * @param[in] data   Save data buffer (must not be NULL)
 * @param[in] length Size of save data in bytes
 *
 * @return ZVIBE_OK on success, ZVIBE_ERROR on failure
 *
 * @pre data contains valid save data from zvibe_get_save_data()
 * @post Game state restored to saved point
 *
 * @warning Save data must be from the same story file and compatible build ABI
 */
zvibeResult zvibe_restore_data(zvibeContext *ctx, const void *data, size_t length);
#endif

#ifdef __cplusplus
}
#endif


#endif /* ZVIBE_API_H */
