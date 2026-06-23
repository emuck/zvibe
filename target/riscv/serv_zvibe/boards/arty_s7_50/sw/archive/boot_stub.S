/* boot_stub.S — Arty S7-50 minimal boot stub
 *
 * Runs from RAM at 0x00000000 (SERV reset_pc default).
 * Preloaded into RAM via servant_zvibe memfile parameter.
 *
 * The QSPI XIP controller initializes itself in hardware during FPGA
 * configuration. A short spin loop provides margin before jumping to XIP.
 *
 * Flash layout:
 *   ZVIF header:   physical 0x100000  →  XIP 0x80100000
 *   Firmware:      physical 0x100100  →  XIP 0x80100100  ← entry point
 */

#define XIP_FIRMWARE_ENTRY  0x80100100
#define STACK_TOP           0x00008000   /* top of 32KB RAM */
#define BOOT_DELAY_CYCLES   2000

    .section .text.start, "ax"
    .global _start
_start:
    /* Set up stack */
    li      sp, STACK_TOP

    /* Brief spin to let the QSPI XIP controller finish its startup FSM */
    li      t0, BOOT_DELAY_CYCLES
1:  addi    t0, t0, -1
    bnez    t0, 1b

    /* Jump to firmware in XIP flash */
    li      t0, XIP_FIRMWARE_ENTRY
    jr      t0
