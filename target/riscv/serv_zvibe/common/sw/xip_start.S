/*
 * XIP Startup Code for Servant ZVibe
 *
 * This code runs from flash (origin set per-platform via --defsym=__flash_origin) and initializes:
 * - Stack pointer
 * - RAM functions (copy code that must run from RAM, e.g., flash write)
 * - Data section (copy initialized data from flash to RAM)
 * - BSS section (zero initialization)
 */

.section .text.start
.global _start

_start:
    /* Set up stack pointer to top of RAM */
    lui sp, %hi(__stack_end)
    addi sp, sp, %lo(__stack_end)

    /* Copy .ramfunc section from flash (LMA) to RAM (VMA) */
    /* This is code that must run from RAM (e.g., flash write operations) */
    lui a0, %hi(__ramfunc_start)
    addi a0, a0, %lo(__ramfunc_start)
    lui a1, %hi(__ramfunc_end)
    addi a1, a1, %lo(__ramfunc_end)
    lui a2, %hi(__ramfunc_source_start)
    addi a2, a2, %lo(__ramfunc_source_start)

ramfunc_copy_loop:
    beq a0, a1, ramfunc_copy_done
    lw t0, 0(a2)
    sw t0, 0(a0)
    addi a0, a0, 4
    addi a2, a2, 4
    j ramfunc_copy_loop

ramfunc_copy_done:
    /* Copy .data section from flash (LMA) to RAM (VMA) */
    lui a0, %hi(__data_start)
    addi a0, a0, %lo(__data_start)
    lui a1, %hi(__data_end)
    addi a1, a1, %lo(__data_end)
    lui a2, %hi(__data_source_start)
    addi a2, a2, %lo(__data_source_start)

data_copy_loop:
    beq a0, a1, data_copy_done
    lw t0, 0(a2)
    sw t0, 0(a0)
    addi a0, a0, 4
    addi a2, a2, 4
    j data_copy_loop

data_copy_done:
    /* Clear BSS section */
    lui a0, %hi(__bss_start)
    addi a0, a0, %lo(__bss_start)
    lui a1, %hi(__bss_end)
    addi a1, a1, %lo(__bss_end)

bss_clear_loop:
    beq a0, a1, bss_clear_done
    sw zero, 0(a0)
    addi a0, a0, 4
    j bss_clear_loop

bss_clear_done:
    /* Jump to main */
    jal ra, main

    /* If main returns, loop forever */
_exit:
    j _exit

/* Default exception/interrupt handler */
.global _irq_handler
_irq_handler:
    j _irq_handler
