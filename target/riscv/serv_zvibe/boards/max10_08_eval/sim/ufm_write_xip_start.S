/*
 * Startup for UFM Write XIP Test with RAM function copy
 */

.section .text.start
.global _start

_start:
    /* Set up stack pointer */
    lui sp, %hi(_stack_top)
    addi sp, sp, %lo(_stack_top)

    /* Copy .ramfunc from flash to RAM */
    la t0, __ramfunc_source_start   /* Source in flash */
    la t1, __ramfunc_start           /* Destination in RAM */
    la t2, __ramfunc_end             /* End of RAM section */
    beq t1, t2, skip_ramfunc         /* Skip if no ramfunc */
ramfunc_copy_loop:
    lw t3, 0(t0)
    sw t3, 0(t1)
    addi t0, t0, 4
    addi t1, t1, 4
    bne t1, t2, ramfunc_copy_loop
skip_ramfunc:

    /* Copy .data from flash to RAM */
    la t0, __data_source_start       /* Source in flash */
    la t1, __data_start              /* Destination in RAM */
    la t2, __data_end                /* End of RAM section */
    beq t1, t2, skip_data            /* Skip if no data */
data_copy_loop:
    lw t3, 0(t0)
    sw t3, 0(t1)
    addi t0, t0, 4
    addi t1, t1, 4
    bne t1, t2, data_copy_loop
skip_data:

    /* Zero .bss section */
    la t0, __bss_start
    la t1, __bss_end
    beq t0, t1, skip_bss             /* Skip if no bss */
bss_zero_loop:
    sw zero, 0(t0)
    addi t0, t0, 4
    bne t0, t1, bss_zero_loop
skip_bss:

    /* Jump to main */
    call main

    /* Infinite loop if main returns */
1:
    j 1b
