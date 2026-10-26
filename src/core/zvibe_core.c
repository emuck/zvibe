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

/* The load check below keeps 1KB of the RAM buffer free for a stack, but the
 * stack actually lives in zState (stack_mem), not in that buffer. The value is
 * the historical one (512 words x 2 bytes) and must not follow
 * ZVIBE_STACK_WORDS, or raising the stack size would shrink the dynamic memory
 * a story may have. Removing the reserve is a separate behavior change. */
#define Z_LEGACY_STACK_RESERVE_BYTES 1024

/**
 * Write the interpreter's capabilities into the story header.
 *
 * Called when a story is loaded and again on restart (restart restores dynamic
 * memory, which includes the header flags).
 *
 * V3: set flags1 bit 4, "status line not available" (the host draws it).
 * V4: the game draws its own status line, so that bit means something else;
 *     clear the capability bits (bold 2, italic 3, fixed-space 4, timed input 7)
 *     because no screen is attached at this level, and write the interpreter
 *     number/version (0x1E, 0x1F), the screen size and capabilities reported by the
 *     platform's screen operations (defaults if none), and the standard revision.
 *
 * Also called after a successful restore: the interpreter-owned header fields
 * describe THIS interpreter, so they must not be taken from the saved image.
 */
void z_init_interpreter_header(void) {
    /* The story's own flags1, which is also what dynamic memory holds at load and
     * after a restart restores it from the story. */
    zmem_byte_t flags1 = G->header.flags1;
#if ZVIBE_ENABLE_V4
    if (Z_IS_V4()) {
        uint8_t rows = ZVIBE_SCREEN_ROWS, cols = ZVIBE_SCREEN_COLS, caps = 0;
        if (G->screen) {
            caps = G->screen->capabilities;
            if (G->screen->get_size)
                G->screen->get_size(G->screen_user, &rows, &cols);
        }
        /* Capability bits describe THIS interpreter's screen: bold 2, italic 3, fixed-space 4.
         * Timed input (7) is never claimed: timers are not supported. */
        flags1 &= (zmem_byte_t)~((1 << 2) | (1 << 3) | (1 << 4) | (1 << 7));
        if (caps & ZVIBE_SCREEN_CAP_BOLD)   flags1 |= (1 << 2);
        if (caps & ZVIBE_SCREEN_CAP_ITALIC) flags1 |= (1 << 3);
        if (caps & ZVIBE_SCREEN_CAP_FIXED)  flags1 |= (1 << 4);
        zmem_write_byte(&G->memory_state, 1, flags1);
        zmem_write_byte(&G->memory_state, 0x1E, (zmem_byte_t)ZVIBE_INTERPRETER_NUMBER);
        zmem_write_byte(&G->memory_state, 0x1F, (zmem_byte_t)ZVIBE_INTERPRETER_VERSION);
        zmem_write_byte(&G->memory_state, 0x20, (zmem_byte_t)rows);
        zmem_write_byte(&G->memory_state, 0x21, (zmem_byte_t)cols);
        /* The standard revision followed: 1.1. Every opcode a V4 story may use is implemented
         * (timed input is not claimed, which the standard allows). */
        zmem_write_byte(&G->memory_state, 0x32, 1);
        zmem_write_byte(&G->memory_state, 0x33, 1);
        return;
    }
#endif
    flags1 |= (1<<4);  /* Status line flag - we disable it */
    zmem_write_byte(&G->memory_state, 1, flags1);
}

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

/* Unpack a packed address (multiply by 2 in V3, by 4 in V4) */
zByte *z_unpack_addr(zDWord addr) {
    return zmem_get_ptr(&G->memory_state, z_packed_to_byte(addr), 0);
}

