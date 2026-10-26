/*
 * Derived from mojozork by Ryan C. Gordon
 * https://github.com/icculus/mojozork
 * Copyright (c) 2015-2023 Ryan C. Gordon
 * Copyright (c) 2025 Martin R. Raumann
 * SPDX-License-Identifier: BSD-3-Clause
 */
/**
 * @file zvibe_ops_base.c
 * @brief Control flow opcodes (call, return, branch, stack)
 * @ingroup OpcodesBase
 *
 * Implements core control flow operations for Z-machine V3:
 * - Opcode dispatch table helpers
 * - Subroutine call/return (call, ret, rtrue, rfalse, ret_popped)
 * - Branching (je, jz, jl, jg, jump, branch decision logic)
 * - Stack manipulation (push, pop, pull)
 * - Game control (save, restore, restart, quit, verify)
 *
 * **Implementation Notes:**
 * - Compact opcode dispatch tables keyed by opcode class and low bits
 * - V3-specific opcode numbers and operand counts
 * - Stack frames: [num_locals][saved_pc_low][saved_pc_high][base_ptr][store_var][locals...]
 */

#include "zvibe.h"

/* Constants for opcode categories */
#define Z3_2OP_BASE      0    /* Base index for 2-operand opcodes */
#define Z3_1OP_BASE      128  /* Base index for 1-operand opcodes */
#define Z3_0OP_BASE      176  /* Base index for 0-operand opcodes */
#define Z3_2OP_VAR_BASE  192  /* Base index for variable-form 2-operand opcodes */
#define Z3_VAR_BASE      224  /* Base index for variable-operand opcodes */

/* Opcodes per category */
#define Z3_2OP_COUNT     32   /* Number of 2-operand opcodes */
#define Z3_1OP_COUNT     16   /* Number of 1-operand opcodes */
#define Z3_0OP_COUNT     16   /* Number of 0-operand opcodes */
#define Z3_VAR_COUNT     32   /* Number of variable-operand opcodes */

/* Version 3 specific constants */
#define Z3_LOCAL_VAR_BYTES 2  /* Size of local variables in Version 3 */

/* ----------------- OPCODE TABLES ----------------- */

/* Dispatch tables live in flash as const data: nothing to initialize at run time
 * and no RAM used. Unlisted slots are NULL (unsupported opcode). */
/* 2OP (opcodes 0-127 and 192-223; index is the low 5 bits) */
static const zOpcodeFn z_2op_table[Z3_2OP_COUNT] = {
    [1] = op_je,
    [2] = op_jl,
    [3] = op_jg,
    [4] = op_dec_chk,
    [5] = op_inc_chk,
    [6] = op_jin,
    [7] = op_test,
    [8] = op_or,
    [9] = op_and,
    [10] = op_test_attr,
    [11] = op_set_attr,
    [12] = op_clear_attr,
    [13] = op_store,
    [14] = op_insert_obj,
    [15] = op_loadw,
    [16] = op_loadb,
    [17] = op_get_prop,
    [18] = op_get_prop_addr,
    [19] = op_get_next_prop,
    [20] = op_add,
    [21] = op_sub,
    [22] = op_mul,
    [23] = op_div,
    [24] = op_mod,
};

/* 1OP (opcodes 128-175; index is the low 4 bits) */
static const zOpcodeFn z_1op_table[Z3_1OP_COUNT] = {
    [0] = op_jz,
    [1] = op_get_sibling,
    [2] = op_get_child,
    [3] = op_get_parent,
    [4] = op_get_prop_len,
    [5] = op_inc,
    [6] = op_dec,
    [7] = op_print_addr,
    [9] = op_remove_obj,
    [10] = op_print_obj,
    [11] = op_ret,
    [12] = op_jump,
    [13] = op_print_paddr,
    [14] = op_load,
    [15] = op_not,
};

/* 0OP (opcodes 176-191; index is the low 4 bits) */
static const zOpcodeFn z_0op_table[Z3_0OP_COUNT] = {
    [0] = op_rtrue,
    [1] = op_rfalse,
    [2] = op_print,
    [3] = op_print_ret,
    [4] = op_nop,
    [5] = op_save,
    [6] = op_restore,
    [7] = op_restart,
    [8] = op_ret_popped,
    [9] = op_pop,
    [10] = op_quit,
    [11] = op_new_line,
    [12] = op_show_status,
    [13] = op_verify,
};

