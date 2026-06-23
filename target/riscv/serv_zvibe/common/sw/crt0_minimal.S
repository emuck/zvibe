/*
 * Minimal startup code for SERV
 *
 * IMPORTANT: This version avoids CSR writes to prevent traps
 * SERV RV32I may have limited CSR support
 */

.section .text.start
.globl _start
_start:
    /* Set up stack pointer at top of 32KB RAM */
    /* RAM: 0x00000000 - 0x00007FFF (32KB) */
    /* CRITICAL: 32KB = 0x8000 bytes, last valid address is 0x7FFF */
    /* Stack pointer should point to first byte AFTER last valid address */
    /* (stacks grow downward, so first push decrements sp first) */
    lui sp, 0x8         /* sp = 0x8000 - one byte past end (standard practice) */

    /* Note: sp is already 16-byte aligned (0x8000) */
    /* No need for andi sp, sp, -16 */

    /* Zero BSS section (if we had one) */
    /* Skip for now - we don't use global variables */

    /* Jump to main */
    call main

    /* If main returns, hang forever */
_hang:
    j _hang

/*
 * Minimal trap handler
 * If we get a trap, just hang (for debugging)
 */
.align 4
.globl trap_handler
trap_handler:
    /* Save current PC for debugging (if possible) */
    /* For now, just hang */
    j trap_handler
