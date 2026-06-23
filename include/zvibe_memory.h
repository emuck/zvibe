/*
 * Copyright (c) 2025 Martin R. Raumann
 * SPDX-License-Identifier: BSD-3-Clause
 */
/**
 * @file zvibe_memory.h
 * @brief Split memory architecture for embedded systems
 * @ingroup MemoryAbstraction
 *
 * Implements split memory model separating dynamic (RAM) and static (flash)
 * regions. This reduces RAM requirements by 85-95% compared to loading the
 * entire story file into RAM.
 *
 * **Memory Regions:**
 * - Dynamic: 0x0 to staticmem_addr-1 (writable, in RAM)
 * - Static: staticmem_addr to story_size-1 (read-only, in flash/ROM)
 * **Embedded Constraints:**
 * - RAM: Dynamic memory (2-14KB for known V3 games)
 * - No malloc: Uses caller-provided static buffers
 * - Thread safety: Not thread-safe, external synchronization required
 * - Alignment: Word operations require even addresses
 *
 * **Implementation Notes:**
 * - Big-endian word access (Z-machine spec)
 * - Bounds checking configurable via state->bounds_checking
 * - Write protection enforced on static memory region
 * - Memory helpers report failures via return codes; callers must check them
 */

#ifndef ZVIBE_MEMORY_H
#define ZVIBE_MEMORY_H

#include <stdint.h>
#include <stddef.h>

/* -------------------- CONFIGURATION -------------------- */

/*
 * ZVIBE_ENABLE_DIAGNOSTICS - Enable internal stdio-based diagnostics
 *
 * Default: 0
 * When enabled, internal memory/core helpers may print debug or error output
 * using stdio. Keep disabled for size-sensitive embedded builds.
 */
#ifndef ZVIBE_ENABLE_DIAGNOSTICS
#define ZVIBE_ENABLE_DIAGNOSTICS 0
#endif

/**
 * @def ZMEM_SUCCESS
 * @brief Operation successful
 */
#define ZMEM_SUCCESS           0

/**
 * @def ZMEM_ERROR_NULL_PTR
 * @brief NULL pointer argument
 */
#define ZMEM_ERROR_NULL_PTR    1

/**
 * @def ZMEM_ERROR_BAD_SIZE
 * @brief Invalid size parameter
 */
#define ZMEM_ERROR_BAD_SIZE    2

/**
 * @def ZMEM_ERROR_OUT_OF_BOUNDS
 * @brief Address out of valid range
 */
#define ZMEM_ERROR_OUT_OF_BOUNDS 3

/**
 * @def ZMEM_ERROR_WRITE_PROTECTED
 * @brief Attempted write to read-only memory
 */
#define ZMEM_ERROR_WRITE_PROTECTED 4

/**
 * @def ZMEM_ERROR_ALIGNMENT
 * @brief Misaligned word access
 */
#define ZMEM_ERROR_ALIGNMENT   5

/* -------------------- TYPES -------------------- */

/** @brief Unsigned 8-bit byte */
typedef uint8_t zmem_byte_t;

/** @brief Unsigned 16-bit word (big-endian in memory) */
typedef uint16_t zmem_word_t;

/** @brief 32-bit memory address */
typedef uint32_t zmem_addr_t;

/**
 * @brief Memory configuration parameters
 *
 * Defines memory layout boundaries. Typically extracted from Z-machine
 * story file header.
 */
typedef struct {
    zmem_addr_t staticmem_addr;     /**< Boundary between dynamic and static (byte address) */
    zmem_addr_t story_size;         /**< Total story size in bytes */
} zmem_config_t;

/**
 * @brief Split memory manager state
 *
 * Contains all state for split memory architecture. Opaque to application,
 * initialized via zmem_init() or zmem_init_embedded().
 *
 * **Ownership:** Caller owns structure, but not internal buffers (static)
 * **Lifetime:** Valid from zmem_init() until zmem_cleanup()
 */
typedef struct {
    /* Configuration */
    zmem_config_t config;           /**< Memory layout configuration */

    /* Dynamic memory (RAM) */
    zmem_byte_t *ram_buffer;        /**< Dynamic memory buffer (caller-provided or allocated) */
    zmem_addr_t ram_size;           /**< Size of RAM buffer in bytes */
    int owns_ram_buffer;            /**< 1 if ram_buffer was heap-allocated by zmem_init() */

    /* Static memory (flash/mmap) */
    const zmem_byte_t *flash_data;  /**< Pointer to static memory (read-only) */
    zmem_addr_t flash_size;         /**< Size of static memory in bytes */

    /* Validation flags */
    int bounds_checking;            /**< Enable bounds checking (0=disabled, 1=enabled) */
    int debug_mode;                 /**< Enable debug messages (0=disabled, 1=enabled) */
} zmem_state_t;

/* -------------------- CORE FUNCTIONS -------------------- */

