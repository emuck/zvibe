/*
 * Linker script for Servant ZVibe
 *
 * Memory Layout:
 *   0x00000000 - 0x00007FFF : RAM (32KB) - reduced for XIP from flash
 *   0x40000000 - 0x7FFFFFFF : UART registers (Wishbone GPIO space)
 *   0x80000000 - 0x80FFFFFF : QSPI Flash (16MB) for XIP
 */

OUTPUT_ARCH(riscv)
ENTRY(_start)

MEMORY
{
    RAM (rwx) : ORIGIN = 0x00000000, LENGTH = 32K
}

SECTIONS
{
    /* Code and read-only data */
    .text : ALIGN(4) {
        *(.text.start)      /* Startup code first */
        *(.text*)           /* All other code */
        *(.rodata*)         /* Read-only data */
        . = ALIGN(4);
    } > RAM

    /* Initialized data */
    .data : ALIGN(4) {
        *(.data*)
        . = ALIGN(4);
    } > RAM

    /* Uninitialized data */
    .bss : ALIGN(4) {
        __bss_start = .;
        *(.bss*)
        *(COMMON)
        __bss_end = .;
        . = ALIGN(4);
    } > RAM
}
