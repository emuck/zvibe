/*
 * Derived from mojozork by Ryan C. Gordon
 * https://github.com/icculus/mojozork
 * Copyright (c) 2015-2023 Ryan C. Gordon
 * Copyright (c) 2025 Martin R. Raumann
 * SPDX-License-Identifier: BSD-3-Clause
 */
/**
 * @file zvibe_core.c
 * @brief Core interpreter execution loop and state management
 * @ingroup CoreInterpreter
 *
 * Main execution loop, instruction fetch/decode, program counter management,
 * and variable access functions.
 *
 * **Key Functions:**
 * - z_run_instruction(): Fetch-decode-execute cycle
 * - z_read_pc_byte/word(): PC-relative memory access
 * - z_var_addr(): Variable resolution (stack/local/global)
 * - z_do_return/branch(): Control flow operations
 *
 * **Embedded Constraints:**
 * - Global state pointer G for compact embedded code
 * - Static memory buffer (no malloc at runtime)
 * - Configurable buffer size via ZVIBE_MEMORY_BUFFER_SIZE
 */

#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>
#if ZVIBE_ENABLE_DIAGNOSTICS
#include <stdio.h>
#endif
#include "zvibe.h"

/* Global state pointer */
zState *G = NULL;

/* Shared memory buffer - dynamic memory at bottom, stack at top.
 * Layout: [dynamic memory (0..staticmem_addr)] [stack (grows up from staticmem_addr)]
 * Stack space is determined at runtime based on game's staticmem_addr. */
static uint8_t static_memory_buffer[ZVIBE_MEMORY_BUFFER_SIZE];

/* Helper functions for PC management using memory abstraction */
zByte z_read_pc_byte(void) {
    zByte value;
    int result = zmem_read_byte(&G->memory_state, G->logical_pc, &value);
    if (result != ZMEM_SUCCESS) {
        G->die("Failed to read byte at PC=0x%04X", (unsigned)G->logical_pc);
    }
    G->logical_pc++;
    return value;
}

zWord z_read_pc_word(void) {
    zWord value;
    if (zmem_read_word(&G->memory_state, G->logical_pc, &value) != ZMEM_SUCCESS) {
        G->die("Failed to read word at PC=0x%04X", (unsigned)G->logical_pc);
    }
    G->logical_pc += 2;
    return value;
}

static void z_set_pc(zDWord logical_addr) {
    G->logical_pc = logical_addr;
}

/**
 * Returns a pointer to memory at the given offset
 * 
 * @param offset Address offset in story memory
 * @return Pointer to memory at offset
 */
zByte *z_get_mem_ptr(zWord offset) { 
    return zmem_get_ptr(&G->memory_state, offset, 0); 
}

/* Unpack V3 packed address (multiply by 2) */
zByte *z_unpack_addr(zDWord addr) {
    return zmem_get_ptr(&G->memory_state, addr * 2, 0);
}

/* Get variable address (stack/local/global) */
zByte *z_var_addr(zByte var, int writing) {
    if (var == 0) { /* Stack */
        if (writing) {
            if (G->stack_ptr >= G->stack_mem + Z3_STACK_SIZE)
                G->die("Stack overflow");
            return (zByte *)G->stack_ptr++;
        } else {
            if (G->stack_ptr == G->stack_mem)
                G->die("Stack underflow");
                
            const zWord num_locals = G->base_ptr ? G->stack_mem[G->base_ptr-1] : 0;
            if ((G->base_ptr + num_locals) >= (G->stack_ptr - G->stack_mem))
                G->die("Stack underflow");
                
            return (zByte *)--G->stack_ptr;
        }
    } else if ((var >= 1) && (var <= 15)) { /* Local var */
        if (G->stack_mem[G->base_ptr-1] <= (var-1))
            G->die("Invalid local var #%u", (unsigned int)(var-1));
        return (zByte *)&G->stack_mem[G->base_ptr + (var-1)];
    } else { /* Global var */
        return zmem_get_ptr(&G->memory_state, G->header.globals_addr + ((var-0x10) * 2), writing);
    }
}

/**
 * Performs a return operation in the Z-machine
 * 
 * @param val Value to return
 */