/**
 * @brief Initialize split memory system with dynamic allocation
 *
 * Allocates RAM and stack buffers via malloc. For embedded systems without
 * malloc, use zmem_init_embedded() instead.
 *
 * @param[out] state      Memory state to initialize (must not be NULL)
 * @param[in]  config     Memory configuration (must not be NULL)
 * @param[in]  story_data Pointer to story file data (must not be NULL, must remain valid)
 * @param[in]  story_size Size of story data in bytes
 *
 * @return Error code:
 *         - ZMEM_SUCCESS on success
 *         - ZMEM_ERROR_NULL_PTR if state, config, or story_data is NULL
 *         - ZMEM_ERROR_BAD_SIZE if story_size invalid
 *
 * @post state initialized and ready for memory operations
 * @post RAM and stack buffers allocated via malloc
 *
 * @warning story_data must remain valid until zmem_cleanup()
 * @note Desktop/test builds only (requires malloc)
 * @see zmem_init_embedded(), zmem_cleanup()
 */
int zmem_init(zmem_state_t *state, const zmem_config_t *config,
              const void *story_data, size_t story_size);

/**
 * @brief Cleanup split memory system
 *
 * Frees allocated buffers (if zmem_init() was used). Does not free
 * caller-provided buffers (if zmem_init_embedded() was used).
 *
 * @param[in,out] state Memory state to cleanup (may be NULL, no-op if NULL)
 *
 * @post state is invalid and must not be used
 * @post Allocated buffers freed (if any)
 *
 * @see zmem_init(), zmem_init_embedded()
 */
void zmem_cleanup(zmem_state_t *state);

/**
 * @brief Read a byte from memory
 *
 * Routes read to RAM or flash based on address. Performs bounds checking
 * if state->bounds_checking enabled.
 *
 * @param[in]  state Memory state (must not be NULL)
 * @param[in]  addr  Address to read (must be < story_size)
 * @param[out] value Pointer to store byte value (must not be NULL)
 *
 * @return Error code:
 *         - ZMEM_SUCCESS on success
 *         - ZMEM_ERROR_NULL_PTR if state or value is NULL
 *         - ZMEM_ERROR_OUT_OF_BOUNDS if addr >= story_size
 *
 * @post value contains byte at addr (if successful)
 *
 * @warning No thread safety; external synchronization required
 */
int zmem_read_byte(const zmem_state_t *state, zmem_addr_t addr, zmem_byte_t *value);

/**
 * @brief Read a word from memory
 *
 * Reads 16-bit word in big-endian format. Routes read to RAM or flash
 * based on address.
 *
 * @param[in]  state Memory state (must not be NULL)
 * @param[in]  addr  Address to read (must be < story_size - 1)
 * @param[out] value Pointer to store word value (must not be NULL)
 *
 * @return Error code:
 *         - ZMEM_SUCCESS on success
 *         - ZMEM_ERROR_NULL_PTR if state or value is NULL
 *         - ZMEM_ERROR_OUT_OF_BOUNDS if addr invalid
 *
 * @post value contains big-endian word converted to host byte order
 *
 * @note Word at addr is stored as [high_byte][low_byte] in memory
 * @warning No thread safety; external synchronization required
 */
int zmem_read_word(const zmem_state_t *state, zmem_addr_t addr, zmem_word_t *value);

/**
 * @brief Write a byte to memory
 *
 * Writes to dynamic memory (RAM) only. Writes to static memory region
 * return ZMEM_ERROR_WRITE_PROTECTED.
 *
 * @param[in,out] state Memory state (must not be NULL)
 * @param[in]     addr  Address to write (must be < staticmem_addr)
 * @param[in]     value Byte value to write
 *
 * @return Error code:
 *         - ZMEM_SUCCESS on success
 *         - ZMEM_ERROR_NULL_PTR if state is NULL
 *         - ZMEM_ERROR_OUT_OF_BOUNDS if addr >= story_size
 *         - ZMEM_ERROR_WRITE_PROTECTED if addr >= staticmem_addr
 *
 * @post RAM updated at addr (if successful)
 *
 * @warning No thread safety; external synchronization required
 */
int zmem_write_byte(zmem_state_t *state, zmem_addr_t addr, zmem_byte_t value);

/**
 * @brief Write a word to memory
 *
 * Writes 16-bit word in big-endian format to dynamic memory (RAM) only.
 *
 * @param[in,out] state Memory state (must not be NULL)
 * @param[in]     addr  Address to write (must be < staticmem_addr - 1)
 * @param[in]     value Word value to write (converted to big-endian)
 *
 * @return Error code:
 *         - ZMEM_SUCCESS on success
 *         - ZMEM_ERROR_NULL_PTR if state is NULL
 *         - ZMEM_ERROR_OUT_OF_BOUNDS if addr invalid
 *         - ZMEM_ERROR_WRITE_PROTECTED if addr >= staticmem_addr
 *
 * @post RAM updated with [high_byte][low_byte] at addr (if successful)
 *
 * @note Converts host byte order to big-endian before writing
 * @warning No thread safety; external synchronization required
 */