/* VAR (opcodes 224-255; index is the low 5 bits) */
static const zOpcodeFn z_var_table[Z3_VAR_COUNT] = {
    [0] = op_call,
    [1] = op_storew,
    [2] = op_storeb,
    [3] = op_put_prop,
    [4] = op_read,
    [5] = op_print_char,
    [6] = op_print_num,
    [7] = op_random,
    [8] = op_push,
    [9] = op_pull,
    [10] = op_split_window,
    [11] = op_set_window,
};

zOpcodeFn z_get_opcode_handler(zByte opcode) {
    if (opcode < Z3_1OP_BASE) {
        return z_2op_table[opcode & (Z3_2OP_COUNT - 1)];
    }
    if (opcode < Z3_0OP_BASE) {
        return z_1op_table[opcode & (Z3_1OP_COUNT - 1)];
    }
    if (opcode < Z3_2OP_VAR_BASE) {
        return z_0op_table[opcode & (Z3_0OP_COUNT - 1)];
    }
    if (opcode < Z3_VAR_BASE) {
        return z_2op_table[opcode & (Z3_2OP_COUNT - 1)];
    }
    return z_var_table[opcode & (Z3_VAR_COUNT - 1)];
}

/* ----------------- CALL/RETURN OPERATIONS ----------------- */

/**
 * Call a Z-machine routine (opcode: call)
 * 
 * Saves the current execution state and transfers control to another routine.
 * In Version 3, routine addresses are packed (multiplied by 2).
 */
void op_call(void) {
    zByte args_count = G->operand_count;
    const zWord *operands = G->operands;
    const zByte store_id = z_read_pc_byte();
    
    /* Check for no-op call (0 address or no arguments) */
    if ((args_count == 0) || (operands[0] == 0)) {
        zByte *store = z_var_addr(store_id, 1);
        Z_WRITE16(store, 0);
        return;
    }
    
    /* Get routine address (packed in Version 3) */
    const zByte *routine = z_unpack_addr(operands[0]);
    /* Calculate logical address for the routine */
    zDWord routine_addr = operands[0] * 2;  /* In V3, addresses are scaled by 2 */
    zDWord current_routine_offset = routine_addr;
    
    /* First byte of routine contains number of local variables */
    const zByte local_count = *routine++;
    current_routine_offset++;
    
    /* Verify enough stack space for call frame (5 words) + locals */
    size_t frame_size = 5 + local_count;
    if (G->stack_ptr + frame_size > G->stack_mem + ZVIBE_STACK_WORDS)
        G->die("Stack overflow in call (need %u words)", (unsigned)frame_size);

    /* Save call frame on stack */
    *G->stack_ptr++ = (zWord)store_id;

    /* Save next instruction address for return */
    const zDWord pc_offset = G->logical_pc;  /* This is the correct return address */
    *G->stack_ptr++ = (pc_offset & 0xFFFF);
    *G->stack_ptr++ = ((pc_offset >> 16) & 0xFFFF);

    /* Save previous base pointer and local variable count */
    *G->stack_ptr++ = G->base_ptr;
    *G->stack_ptr++ = local_count;

    /* Set new base pointer to current stack position */
    G->base_ptr = (zWord)(G->stack_ptr - G->stack_mem);

    /* Initialize local variables from routine header (Version 3 specific) */
    zSByte i;
    for (i = 0; i < local_count; i++, routine += 2) {
        /* NOTE: Despite Z-machine being big-endian, routine header defaults appear to be
         * stored in native (little-endian) format. This matches what the pointer cast does. */
        const zWord value = (zWord)routine[0] | ((zWord)routine[1] << 8);  /* Little-endian */
        *G->stack_ptr++ = value;
        current_routine_offset += 2;
    }
    
    /* Copy arguments to locals (overriding defaults) */
    zByte arg_count = args_count - 1;  /* Exclude routine address */
    if (arg_count > local_count) {
        arg_count = local_count;
    }
    
    const zWord *src = operands + 1;
    zByte *dst = (zByte *)(G->stack_mem + G->base_ptr);
    for (i = 0; i < arg_count; i++) {
        Z_WRITE16(dst, src[i]);
    }
    
    /* Set program counter to first instruction of routine (after local vars header) */
    G->logical_pc = current_routine_offset;
}

