/*
 * Derived from mojozork by Ryan C. Gordon
 * https://github.com/icculus/mojozork
 * Copyright (c) 2015-2023 Ryan C. Gordon
 * Copyright (c) 2025 Martin R. Raumann
 * SPDX-License-Identifier: BSD-3-Clause
 */
/**
 * @file zvibe.h
 * @brief Internal types and functions for Z-machine core interpreter
 * @ingroup CoreInterpreter
 *
 * Core Z-machine Version 3 interpreter implementation. This header defines
 * internal types, structures, and functions. Applications should use
 * zvibe_api.h instead of this header.
 *
 * **Memory Model:**
 * - Static allocation only (no malloc at runtime)
 * - Split memory architecture (RAM + flash)
 * - Shared buffer for dynamic memory and stack
 *
 * **Embedded Constraints:**
 * - RAM: 16KB default shared buffer (configurable via ZVIBE_MEMORY_BUFFER_SIZE)
 * - Stack: Grows upward from end of dynamic memory
 * - No malloc: All allocations static
 * - Thread safety: Not thread-safe, single-threaded only
 *
 * **Implementation Notes:**
 * - Z-machine Version 3 only (no V4/V5/etc. support)
 * - Big-endian architecture (Z-machine spec)
 * - Global state via G pointer (simplifies embedded code)
 */

#ifndef ZVIBE_H
#define ZVIBE_H

#include <stddef.h>
#include <stdint.h>
#include "zvibe_memory.h"

/* -------------------- BASIC TYPE DEFINITIONS -------------------- */

/** @brief Unsigned 8-bit byte */
typedef uint8_t zByte;

/** @brief Signed 8-bit byte */
typedef int8_t zSByte;

/** @brief Unsigned 16-bit word (big-endian in memory) */
typedef uint16_t zWord;

/** @brief Signed 16-bit word (big-endian in memory) */
typedef int16_t zSWord;

/** @brief Unsigned 32-bit double word */
typedef uint32_t zDWord;

/** @brief Signed 32-bit double word */
typedef int32_t zSDWord;

/** @brief Size type for memory operations */
typedef size_t zSize;

/* -------------------- MEMORY CONFIGURATION -------------------- */

/**
 * @def Z3_STORY_MEM_SIZE
 * @brief Maximum story file size for bounds checking
 *
 * Z-machine V3 maximum: 128KB (per spec)
 */
#define Z3_STORY_MEM_SIZE   (128*1024)

/**
 * @def Z3_STACK_SIZE
 * @brief Minimum stack size in 16-bit words
 *
 * Stack size: 512 words = 1KB
 * Sufficient for typical V3 games (max call depth ~10-20)
 */
#define Z3_STACK_SIZE       512

/*
 * ZVIBE_MEMORY_BUFFER_SIZE - Shared buffer size for dynamic memory + stack
 *
 * Default: 16KB
 * Layout: [dynamic memory (0..staticmem_addr)][stack (grows upward)]
 * Stack space = buffer_size - staticmem_addr (typically 2-4KB for V3 games)
 *
 * Override at compile time: -DZVIBE_MEMORY_BUFFER_SIZE=...
 */
#ifndef ZVIBE_MEMORY_BUFFER_SIZE
/** @brief Shared buffer size for dynamic memory and stack (default: 16KB) */
#define ZVIBE_MEMORY_BUFFER_SIZE  (16*1024)
#endif

/**
 * @def Z3_MAX_OPERANDS
 * @brief Maximum operands per instruction (Z-machine V3 spec)
 */
#define Z3_MAX_OPERANDS     4

/**
 * @def Z3_ALPHABET_SIZE
 * @brief Alphabet table size (Z-machine V3 spec)
 *
 * 3 alphabets × 26 letters = 78 characters
 */
#define Z3_ALPHABET_SIZE    78

/**
 * @def Z3_VERSION
 * @brief Z-machine version supported by this implementation
 */
#define Z3_VERSION          3

/* -------------------- MEMORY ACCESS MACROS -------------------- */

/**
 * @def Z_READ8
 * @brief Read byte from pointer and advance
 *
 * @param ptr Pointer to byte (advanced after read)
 * @return Byte value
 *
 * @warning Advances pointer as side effect
 */
#define Z_READ8(ptr)       (*(ptr)++)

/**
 * @brief Read 16-bit big-endian word from pointer
 *
 * Uses memcpy to avoid alignment issues, then swaps bytes for big-endian.
 *
 * @param[in] p Pointer to 2-byte buffer
 * @return Word value in host byte order
 *
 * @note Handles unaligned pointers correctly
 */
static inline zWord z_read16_be_impl(const zByte *p) {
    zWord val;
    __builtin_memcpy(&val, p, 2);
    return ((val >> 8) & 0xFF) | ((val & 0xFF) << 8);  /* Swap bytes for big-endian */
}