int zmem_write_word(zmem_state_t *state, zmem_addr_t addr, zmem_word_t value);

/**
 * @brief Get direct pointer to memory for legacy code
 *
 * Provides direct pointer access for compatibility with existing code.
 * Prefer zmem_read_byte() / zmem_write_byte() for new code.
 *
 * @param[in] state     Memory state (must not be NULL)
 * @param[in] addr      Address to get pointer for
 * @param[in] for_write 1 if pointer will be used for writing, 0 for reading
 *
 * @return Pointer to memory at addr, NULL on error
 *
 * @warning Returned pointer may be invalidated by other memory operations
 * @warning for_write=1 for static memory region returns NULL
 * @warning No bounds checking on pointer usage after return
 *
 * @note Prefer zmem_read_byte/zmem_write_byte for safer access
 */
zmem_byte_t *zmem_get_ptr(const zmem_state_t *state, zmem_addr_t addr, int for_write);

/**
 * @brief Copy block of memory
 *
 * Copies bytes from src to dst within story memory. Handles overlapping
 * regions correctly. Destination must be in writable (dynamic) region.
 *
 * @param[in,out] state    Memory state (must not be NULL)
 * @param[in]     dst_addr Destination address (must be < staticmem_addr)
 * @param[in]     src_addr Source address (may be in RAM or flash)
 * @param[in]     size     Number of bytes to copy
 *
 * @return Error code:
 *         - ZMEM_SUCCESS on success
 *         - ZMEM_ERROR_NULL_PTR if state is NULL
 *         - ZMEM_ERROR_OUT_OF_BOUNDS if addresses invalid
 *         - ZMEM_ERROR_WRITE_PROTECTED if dst in static region
 *
 * @post size bytes copied from src_addr to dst_addr
 *
 * @note Handles overlapping regions (uses memmove semantics)
 */
int zmem_copy_block(zmem_state_t *state, zmem_addr_t dst_addr,
                    zmem_addr_t src_addr, size_t size);

/* -------------------- VALIDATION FUNCTIONS -------------------- */

/**
 * @brief Validate memory address
 *
 * Checks if address is within valid range and writable (if for_write=1).
 *
 * @param[in] state     Memory state (must not be NULL)
 * @param[in] addr      Address to validate
 * @param[in] for_write 1 if validating for write access, 0 for read
 *
 * @return Error code:
 *         - ZMEM_SUCCESS if address valid
 *         - ZMEM_ERROR_NULL_PTR if state is NULL
 *         - ZMEM_ERROR_OUT_OF_BOUNDS if addr >= story_size
 *         - ZMEM_ERROR_WRITE_PROTECTED if for_write and addr in static region
 */
int zmem_validate_addr(const zmem_state_t *state, zmem_addr_t addr, int for_write);

/**
 * @brief Check if address is in dynamic (RAM) region
 *
 * @param[in] state Memory state (must not be NULL)
 * @param[in] addr  Address to check
 *
 * @return 1 if addr < staticmem_addr (in RAM), 0 otherwise (in flash)
 */
int zmem_is_ram_addr(const zmem_state_t *state, zmem_addr_t addr);

/**
 * @brief Get memory usage statistics
 *
 * Reports current RAM and stack usage for debugging and profiling.
 *
 * @param[in]  state      Memory state (must not be NULL)
 * @param[out] ram_used   RAM bytes used (may be NULL to skip)
 */
void zmem_get_stats(const zmem_state_t *state, size_t *ram_used);

/**
 * @brief Initialize split memory system with static buffers
 *
 * For embedded systems without malloc. Caller provides pre-allocated
 * RAM and stack buffers. Buffers must remain valid until zmem_cleanup().
 *
 * @param[out] state        Memory state to initialize (must not be NULL)
 * @param[in]  config       Memory configuration (must not be NULL)
 * @param[in]  story_data   Pointer to story file (must not be NULL, must remain valid)
 * @param[in]  story_size   Size of story data in bytes
 * @param[in]  ram_buffer   Pre-allocated RAM buffer (must be >= staticmem_addr bytes)
 *
 * @return Error code:
 *         - ZMEM_SUCCESS on success
 *         - ZMEM_ERROR_NULL_PTR if any pointer is NULL
 *         - ZMEM_ERROR_BAD_SIZE if story_size or buffer sizes invalid
 *
 * @post state initialized and ready for memory operations
 * @post Uses caller-provided buffers (no malloc)
 *
 * @warning story_data and ram_buffer must remain valid until zmem_cleanup()
 * @note zmem_cleanup() does not free caller-provided ram_buffer
 * @note Embedded builds only (no malloc required)
 * @see zmem_init(), zmem_cleanup()
 */
int zmem_init_embedded(zmem_state_t *state, const zmem_config_t *config,
                       const void *story_data, size_t story_size,
                       uint8_t *ram_buffer);


#endif /* ZVIBE_MEMORY_H */