void z_do_return(zWord val) {
    if (G->base_ptr == 0)
        G->die("Stack underflow in return");
        
    /* Restore execution state */
    G->stack_ptr = G->stack_mem + G->base_ptr;
    G->stack_ptr--;
    G->base_ptr = *--G->stack_ptr;
    
    G->stack_ptr -= 2;
    const zDWord pcoffset = ((zDWord)G->stack_ptr[0]) | (((zDWord)G->stack_ptr[1]) << 16);
    G->logical_pc = pcoffset;
    
    const zByte storeid = (zByte)*--G->stack_ptr;
    
    /* Store return value */
    zByte *store = z_var_addr(storeid, 1);
    Z_WRITE16(store, val);
}

/**
 * Performs a branch operation in the Z-machine
 * 
 * @param truth Truth value to determine branch direction
 */
void z_do_branch(int truth) {
    const zByte branch = z_read_pc_byte();
    const int farjump = (branch & (1<<6)) == 0;
    const int ontruth = (branch & (1<<7)) ? 1 : 0;
    
    const zByte byte2 = farjump ? z_read_pc_byte() : 0;
    
    if (truth == ontruth) {
        zSWord offset = (zSWord)(branch & 0x3F);
        if (farjump) {
            if (offset & (1 << 5))
                offset |= 0xC0;   /* Sign extension */
            offset = (offset << 8) | ((zSWord)byte2);
        }
        
        if (offset == 0)
            z_do_return(0);
        else if (offset == 1)
            z_do_return(1);
        else
            z_set_pc(G->logical_pc + offset - 2);
    }
}

/**
 * Parse a single operand for an instruction
 * 
 * @param optype Operand type
 * @param operand Pointer to store parsed operand
 * @return 1 if operand was parsed, 0 if omitted
 */
static int z_parse_operand(zByte optype, zWord *operand) {
    switch (optype) {
        case 0: *operand = z_read_pc_word(); return 1;            /* large constant (u16) */
        case 1: *operand = z_read_pc_byte(); return 1;            /* small constant (u8) */
        case 2: {                                                  /* variable */
            const zByte *addr = z_var_addr(z_read_pc_byte(), 0);
            *operand = Z_READ16(addr);
            return 1;
        }
        case 3: break;  /* omitted */
    }
    return 0;
}

/**
 * Parse variable number of operands for instruction
 * 
 * @param operands Array to store parsed operands
 * @return Number of operands parsed
 */
static zByte z_parse_var_operands(zWord *operands) {
    const zByte operand_types = z_read_pc_byte();
    zByte shifter = 6;
    zByte i;
    
    for (i = 0; i < 4; i++) {
        const zByte optype = (operand_types >> shifter) & 0x3;
        shifter -= 2;
        if (!z_parse_operand(optype, operands + i))
            break;
    }
    
    return i;
}

/**
 * Execute a single Z-machine instruction
 */
void z_run_instruction(void) {
    zByte opcode = z_read_pc_byte();
    
    zOpcodeFn op = NULL;
    
    /* For Version 3, we don't need to handle extended opcodes */
    if (opcode <= 127) {  /* 2OP */
        G->operand_count = 2;
        z_parse_operand(((opcode >> 6) & 0x1) ? 2 : 1, G->operands + 0);
        z_parse_operand(((opcode >> 5) & 0x1) ? 2 : 1, G->operands + 1);
    } else if (opcode <= 175) {  /* 1OP */
        G->operand_count = 1;
        const zByte optype = (opcode >> 4) & 0x3;
        z_parse_operand(optype, G->operands);
    } else if (opcode <= 191) {  /* 0OP */
        G->operand_count = 0;
    } else if (opcode > 191) {  /* VAR */
        G->operand_count = z_parse_var_operands(G->operands);
    }
    
    op = z_get_opcode_handler(opcode);

    if (!op)
        G->die("Unsupported opcode #%u", (unsigned int)opcode);
    else {
        op();
        G->instr_count++;
    }
}

/**
 * Initialize the Z-machine
 * 
 * @param state Pointer to Z-machine state to initialize
 */
void z_init_machine(zState *state) {
    G = state;
    if (!G) return;
    
    /* Initialize state */
    memset(G, 0, sizeof(zState));

    init_opcodes();
}

/**
 * Report memory usage statistics
 */