/**
 * Return from routine with a value (opcode: ret)
 */
void op_ret(void) {
    z_do_return(G->operands[0]);
}

/**
 * Return from routine with value 1 (opcode: rtrue)
 */
void op_rtrue(void) {
    z_do_return(1);
}

/**
 * Return from routine with value 0 (opcode: rfalse)
 */
void op_rfalse(void) {
    z_do_return(0);
}

/**
 * Return from routine with value popped from stack (opcode: ret_popped)
 */
void op_ret_popped(void) {
    zByte *ptr = z_var_addr(0, 0);
    const zWord result = Z_READ16(ptr);
    z_do_return(result);
}

/* ----------------- BRANCH OPERATIONS ----------------- */

/**
 * Jump if equal (opcode: je)
 * 
 * Branches if the first operand equals any of the other operands.
 */
void op_je(void) {
    const zWord a = G->operands[0];
    zSByte i;
    
    for (i = 1; i < G->operand_count; i++) {
        if (a == G->operands[i]) {
            z_do_branch(1);
            return;
        }
    }
    
    z_do_branch(0);
}

/**
 * Jump if zero (opcode: jz)
 */
void op_jz(void) {
    z_do_branch((G->operands[0] == 0) ? 1 : 0);
}

/**
 * Jump if less than (opcode: jl)
 */
void op_jl(void) {
    z_do_branch((((zSWord)G->operands[0]) < ((zSWord)G->operands[1])) ? 1 : 0);
}

/**
 * Jump if greater than (opcode: jg)
 */
void op_jg(void) {
    z_do_branch((((zSWord)G->operands[0]) > ((zSWord)G->operands[1])) ? 1 : 0);
}

/**
 * Test bits (opcode: test)
 * 
 * Branches if all the bits set in the second operand are also set in the first.
 */
void op_test(void) {
    z_do_branch((G->operands[0] & G->operands[1]) == G->operands[1]);
}

/**
 * Unconditional jump (opcode: jump)
 * 
 * Adds a signed offset to the program counter.
 */
void op_jump(void) {
    zDWord new_pc = G->logical_pc + ((zSWord)G->operands[0]) - 2;
    G->logical_pc = new_pc;
}

/* ----------------- STACK OPERATIONS ----------------- */

/**
 * Push value onto stack (opcode: push)
 */
void op_push(void) {
    zByte *store = z_var_addr(0, 1);
    Z_WRITE16(store, G->operands[0]);
}

/**
 * Pull value from stack (opcode: pull)
 * 
 * Pulls a value from the stack and stores it in a variable.
 */
void op_pull(void) {
    zWord val;
    
    /* Get value from stack (pop it) */
    const zByte *ptr = z_var_addr(0, 0);
    val = Z_READ16(ptr);
    
    /* Check if destination is stack */
    zByte dest_id = (zByte)G->operands[0];
    if (dest_id == 0) {
        /* Special case: pulling to stack - write in place */
        if (G->stack_ptr == G->stack_mem) {
            G->die("Stack underflow in op_pull");
        }
        
        /* Write to the top of stack without pushing - use consistent byte access */
        zByte *stack_ptr = (zByte *)(G->stack_ptr - 1);
        stack_ptr[0] = (val >> 8) & 0xFF;
        stack_ptr[1] = val & 0xFF;
    } else {
        /* Normal case - pull to another variable */
        zByte *store = z_var_addr(dest_id, 1);
        Z_WRITE16(store, val);
    }
}

/**
 * Pop and discard stack value (opcode: pop)
 */
void op_pop(void) {
    z_var_addr(0, 0);  /* Just read the value to pop it */
}

/* ----------------- UTILITY OPERATIONS ----------------- */

/**
 * No operation (opcode: nop)
 */
void op_nop(void) {
    /* No operation - does nothing */
}

/**
 * Quit the interpreter (opcode: quit)
 */
void op_quit(void) {
    G->quit_flag = 1;
}
