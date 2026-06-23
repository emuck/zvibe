/*
 * Derived from mojozork by Ryan C. Gordon
 * https://github.com/icculus/mojozork
 * Copyright (c) 2015-2023 Ryan C. Gordon
 * Copyright (c) 2025 Martin R. Raumann
 * SPDX-License-Identifier: BSD-3-Clause
 */
/**
 * @file zvibe_ops_mem.c
 * @brief Memory access opcodes (load, store, variables)
 * @ingroup OpcodesMem
 *
 * Implements memory and variable access operations for Z-machine V3:
 * - Direct memory: loadb, loadw, storeb, storew
 * - Variables: load, store (stack/local/global)
 * - Stack: push, pop (V3 has separate opcodes, not var 0)
 *
 * **Implementation Notes:**
 * - Variable 0 = stack (push/pop via z_var_addr)
 * - Variables 1-15 = local variables (stack frame)
 * - Variables 16+ = global variables (at header.globals_addr)
 * - Memory access routes through zmem_* for split RAM/flash
 */

#include <stdio.h>
#include "zvibe.h"

/* Constants for memory operations */
#define Z3_VERIFY_START   0x40        /* Starting address for checksum verification */

/* ----------------- MEMORY ACCESS OPERATIONS ----------------- */

/**
 * Load a variable's value (opcode: load)
 * 
 * Reads a value from a variable and stores it in another variable.
 */
void op_load(void) {
    zByte var_id = (zByte)(G->operands[0] & 0xFF);
    zWord val;

    if (var_id == 0) {
        /* Special case for stack: read in place without popping */
        if (G->stack_ptr == G->stack_mem) {
            G->die("Stack underflow in op_load");
        }

        /* Read the top value without popping - read as big-endian */
        const zByte *ptr = (const zByte *)(G->stack_ptr - 1);
        val = (ptr[0] << 8) | ptr[1];
    } else {
        /* Normal case for other variables */
        const zByte *valptr = z_var_addr(var_id, 0);
        val = Z_READ16(valptr);
    }

    /* Get the destination and store the value */
    zByte *store = z_var_addr(z_read_pc_byte(), 1);
    Z_WRITE16(store, val);
}

/**
 * Store a value in a variable (opcode: store)
 * 
 * Writes a value to a variable.
 */
void op_store(void) {
    zByte var_id = (zByte)(G->operands[0] & 0xFF);
    zWord val = G->operands[1];
    
    if (var_id == 0) {
        /* Special case for stack: write to current top in place */
        if (G->stack_ptr == G->stack_mem) {
            G->die("Stack underflow in op_store");
        }
        
        /* Write to current top without incrementing pointer */
        zByte *ptr = (zByte *)(G->stack_ptr - 1);
        ptr[0] = (val >> 8) & 0xFF;
        ptr[1] = val & 0xFF;
    } else {
        /* Normal case - use z_var_addr */
        zByte *store = z_var_addr(var_id, 1);
        Z_WRITE16(store, val);
    }
}

/**
 * Load a word from memory (opcode: loadw)
 * 
 * Loads a 16-bit word from an array in memory.
 */
void op_loadw(void) {
    zByte *store = z_var_addr(z_read_pc_byte(), 1);
    
    /* Calculate byte offset: base + (index * 2) */
    const zWord offset = (G->operands[0] + (G->operands[1] * 2));
    
    /* Get pointer to memory and read word */
    const zByte *src = z_get_mem_ptr(offset);
    zWord value = (src[0] << 8) | src[1];
    
    Z_WRITE16(store, value);
}

/**
 * Load a byte from memory (opcode: loadb)
 * 
 * Loads an 8-bit byte from an array in memory.
 */
void op_loadb(void) {
    zByte *store = z_var_addr(z_read_pc_byte(), 1);
    
    /* Calculate byte offset: base + index */
    const zWord offset = (G->operands[0] + G->operands[1]);
    
    /* Get pointer to memory and read byte */
    const zByte *src = z_get_mem_ptr(offset);
    
    /* Store byte value (zero-extended to 16 bits) */
    Z_WRITE16(store, *src);
}

/**
 * Store a word in memory (opcode: storew)
 * 
 * Stores a 16-bit word in an array in memory.
 */