void z_report_memory_usage(void) {
#if ZVIBE_ENABLE_DIAGNOSTICS
    if (!G) return;
    
    printf("Memory Usage Report:\n");
    printf("  Story memory: %u/%u bytes (%.1f%%)\n", 
           (unsigned int)G->story_size, 
           Z3_STORY_MEM_SIZE,
           (G->story_size * 100.0f) / Z3_STORY_MEM_SIZE);
    
    printf("  Stack usage: %u/%u entries (%.1f%%)\n",
           (unsigned int)(G->stack_ptr - G->stack_mem),
           Z3_STACK_SIZE,
           ((G->stack_ptr - G->stack_mem) * 100.0f) / Z3_STACK_SIZE);
    
    /* Calculate actual static memory size (with split memory) */
    size_t static_size = sizeof(zState);
    static_size -= sizeof(G->stack_mem);   /* Exclude stack */
    
    /* Report split memory usage */
    size_t ram_used;
    zmem_get_stats(&G->memory_state, &ram_used);

    printf("  Static memory: %u bytes\n", (unsigned int)static_size);
    printf("  Split memory - RAM: %u bytes\n", (unsigned int)ram_used);
#else
    (void)G;
#endif
}

/**
 * Calculate the size needed for save data
 * 
 * @return Number of bytes needed for save data
 */
size_t z_get_save_data_size(void) {
    if (!G) return 0;
    
    size_t required_size = G->memory_state.ram_size;
    required_size += sizeof(uint32_t); /* PC address */
    required_size += sizeof(uint32_t); /* Stack size */
    required_size += (G->stack_ptr - G->stack_mem) * sizeof(zWord); /* Stack data */
    required_size += sizeof(G->base_ptr); /* Base pointer */
    
    return required_size;
}

/**
 * Get save data from Z-machine state
 * 
 * @param buffer Buffer to store save data
 * @param buffer_size Size of buffer in bytes
 * @return Number of bytes written to buffer, or 0 on error
 */
size_t z_get_save_data(void *buffer, size_t buffer_size) {
    if (!G || !buffer || buffer_size == 0) return 0;
    
    /* Calculate required size */
    size_t required_size = z_get_save_data_size();
    
    if (buffer_size < required_size) {
        return 0; /* Buffer too small */
    }
    
    uint8_t *ptr = (uint8_t *)buffer;
    size_t ram_size = G->memory_state.ram_size;

    /* Dynamic memory is already stored contiguously in RAM. */
    memcpy(ptr, G->memory_state.ram_buffer, ram_size);
    ptr += ram_size;
    
    /* Save execution state */
    uint32_t pc_addr = (uint32_t)G->logical_pc;
    memcpy(ptr, &pc_addr, sizeof(pc_addr));
    ptr += sizeof(pc_addr);
    
    /* Save stack size */
    uint32_t stack_size = (uint32_t)(G->stack_ptr - G->stack_mem);
    memcpy(ptr, &stack_size, sizeof(stack_size));
    ptr += sizeof(stack_size);
    
    /* Save stack data */
    memcpy(ptr, G->stack_mem, stack_size * sizeof(zWord));
    ptr += stack_size * sizeof(zWord);
    
    /* Save base pointer */
    memcpy(ptr, &G->base_ptr, sizeof(G->base_ptr));
    ptr += sizeof(G->base_ptr);
    
    return ptr - (uint8_t *)buffer;
}

/**
 * Restore Z-machine state from save data
 * 
 * @param data Pointer to save data
 * @param length Length of save data in bytes
 * @return 0 on success, error code otherwise
 */
int z_restore_save_data(const void *data, size_t length) {
    if (!G || !data) return 1; /* Invalid arguments */

    size_t ram_size = G->memory_state.ram_size;
    size_t minimum_size = ram_size + sizeof(uint32_t) + sizeof(uint32_t) + sizeof(G->base_ptr);

    if (length < minimum_size) return 2; /* Data too small */

    memcpy(G->memory_state.ram_buffer, data, ram_size);

    /* Read execution state */
    const uint8_t *ptr = (const uint8_t *)data + ram_size;
    
    /* Restore program counter */
    uint32_t pc_addr;
    memcpy(&pc_addr, ptr, sizeof(pc_addr));
    ptr += sizeof(pc_addr);
    
    /* Restore stack size */
    uint32_t stack_size;
    memcpy(&stack_size, ptr, sizeof(stack_size));
    ptr += sizeof(stack_size);
    
    /* Validate stack size and serialized length */
    if (stack_size > Z3_STACK_SIZE) return 3; /* Invalid stack size */

    if ((size_t)(ptr - (const uint8_t *)data) + (stack_size * sizeof(zWord)) + sizeof(G->base_ptr) > length) {
        return 2;
    }
    
    /* Restore stack data */
    memcpy(G->stack_mem, ptr, stack_size * sizeof(zWord));
    ptr += stack_size * sizeof(zWord);
    
    /* Restore base pointer */
    memcpy(&G->base_ptr, ptr, sizeof(G->base_ptr));
    
    /* Set program counter and stack pointer */
    G->logical_pc = pc_addr;
    G->stack_ptr = G->stack_mem + stack_size;
    
    return 0; /* Success */
}