/**
 * @def Z_READ16
 * @brief Read 16-bit big-endian word from pointer and advance
 *
 * @param ptr Pointer to word (advanced by 2 after read)
 * @return Word value in host byte order
 *
 * @warning Advances pointer by 2 as side effect
 * @note Handles unaligned pointers correctly
 */
#define Z_READ16(ptr)      (z_read16_be_impl(ptr), (ptr) += 2, z_read16_be_impl((ptr) - 2))

/**
 * @def Z_WRITE16
 * @brief Write 16-bit word to pointer in big-endian format and advance
 *
 * @param dst Pointer to destination (advanced by 2 after write)
 * @param val Word value to write (in host byte order)
 *
 * @warning Advances pointer by 2 as side effect
 * @note Writes [high_byte][low_byte] to memory
 */
#define Z_WRITE16(dst, val) { *(dst)++ = (zByte)(((val) >> 8) & 0xFF); *(dst)++ = (zByte)((val) & 0xFF); }

/* -------------------- STRUCTURES -------------------- */

/**
 * @brief Opcode handler function pointer
 *
 * All opcode handlers have this signature. They read operands from
 * G->operands[] and perform the specified operation.
 */
typedef void (*zOpcodeFn)(void);

/**
 * @brief Z-machine story file header (Version 3 format)
 *
 * Extracted from bytes 0-63 of story file. All multi-byte fields
 * are big-endian.
 *
 * @note Corresponds to Z-machine spec section 11
 */
typedef struct {
    zByte version;          /**< Z-machine version (always 3) */
    zByte flags1;           /**< Flags 1 (status line type, etc.) */
    zWord release;          /**< Release number */
    zWord himem_addr;       /**< High memory base address */
    zWord pc_start;         /**< Initial PC value */
    zWord dict_addr;        /**< Dictionary address */
    zWord objtab_addr;      /**< Object table address */
    zWord globals_addr;     /**< Global variables address */
    zWord staticmem_addr;   /**< Static memory base address */
    zWord flags2;           /**< Flags 2 */
    char serial[7];         /**< Serial number (6 chars + null terminator) */
    zWord abbrtab_addr;     /**< Abbreviations table address */
    zWord story_len;        /**< Story file length (in bytes / 2) */
    zWord story_checksum;   /**< Story file checksum */
} zHeader;

/**
 * @brief Complete Z-machine interpreter state
 *
 * Contains all state for a running Z-machine instance. Uses static
 * allocation only (no malloc at runtime).
 *
 * **Ownership:** Application owns via zvibeContext
 * **Lifetime:** Valid from z_init_machine() until context destroyed
 * **Thread safety:** Not thread-safe, single-threaded access only
 *
 * **Memory Layout:**
 * - memory_state: Split memory manager (RAM + flash access)
 * - stack_mem: 1KB stack (512 × 16-bit words)
 * - Total size: ~3.3KB static allocation
 */
typedef struct {
    /* Memory management */
    zmem_state_t memory_state;           /**< Split memory manager (RAM/flash) */
    zWord stack_mem[Z3_STACK_SIZE];      /**< Stack memory (512 words = 1KB) */

    /* Core execution state */
    zWord *stack_ptr;             /**< Current stack pointer */
    zWord base_ptr;               /**< Base pointer for current stack frame */
    zSize story_size;             /**< Story file size in bytes */
    zDWord logical_pc;            /**< Program counter (logical byte address) */
    zDWord instr_count;           /**< Instruction counter (for debugging) */

    /* Story file header */
    zHeader header;               /**< Parsed story file header (V3 format) */

    /* Instruction decoding */
    zWord operands[Z3_MAX_OPERANDS];  /**< Current instruction operands */
    zByte operand_count;          /**< Number of operands for current instruction */
    int quit_flag;                /**< Set by QUIT opcode */

    /* Input state (READ opcode) */
    int input_requested;          /**< 1 if waiting for input */
    zWord input_buffer_addr;      /**< Address of READ input buffer */
    zWord parse_buffer_addr;      /**< Address of READ parse buffer */
    zDWord saved_logical_pc;      /**< PC saved when READ executed */

    /* Save/restore state */
    int save_requested;           /**< 1 if SAVE opcode executed */
    int restore_requested;        /**< 1 if RESTORE opcode executed */
    int restart_requested;        /**< 1 if RESTART opcode executed */

    /* UI state */
    zWord current_window;         /**< Current output window (0=lower, 1=upper) */
    zWord upper_window_lines;     /**< Upper window height in lines */

    /* Platform callbacks */
    void (*split_window)(zWord oldval, zWord newval);  /**< Split window callback */
    void (*set_window)(zWord oldval, zWord newval);    /**< Set window callback */
    void (*writestr)(const char *str, zSize len);      /**< Text output callback */
    void (*die)(const char *fmt, ...);                 /**< Fatal error callback */
} zState;