void op_storew(void) {
    /* Calculate byte offset: base + (index * 2) */
    const zWord offset = (G->operands[0] + (G->operands[1] * 2));
    if (zmem_write_word(&G->memory_state, offset, G->operands[2]) != ZMEM_SUCCESS)
        G->die("storew: write failed at 0x%04X", (unsigned)offset);
}

/**
 * Store a byte in memory (opcode: storeb)
 * 
 * Stores an 8-bit byte in an array in memory.
 */
void op_storeb(void) {
    /* Calculate byte offset: base + index */
    const zWord offset = (G->operands[0] + G->operands[1]);
    if (zmem_write_byte(&G->memory_state, offset, (zByte)G->operands[2]) != ZMEM_SUCCESS)
        G->die("storeb: write failed at 0x%04X", (unsigned)offset);
}

/* ----------------- SYSTEM MANAGEMENT OPERATIONS ----------------- */

/**
 * Verify story file integrity (opcode: verify)
 * 
 * Calculates a checksum of the story file and compares it with
 * the stored checksum in the header.
 */
void op_verify(void) {
    /* Use header file length (in words * 2), not actual file size */
    /* This excludes any padding bytes as required by Z-machine spec */
    const zDWord total = (zDWord)G->header.story_len * 2;
    zDWord checksum = 0;
    
    /* Calculate checksum starting from address 0x40 using original story data */
    /* NOTE: verify should use original story data, not current RAM state */
    for (zDWord i = Z3_VERIFY_START; i < total; i++) {
        zmem_byte_t byte_val;
        
        /* Read directly from original flash data for all addresses */
        if (i >= G->memory_state.flash_size) {
            G->die("Address 0x%04X beyond story data during verify", (unsigned)i);
        }
        
        byte_val = G->memory_state.flash_data[i];
        checksum += byte_val;
    }
    
    /* Compare with header checksum (only low 16 bits matter) */
    zWord final_checksum = (zWord)(checksum & 0xFFFF);
    z_do_branch((final_checksum == G->header.story_checksum) ? 1 : 0);
}

/**
 * Restart the game (opcode: restart)
 *
 * Signals to the API layer that a restart is requested.
 * The frontend will handle the actual restart operation.
 */
void op_restart(void) {
    /* Set flag to signal API layer */
    G->restart_requested = 1;

    /* Save current PC to resume after restart handled */
    G->saved_logical_pc = G->logical_pc;

    /* Exit to API layer - execution will continue after restart is handled */
}

/**
 * Save game state (opcode: save)
 * 
 * Signals to the API layer that a save is requested.
 * The frontend will handle the actual save operation.
 */
void op_save(void) {
    /* Set flag to signal API layer */
    G->save_requested = 1;
    
    /* Save current PC to resume after save handled */
    G->saved_logical_pc = G->logical_pc;
    
    /* Exit to API layer - execution will resume when save is handled */
}

/**
 * Restore game state (opcode: restore)
 * 
 * Signals to the API layer that a restore is requested.
 * The frontend will handle the actual restore operation.
 */
void op_restore(void) {
    /* Set flag to signal API layer */
    G->restore_requested = 1;
    
    /* Save current PC to resume if restore fails */
    G->saved_logical_pc = G->logical_pc;
    
    /* Exit to API layer - execution will resume when restore is handled */
}

/**
 * Set window (opcode: set_window)
 * 
 * Sets the current active window. Uses callback-based implementation
 * to allow platform-specific window handling.
 */
void op_set_window(void) {
    /* Handle set_window operation */
    zWord old_window = G->current_window;
    G->current_window = G->operands[0];
    
    /* Call callback if available */
    if (G->set_window) {
        G->set_window(old_window, G->current_window);
    }
}

/**
 * Split window (opcode: split_window)
 * 
 * Configures window splitting. Uses callback-based implementation
 * to allow platform-specific window management.
 */
void op_split_window(void) {
    /* Handle split_window operation */
    zWord old_lines = G->upper_window_lines;
    G->upper_window_lines = G->operands[0];
    
    /* Call callback if available */
    if (G->split_window) {
        G->split_window(old_lines, G->upper_window_lines);
    }
}

/**
 * Show status line (opcode: show_status)
 * 
 * Dummy function to maintain compatibility with games that use
 * this opcode, but our implementation doesn't need to do anything
 * since status line is handled elsewhere.
 */
void op_show_status(void) {
    /* Nothing to do - status line is handled separately */
}

