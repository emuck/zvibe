MEMORY { ram (rwx) : ORIGIN = 0x00000000, LENGTH = 32K }
SECTIONS {
    .text 0x00000000 : { *(.text*) } > ram
    /DISCARD/ : { *(.eh_frame) *(.comment) }
}