/* -------------------- GLOBAL STATE -------------------- */

/**
 * @brief Global pointer to current Z-machine state
 *
 * Used by all interpreter functions for convenient access to state.
 * Set by zvibe_create() / z_init_machine().
 *
 * @warning Not thread-safe; assumes single interpreter instance
 */
extern zState *G;

/* -------------------- CORE FUNCTIONS -------------------- */

/** @brief Initialize a caller-provided Z-machine state structure */
void z_init_machine(zState *state);

/** @brief Load story file data into memory system */
int z_load_story_data(const void *data, size_t size);

/** @brief Execute one Z-machine instruction */
void z_run_instruction(void);

/** @brief Initialize opcode dispatch tables */
void init_opcodes(void);

/** @brief Get handler for a decoded opcode */
zOpcodeFn z_get_opcode_handler(zByte opcode);

/** @brief Print memory usage statistics (debug) */
void z_report_memory_usage(void);

/** @brief Read byte from PC and advance PC */
zByte z_read_pc_byte(void);

/** @brief Read word from PC and advance PC by 2 */
zWord z_read_pc_word(void);

/** @brief Get pointer to memory at offset (for legacy code) */
zByte *z_get_mem_ptr(zWord offset);

/**
 * @brief Unpack packed address to byte address
 *
 * V3: Packed address × 2 = byte address
 *
 * @param[in] addr Packed address
 * @return Pointer to memory at unpacked address
 */
zByte *z_unpack_addr(zDWord addr);

/**
 * @brief Get address of variable (stack/local/global)
 *
 * @param[in] var     Variable number (0=stack, 1-15=locals, 16+=globals)
 * @param[in] writing 1 if for write, 0 if for read
 * @return Pointer to variable storage
 */
zByte *z_var_addr(zByte var, int writing);

/** @brief Execute return from subroutine */
void z_do_return(zWord val);

/** @brief Execute conditional branch */
void z_do_branch(int truth);

/** @brief Print ZSCII-encoded string to output */
zSize z_print_zscii(const zByte *str, int abbr);

/** @brief Decode ZSCII string to buffer */
zSize z_decode_zscii(const zByte *str, int abbr, char *buf, zSize *buflen);

/** @brief Get pointer to object table entry */
zByte *z_get_obj_ptr(zWord objid);

/** @brief Get pointer to object property data */
zByte *z_get_obj_property(zWord objid, zDWord propid, zByte *size);

/** @brief Get pointer to object name (ZSCII-encoded) */
const zByte *z_get_obj_name(zWord objid);

/** @brief Get default property value */
zWord z_get_default_property(zWord propid);

/** @brief Process user input from READ opcode */
void z_process_input(const char *input_text);

/** @brief Set random number generator seed (0 = system time) */
void z_random_set_seed(zSDWord seed);

/** @brief Get required save data buffer size */
size_t z_get_save_data_size(void);

/** @brief Extract save data into buffer */
size_t z_get_save_data(void *buffer, size_t buffer_size);

/** @brief Restore interpreter state from save data */
int z_restore_save_data(const void *data, size_t length);

/* -------------------- OPCODE IMPLEMENTATIONS -------------------- */
/* See zvibe_ops_*.c for implementations */

/* 2-operand instructions (OpcodesBase, OpcodesMath, OpcodesMem, ObjectTable) */
/** @brief Jump if equal (variable operands) @ingroup OpcodesBase */
void op_je(void);
/** @brief Jump if less than (signed) @ingroup OpcodesMath */
void op_jl(void);
/** @brief Jump if greater than (signed) @ingroup OpcodesMath */
void op_jg(void);
/** @brief Decrement variable and branch if < value @ingroup OpcodesMath */
void op_dec_chk(void);
/** @brief Increment variable and branch if > value @ingroup OpcodesMath */
void op_inc_chk(void);
/** @brief Jump if object is child of parent @ingroup ObjectTable */
void op_jin(void);
/** @brief Test if bits set (bitwise AND) @ingroup OpcodesMath */
void op_test(void);
/** @brief Bitwise OR @ingroup OpcodesMath */
void op_or(void);
/** @brief Bitwise AND @ingroup OpcodesMath */
void op_and(void);
/** @brief Test object attribute @ingroup ObjectTable */
void op_test_attr(void);
/** @brief Set object attribute @ingroup ObjectTable */
void op_set_attr(void);
/** @brief Clear object attribute @ingroup ObjectTable */
void op_clear_attr(void);
/** @brief Store value to variable @ingroup OpcodesMem */
void op_store(void);
/** @brief Insert object into parent @ingroup ObjectTable */
void op_insert_obj(void);
/** @brief Load word from memory @ingroup OpcodesMem */
void op_loadw(void);
/** @brief Load byte from memory @ingroup OpcodesMem */
void op_loadb(void);
/** @brief Get object property value @ingroup ObjectTable */
void op_get_prop(void);
/** @brief Get object property address @ingroup ObjectTable */
void op_get_prop_addr(void);
/** @brief Get next object property number @ingroup ObjectTable */
void op_get_next_prop(void);
/** @brief Add (signed 16-bit) @ingroup OpcodesMath */
void op_add(void);
/** @brief Subtract (signed 16-bit) @ingroup OpcodesMath */
void op_sub(void);
/** @brief Multiply (signed 16-bit) @ingroup OpcodesMath */
void op_mul(void);
/** @brief Divide (signed 16-bit) @ingroup OpcodesMath */
void op_div(void);
/** @brief Modulo (signed 16-bit) @ingroup OpcodesMath */
void op_mod(void);

