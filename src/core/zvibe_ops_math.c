/*
 * Derived from mojozork by Ryan C. Gordon
 * https://github.com/icculus/mojozork
 * Copyright (c) 2015-2023 Ryan C. Gordon
 * Copyright (c) 2025 Martin R. Raumann
 * SPDX-License-Identifier: BSD-3-Clause
 */
/**
 * @file zvibe_ops_math.c
 * @brief Arithmetic and logical opcodes
 * @ingroup OpcodesMath
 *
 * Implements mathematical and bitwise operations for Z-machine V3:
 * - Arithmetic: add, sub, mul, div, mod (signed 16-bit)
 * - Bitwise: and, or, not, test
 * - Increment/decrement: inc, dec, inc_chk, dec_chk
 * - Comparison: jl, jg (signed)
 * - Random number generation
 *
 * **Implementation Notes:**
 * - All arithmetic is signed 16-bit (zSWord)
 * - Division by zero is fatal error
 * - Random uses stdlib rand() (seed via random opcode)
 */

#include <stdlib.h>
#include <time.h>
#include "zvibe.h"

/* Constants for the random number generator */
#define Z3_RANDOM_MULTIPLIER 1103515245
#define Z3_RANDOM_INCREMENT  12345
#define Z3_RANDOM_DIVISOR    65536
#define Z3_RANDOM_MODULUS    32768

/* ----------------- ARITHMETIC OPERATIONS ----------------- */

/**
 * Add two numbers (opcode: add)
 */
void op_add(void) {
    zByte *store = z_var_addr(z_read_pc_byte(), 1);
    const zSWord result = ((zSWord)G->operands[0]) + ((zSWord)G->operands[1]);
    Z_WRITE16(store, result);
}

/**
 * Subtract two numbers (opcode: sub)
 */
void op_sub(void) {
    zByte *store = z_var_addr(z_read_pc_byte(), 1);
    const zSWord result = ((zSWord)G->operands[0]) - ((zSWord)G->operands[1]);
    Z_WRITE16(store, result);
}

/**
 * Multiply two numbers (opcode: mul)
 */
void op_mul(void) {
    zByte *store = z_var_addr(z_read_pc_byte(), 1);
    const zSWord result = ((zSWord)G->operands[0]) * ((zSWord)G->operands[1]);
    Z_WRITE16(store, result);
}

/**
 * Divide two numbers (opcode: div)
 */
void op_div(void) {
    zByte *store = z_var_addr(z_read_pc_byte(), 1);
    
    /* Check for division by zero */
    if (G->operands[1] == 0) {
        G->die("Division by zero");
    }
    
    const zSWord result = ((zSWord)G->operands[0]) / ((zSWord)G->operands[1]);
    Z_WRITE16(store, result);
}

/**
 * Modulo operation (opcode: mod)
 */
void op_mod(void) {
    zByte *store = z_var_addr(z_read_pc_byte(), 1);
    
    /* Check for division by zero */
    if (G->operands[1] == 0) {
        G->die("Division by zero");
    }
    
    const zSWord result = ((zSWord)G->operands[0]) % ((zSWord)G->operands[1]);
    Z_WRITE16(store, result);
}

/* ----------------- BITWISE OPERATIONS ----------------- */

/**
 * Bitwise AND (opcode: and)
 */
void op_and(void) {
    zByte *store = z_var_addr(z_read_pc_byte(), 1);
    Z_WRITE16(store, G->operands[0] & G->operands[1]);
}

/**
 * Bitwise OR (opcode: or)
 */
void op_or(void) {
    zByte *store = z_var_addr(z_read_pc_byte(), 1);
    Z_WRITE16(store, G->operands[0] | G->operands[1]);
}

/**
 * Bitwise NOT (opcode: not)
 */
void op_not(void) {
    zByte *store = z_var_addr(z_read_pc_byte(), 1);
    Z_WRITE16(store, ~G->operands[0]);
}

/* ----------------- INCREMENT/DECREMENT OPERATIONS ----------------- */

/**
 * Increment variable (opcode: inc)
 * 
 * Efficiently increments a variable by directly manipulating memory.
 */