/* Get variable address (stack/local/global) */
zByte *z_var_addr(zByte var, int writing) {
    if (var == 0) { /* Stack */
        if (writing) {
            if (G->stack_ptr >= G->stack_mem + ZVIBE_STACK_WORDS)
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

#if ZVIBE_ENABLE_V4
/**
 * Parse the operands of a VAR-form instruction that has two operand-type bytes
 * (call_vs2, opcode 236: up to eight operands).
 *
 * Both type bytes are read BEFORE any operand data, even when the first byte
 * already ends in "omitted" (standard section 4.4.3.1); the operands are then
 * read in order up to the first omitted type.
 */
static zByte z_parse_double_var_operands(zWord *operands) {
    const zByte types1 = z_read_pc_byte();
    const zByte types2 = z_read_pc_byte();
    zByte i;

    for (i = 0; i < 8; i++) {
        const zByte types = (i < 4) ? types1 : types2;
        const zByte optype = (zByte)((types >> (6 - 2 * (i & 3))) & 0x3);
        if (!z_parse_operand(optype, operands + i))
            break;
    }

    return i;
}
#endif

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
#if ZVIBE_ENABLE_V4
        if (opcode == 236 && Z_IS_V4())             /* call_vs2: two type bytes */
            G->operand_count = z_parse_double_var_operands(G->operands);
        else
#endif
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
           Z_STORY_MEM_SIZE,
           (G->story_size * 100.0f) / Z_STORY_MEM_SIZE);
    
    printf("  Stack usage: %u/%u entries (%.1f%%)\n",
           (unsigned int)(G->stack_ptr - G->stack_mem),
           ZVIBE_STACK_WORDS,
           ((G->stack_ptr - G->stack_mem) * 100.0f) / ZVIBE_STACK_WORDS);
    
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

#if ZVIBE_VERIFY_SAVE_IDENTITY
/**
 * Does a saved image belong to the story that is loaded? Compares the header
 * bytes that identify a story and that neither the game nor the interpreter
 * ever writes: version (0), then everything from the release (2) up to the
 * checksum (0x1D) except flags 2 (0x10-0x11). That covers release, addresses,
 * serial number, abbreviations, length and checksum. The image starts with
 * dynamic memory, which holds the header. (Flags 1 at byte 1 and the
 * interpreter fields from 0x1E are written by the interpreter, so they differ
 * legitimately between a save and the running machine.)
 */
static int z_save_matches_story(const zByte *saved) {
    const zByte *story = (const zByte *)G->memory_state.flash_data;
    zByte diff = saved[0] ^ story[0];                         /* version */
    for (int i = 2; i < 0x1E; i++)
        if (i < 0x10 || i >= 0x12)                            /* skip flags 2 (0x10-0x11): the game writes it */
            diff |= saved[i] ^ story[i];                      /* release, addresses, serial, abbreviations, length, checksum */
    return diff == 0;
}
#endif

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

    /* Read and validate the execution state before touching any live state, so
     * a rejected save leaves the running machine unchanged. */
    const uint8_t *ptr = (const uint8_t *)data + ram_size;

    uint32_t pc_addr;
    memcpy(&pc_addr, ptr, sizeof(pc_addr));
    ptr += sizeof(pc_addr);

    uint32_t stack_size;
    memcpy(&stack_size, ptr, sizeof(stack_size));
    ptr += sizeof(stack_size);

    /* Validate stack size, then that the stack words fit in what remains */
    if (stack_size > ZVIBE_STACK_WORDS) return 3; /* Invalid stack size */
    if (length - minimum_size < stack_size * sizeof(zWord)) return 2;

#if ZVIBE_VERIFY_SAVE_IDENTITY
    /* The save must belong to the story that is running. Restoring another story's
     * (or another version's) image would put a foreign header and continuation into
     * this machine: reject it before anything is overwritten. */
    if (!z_save_matches_story((const zByte *)data))
        return 4; /* Save belongs to a different story */
#endif

    /* Validated: now restore dynamic memory, stack and base pointer */
    Z_FLAGS2_KEEP();
    memcpy(G->memory_state.ram_buffer, data, ram_size);
    Z_FLAGS2_RETURN();
    memcpy(G->stack_mem, ptr, stack_size * sizeof(zWord));
    ptr += stack_size * sizeof(zWord);
    memcpy(&G->base_ptr, ptr, sizeof(G->base_ptr));

    /* Set program counter and stack pointer */
    G->logical_pc = pc_addr;
    G->stack_ptr = G->stack_mem + stack_size;

#if ZVIBE_VERIFY_SAVE_IDENTITY
    z_init_interpreter_header();    /* interpreter-owned header fields describe this interpreter */
#endif
    
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
    if (!data || size == 0)
        return 1; /* Invalid arguments */

    if (size < 64)
        return 2; /* Too small for Z-machine header */

    if (size > Z_STORY_MEM_SIZE)
        return 2; /* Story data too large */

#if ZVIBE_ENABLE_V4
    /* In a dual build the check above uses the V4 limit; a V3 story is still
     * capped at the V3 limit. Done before any state is touched, so a rejected
     * load leaves the machine as it was. A V3-only build already enforced the
     * V3 limit above and compiles none of this. */
    if (((const uint8_t *)data)[0] == 3 && size > Z3_STORY_LIMIT)
        return 2; /* V3 story data too large */
#endif

    /* Initialize state with new story */
    G->story_size = (zSize)size;
#if ZVIBE_ENABLE_V4
    z_reset_output_state();
    G->char_requested = 0;
#endif
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
    if (!Z_VERSION_SUPPORTED(G->header.version))
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

#if ZVIBE_ENABLE_V4
    /* Dynamic memory must hold the whole 64-byte header: the interpreter writes its
     * identity and screen size into it. (V3 loading is left exactly as it was.) */
    if (Z_IS_V4() && G->header.staticmem_addr < 0x40)
        return 5; /* Corrupt header */
#endif

    /* Validate game fits in buffer with room for minimum stack */
    size_t min_stack_bytes = Z_LEGACY_STACK_RESERVE_BYTES;
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
    z_init_interpreter_header();

    /* Set up program counter */
    G->logical_pc = (zDWord)G->header.pc_start;

    return 0; /* Success */
}