/* 1-operand instructions (OpcodesBase, OpcodesMem, OpcodesText, ObjectTable) */
/** @brief Jump if zero @ingroup OpcodesBase */
void op_jz(void);
/** @brief Get object sibling @ingroup ObjectTable */
void op_get_sibling(void);
/** @brief Get object child @ingroup ObjectTable */
void op_get_child(void);
/** @brief Get object parent @ingroup ObjectTable */
void op_get_parent(void);
/** @brief Get property length in bytes @ingroup ObjectTable */
void op_get_prop_len(void);
/** @brief Increment variable @ingroup OpcodesMath */
void op_inc(void);
/** @brief Decrement variable @ingroup OpcodesMath */
void op_dec(void);
/** @brief Print ZSCII string at address @ingroup OpcodesText */
void op_print_addr(void);
/** @brief Remove object from parent @ingroup ObjectTable */
void op_remove_obj(void);
/** @brief Print object name @ingroup OpcodesText */
void op_print_obj(void);
/** @brief Return from subroutine @ingroup OpcodesBase */
void op_ret(void);
/** @brief Unconditional jump (signed offset) @ingroup OpcodesBase */
void op_jump(void);
/** @brief Print ZSCII string at packed address @ingroup OpcodesText */
void op_print_paddr(void);
/** @brief Load variable value @ingroup OpcodesMem */
void op_load(void);
/** @brief Bitwise NOT @ingroup OpcodesMath */
void op_not(void);

/* 0-operand instructions (OpcodesBase, OpcodesText) */
/** @brief Return true (1) @ingroup OpcodesBase */
void op_rtrue(void);
/** @brief Return false (0) @ingroup OpcodesBase */
void op_rfalse(void);
/** @brief Print inline string @ingroup OpcodesText */
void op_print(void);
/** @brief Print inline string and return true @ingroup OpcodesText */
void op_print_ret(void);
/** @brief No operation @ingroup OpcodesBase */
void op_nop(void);
/** @brief Save game state @ingroup OpcodesBase */
void op_save(void);
/** @brief Restore game state @ingroup OpcodesBase */
void op_restore(void);
/** @brief Restart game @ingroup OpcodesBase */
void op_restart(void);
/** @brief Return with value popped from stack @ingroup OpcodesBase */
void op_ret_popped(void);
/** @brief Pop value from stack (discard) @ingroup OpcodesBase */
void op_pop(void);
/** @brief Quit game @ingroup OpcodesBase */
void op_quit(void);
/** @brief Print newline @ingroup OpcodesText */
void op_new_line(void);
/** @brief Show status line @ingroup OpcodesText */
void op_show_status(void);
/** @brief Verify story file checksum @ingroup OpcodesBase */
void op_verify(void);

/* Variable operand instructions (OpcodesBase, OpcodesMem, OpcodesText, ObjectTable) */
/** @brief Call subroutine @ingroup OpcodesBase */
void op_call(void);
/** @brief Store word to memory @ingroup OpcodesMem */
void op_storew(void);
/** @brief Store byte to memory @ingroup OpcodesMem */
void op_storeb(void);
/** @brief Set object property value @ingroup ObjectTable */
void op_put_prop(void);
/** @brief Read user input @ingroup OpcodesText */
void op_read(void);
/** @brief Print ZSCII character @ingroup OpcodesText */
void op_print_char(void);
/** @brief Print signed number @ingroup OpcodesText */
void op_print_num(void);
/** @brief Generate random number @ingroup OpcodesMath */
void op_random(void);
/** @brief Push value onto stack @ingroup OpcodesBase */
void op_push(void);
/** @brief Pull value from stack (V6: store to variable) @ingroup OpcodesBase */
void op_pull(void);
/** @brief Split screen into windows @ingroup OpcodesText */
void op_split_window(void);
/** @brief Set current output window @ingroup OpcodesText */
void op_set_window(void);


#endif /* ZVIBE_H */