/**
 * Initialize Z-machine with story data
 *
 * @param data Pointer to story data in memory
 * @param size Size of story data in bytes
 * @return 0 on success, error code otherwise
 */
int z_load_story_data(const void *data, size_t size) {
    #define Z3_VERSION 3  /* Version 3 Z-machine */

    if (!data || size == 0)
        return 1; /* Invalid arguments */

    if (size < 64)
        return 2; /* Too small for Z-machine header */

    if (size > Z3_STORY_MEM_SIZE)
        return 2; /* Story data too large */

    /* Initialize state with new story */
    G->story_size = (zSize)size;
    G->instr_count = 0;
    G->logical_pc = 0;
    G->quit_flag = 0;
    G->input_requested = 0;
    G->save_requested = 0;
    G->restore_requested = 0;
    G->restart_requested = 0;
    memset(G->stack_mem, 0, sizeof(G->stack_mem));
    memset(G->operands, 0, sizeof(G->operands));
    G->operand_count = 0;
    G->stack_ptr = G->stack_mem;
    G->base_ptr = 0;

    memset(&G->header, 0, sizeof(G->header));

    /* Parse header from story data */
    const zByte *ptr = (const zByte *)data;
    G->header.version = Z_READ8(ptr);

    /* Verify version is 3 */
    if (G->header.version != Z3_VERSION)
        return 3; /* Unsupported version */

    G->header.flags1 = Z_READ8(ptr);
    G->header.release = Z_READ16(ptr);
    G->header.himem_addr = Z_READ16(ptr);
    G->header.pc_start = Z_READ16(ptr);
    G->header.dict_addr = Z_READ16(ptr);
    G->header.objtab_addr = Z_READ16(ptr);
    G->header.globals_addr = Z_READ16(ptr);
    G->header.staticmem_addr = Z_READ16(ptr);
    G->header.flags2 = Z_READ16(ptr);

    for (int i = 0; i < 6; i++) {
        G->header.serial[i] = Z_READ8(ptr);
    }
    G->header.serial[6] = '\0';

    G->header.abbrtab_addr = Z_READ16(ptr);
    G->header.story_len = Z_READ16(ptr);
    G->header.story_checksum = Z_READ16(ptr);

    /* Validate header addresses fall within story data */
    if (G->header.pc_start >= size ||
        G->header.dict_addr >= size ||
        G->header.objtab_addr >= size ||
        (size_t)G->header.globals_addr + 480U > size ||
        G->header.staticmem_addr > size ||
        G->header.staticmem_addr == 0 ||
        G->header.abbrtab_addr >= size)
        return 5; /* Corrupt header */

    /* Validate game fits in buffer with room for minimum stack */
    size_t min_stack_bytes = Z3_STACK_SIZE * sizeof(uint16_t);
    if (G->header.staticmem_addr + min_stack_bytes > ZVIBE_MEMORY_BUFFER_SIZE) {
        return 4; /* Game too large for buffer */
    }

    /* Initialize split memory with shared buffer */
    zmem_config_t config = {
        .staticmem_addr = G->header.staticmem_addr,
        .story_size = size
    };

    int zmem_result = zmem_init_embedded(&G->memory_state, &config, data, size,
                                         static_memory_buffer);
    if (zmem_result != ZMEM_SUCCESS) {
        return 4; /* Split memory initialization failed */
    }

    /* Set up interpreter flags using split memory */
    zmem_byte_t flags1;
    zmem_read_byte(&G->memory_state, 1, &flags1);
    flags1 |= (1<<4);  /* Status line flag - we disable it */
    zmem_write_byte(&G->memory_state, 1, flags1);

    /* Set up program counter */
    G->logical_pc = (zDWord)G->header.pc_start;

    return 0; /* Success */
}
