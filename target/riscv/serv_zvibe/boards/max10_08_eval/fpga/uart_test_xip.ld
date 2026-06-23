/* Linker script for UFM XIP test - execute directly from UFM */

MEMORY
{
    UFM (rx) : ORIGIN = 0x80000000, LENGTH = 172K
}

SECTIONS
{
    .text 0x80000000 : {
        *(.text)
        *(.text*)
        *(.rodata)
        *(.rodata*)
    } > UFM

    /DISCARD/ : {
        *(.eh_frame)
        *(.comment)
    }
}