void op_inc(void) {
    zByte var_id = (zByte)G->operands[0];
    zByte *addr = z_var_addr(var_id, 0);  /* Get address for reading */
    zSWord value = ((zSWord)(addr[0] << 8)) | addr[1];  /* Read value */
    value++;  /* Increment */
    
    /* Write back the incremented value */
    addr = z_var_addr(var_id, 1);  /* Get address for writing */
    addr[0] = (value >> 8) & 0xFF;  /* Write high byte */
    addr[1] = value & 0xFF;  /* Write low byte */
}

/**
 * Decrement variable (opcode: dec)
 * 
 * Efficiently decrements a variable by directly manipulating memory.
 */
void op_dec(void) {
    zByte var_id = (zByte)G->operands[0];
    zByte *addr = z_var_addr(var_id, 0);  /* Get address for reading */
    zSWord value = ((zSWord)(addr[0] << 8)) | addr[1];  /* Read value */
    value--;  /* Decrement */
    
    /* Write back the decremented value */
    addr = z_var_addr(var_id, 1);  /* Get address for writing */
    addr[0] = (value >> 8) & 0xFF;  /* Write high byte */
    addr[1] = value & 0xFF;  /* Write low byte */
}

/**
 * Increment and check variable (opcode: inc_chk)
 * 
 * Increments a variable and branches if greater than a value.
 */
void op_inc_chk(void) {
    zByte var_id = (zByte)G->operands[0];
    zByte *addr = z_var_addr(var_id, 0);  /* Get address for reading */
    zSWord value = ((zSWord)(addr[0] << 8)) | addr[1];  /* Read value */
    value++;  /* Increment */
    
    /* Write back the incremented value */
    addr = z_var_addr(var_id, 1);  /* Get address for writing */
    addr[0] = (value >> 8) & 0xFF;  /* Write high byte */
    addr[1] = value & 0xFF;  /* Write low byte */
    
    /* Branch if value > operand */
    z_do_branch((value > ((zSWord)G->operands[1])) ? 1 : 0);
}

/**
 * Decrement and check variable (opcode: dec_chk)
 * 
 * Decrements a variable and branches if less than a value.
 */
void op_dec_chk(void) {
    zByte var_id = (zByte)G->operands[0];
    zByte *addr = z_var_addr(var_id, 0);  /* Get address for reading */
    zSWord value = ((zSWord)(addr[0] << 8)) | addr[1];  /* Read value */
    value--;  /* Decrement */
    
    /* Write back the decremented value */
    addr = z_var_addr(var_id, 1);  /* Get address for writing */
    addr[0] = (value >> 8) & 0xFF;  /* Write high byte */
    addr[1] = value & 0xFF;  /* Write low byte */
    
    /* Branch if value < operand */
    z_do_branch((value < ((zSWord)G->operands[1])) ? 1 : 0);
}

/* ----------------- RANDOM NUMBER GENERATOR ----------------- */

/**
 * Random number generator state
 */
static zSDWord z_random_seed = 0;

/**
 * Set the random number generator seed
 *
 * @param seed Seed value (0 = use system time)
 */
void z_random_set_seed(zSDWord seed) {
    z_random_seed = seed;
}

/**
 * Generate a random number using linear congruential generator
 * 
 * @return Random integer between 0 and Z3_RANDOM_MODULUS-1
 */
static int z_generate_random(void) {
    /* Standard linear congruential generator parameters */
    z_random_seed = z_random_seed * Z3_RANDOM_MULTIPLIER + Z3_RANDOM_INCREMENT;
    return (int)((unsigned int)(z_random_seed / Z3_RANDOM_DIVISOR) % Z3_RANDOM_MODULUS);
}

/**
 * Random number generation (opcode: random)
 * 
 * Generates a random number in the range 1 to N, or handles
 * special cases for resetting the random seed.
 */
void op_random(void) {
    zByte *store = z_var_addr(z_read_pc_byte(), 1);
    const zSWord range = (zSWord)G->operands[0];
    zWord result = 0;
    
    if (range == 0) {
        /* Reseed randomly using system time */
        z_random_seed = (int)time(NULL);
    }
    else if (range < 0) {
        /* Reseed with a specific value */
        z_random_seed = -range;
    }
    else {
        /* Generate random number in range 1..range */
        const zWord min_val = 1;
        const zWord max_val = (zWord)range;
        
        result = (((zWord)z_generate_random()) % ((max_val + 1) - min_val)) + min_val;
        
        /* Ensure result is never zero (per Z-machine spec) */
        if (result == 0) {
            result = 1;
        }
    }
    
    Z_WRITE16(store, result);
}

