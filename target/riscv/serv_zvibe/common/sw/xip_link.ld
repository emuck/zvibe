/*
 * XIP Linker Script for Servant ZVibe
 *
 * Execute-in-place from flash (QSPI or UFM) with data/bss in RAM
 *
 * Flash origin is set via linker command line: --defsym=__flash_origin=0xADDR
 * Default value works for Arty S7-50 (1MB offset).
 * Flash layout calculator provides optimal address for each platform.
 *
 * Memory Map:
 *   FLASH: Variable origin - Code (.text, .rodata)
 *   RAM:   0x00000200 - 0x00007FFF - Data (.data, .bss, .stack)
 */

OUTPUT_ARCH( "riscv" )
ENTRY(_start)

/* Flash origin can be overridden via linker command line */
/* Default: Arty S7-50 @ 1MB offset */
__flash_origin = DEFINED(__flash_origin) ? __flash_origin : 0x80100000;

MEMORY
{
  FLASH (rx)  : ORIGIN = __flash_origin, LENGTH = 15M
  RAM   (rwx) : ORIGIN = 0x00000200, LENGTH = 32K - 512
}

PHDRS
{
  text    PT_LOAD FLAGS(5); /* R + X */
  ramfunc PT_LOAD FLAGS(5); /* R + X */
  data    PT_LOAD FLAGS(6); /* R + W */
}

SECTIONS
{
  /* Code and read-only data in flash */
  .text : {
    *(.text.start)      /* Entry point first */
    *(.text*)
    *(.rodata*)
    *(.srodata*)        /* Small read-only data */
    . = ALIGN(4);
  } > FLASH :text

  /* RAM-resident code - stored in flash, copied to RAM at startup */
  /* Used for code that must run while flash is unavailable (e.g., flash writes) */
  .ramfunc : AT (LOADADDR(.text) + SIZEOF(.text)) {
    __ramfunc_start = .;
    *(.ramfunc*)
    . = ALIGN(4);
    __ramfunc_end = .;
  } > RAM :ramfunc

  /* Initialized data - stored in flash, copied to RAM at startup */
  .data : AT (LOADADDR(.ramfunc) + SIZEOF(.ramfunc)) {
    __data_start = .;
    *(.data*)
    *(.sdata*)
    . = ALIGN(4);
    __data_end = .;
  } > RAM :data

  /* Uninitialized data in RAM */
  .bss : {
    __bss_start = .;
    *(.bss*)
    *(.sbss*)
    *(COMMON)
    . = ALIGN(4);
    __bss_end = .;
  } > RAM :data

  /* Stack at top of RAM (grows downward) - NOT a section, just symbols */
  __stack_end = ORIGIN(RAM) + LENGTH(RAM);  /* 0x8000 for 32KB RAM */

  /* Symbols for initialization */
  __ramfunc_source_start = LOADADDR(.ramfunc);
  __ramfunc_size = SIZEOF(.ramfunc);
  __data_source_start = LOADADDR(.data);
  __data_size = SIZEOF(.data);
  __bss_size = SIZEOF(.bss);
}
