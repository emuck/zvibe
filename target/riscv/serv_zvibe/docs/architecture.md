# System Architecture

Servant ZVibe is a minimal RISC-V SoC implementing Execute-In-Place (XIP) from
on-chip or external flash. Two boards are supported with a shared RTL core and
board-specific wrappers.

## Block Diagram

```
┌─────────────────────────────────────┐
│         RISC-V CPU (SERV)           │
│         32-bit, RV32I               │
│         ~200 LUTs                   │
└─────────────┬───────────────────────┘
              │ Wishbone B4 (30-bit address)
    ┌─────────┼─────────┬─────────────┬────────────┐
    │         │         │             │            │
┌───▼───┐ ┌───▼───┐ ┌───▼────┐  ┌─────▼─────┐  ┌───▼──────────────┐
│  RAM  │ │ UART  │ │ Timer  │  │   GPIO    │  │  Flash XIP       │
│ 32KB  │ │115200 │ │ 32-bit │  │  LEDs     │  │  MAX10: 172KB UFM│
│ BRAM  │ │ TX/RX │ │ count  │  │           │  │  Arty:  16MB QSPI│
└───────┘ └───────┘ └────────┘  └───────────┘  └──────────────────┘
```

## Boards

| | MAX10 08 Eval | Arty S7-50 |
|-|--------------|------------|
| FPGA | 10M08SAU169C8G | XC7S50-CSGA324 |
| Clock | 100 MHz (PLL from 50 MHz) | 166.66 MHz (MMCM from 100 MHz) |
| Flash | 172KB on-chip UFM | 16MB external QSPI (S25FL128S) |
| Boot | XIP direct (PC=0x80000100) | XIP direct (PC=0x80100100) |
| LEs / LUTs | ~2,800 LEs (35% of 8,000) | ~1,445 LUTs (4.4% of 32,600) |
| BRAM | 31×M9K (67% of 46) | 8.5 RAMB tiles |
| Timing WNS | +0.946 ns setup @ 100 MHz | +0.288 ns @ 166.66 MHz |

## Components

### CPU: SERV
- **Design**: Bit-serial RISC-V core by Olof Kindgren
- **ISA**: RV32I (base integer instruction set)
- **Size**: ~200 LUTs
- **CPI**: ~32 cycles per instruction (bit-serial execution)
- **Effective throughput**: ~3 MIPS @ 100 MHz (MAX10), ~5 MIPS @ 166.66 MHz (Arty)
- **Repository**: https://github.com/olofk/serv

### RAM: 32KB
- **Size**: 32KB — data, stack, and `.ramfunc` sections only
- **MAX10**: Intel M9K blocks
- **Arty**: Xilinx RAMB36E1 + RAMB18E1 (8.5 tiles)
- Code executes in-place from flash; RAM is never used for instruction fetch

### UART
- **Baud Rate**: 115200 (fixed), 8N1
- **FIFOs**: 64 bytes TX, 64 bytes RX
- **Status**: TX_READY polling flag
- **Size**: ~50 LUTs

See [rtl/uart.md](rtl/uart.md) for detailed specification.

### Timer
- **Type**: 32-bit up-counter
- **Clock**: System clock
- **Registers**: Single 32-bit read-only counter

### GPIO
- **MAX10**: 5 LED outputs
- **Arty**: 4 LED outputs
- **Control**: 32-bit write register, bits [n:0] map to LEDs

### Flash Interface

**MAX10 — UFM (User Flash Memory):**
- On-chip 172KB, organised as 2KB pages
- XIP read and runtime write/erase via unified Wishbone controller
- RTL: `common/rtl/ufm/max10_ufm_unified.sv`

**Arty — QSPI (S25FL128S):**
- External 16MB flash; 15MB user region (0x100000 onward)
- XIP read via QIOR (0xEB) Quad I/O Read, ~150 LUTs
- 4KB BRAM direct-mapped cache for XIP reads (2.7× median speedup)
- Write/erase via SPI-mode register interface
- RTL: `common/rtl/qspi/s25fl_xip.sv`, `s25fl_write.sv`, `qspi_mux.sv`, `qspi_cache_bram.sv`

See [rtl/qspi_xip.md](rtl/qspi_xip.md) and [rtl/qspi_write.md](rtl/qspi_write.md) for Arty flash specs.

## Memory Maps

**MAX10:**

| Address Range         | Size   | Component        |
|-----------------------|--------|------------------|
| 0x00000000-0x00007FFF | 32KB   | RAM              |
| 0x40000000-0x4000000F | 16B    | UART             |
| 0x40000010-0x4000001F | 16B    | Timer            |
| 0x40000020-0x4000002F | 16B    | GPIO (5 LEDs)    |
| 0x40000040-0x4000005F | 32B    | UFM write ctrl   |
| 0x80000000-0x8002AFFF | 172KB  | UFM XIP          |

**Arty S7-50:**

| Address Range         | Size   | Component        |
|-----------------------|--------|------------------|
| 0x00000000-0x00007FFF | 32KB   | RAM              |
| 0x40000000-0x4000000F | 16B    | UART             |
| 0x40000010-0x4000001F | 16B    | Timer            |
| 0x40000020-0x4000002F | 16B    | GPIO (4 LEDs)    |
| 0x40000030-0x4000003F | 16B    | Flash status (R) |
| 0x40000040-0x4000005F | 32B    | QSPI write ctrl  |
| 0x80000000-0x80FFFFFF | 16MB   | QSPI XIP         |

### Peripheral Register Map

#### UART (0x40000000)
| Offset | Register | Access | Description        |
|--------|----------|--------|--------------------|
| 0x00   | TX_DATA  | W      | Transmit data byte |
| 0x00   | RX_DATA  | R      | Receive data byte  |
| 0x04   | STATUS   | R      | TX_READY (bit 0)   |

#### Timer (0x40000010)
| Offset | Register | Access | Description          |
|--------|----------|--------|----------------------|
| 0x00   | COUNTER  | R      | 32-bit counter value |

#### GPIO (0x40000020)
| Offset | Register | Access | Description                 |
|--------|----------|--------|-----------------------------|
| 0x00   | LED      | W      | LED bits [4:0] MAX10 / [3:0] Arty |

## Wishbone Interconnect

- **Standard**: Wishbone B4 (classic, single cycle)
- **Address Width**: 30 bits (word-addressed)
- **Data Width**: 32 bits
- **Granularity**: 8 bits

### Address Decoding

| Address Bits [31:28] | Component   |
|----------------------|-------------|
| 0x0                  | RAM         |
| 0x4                  | Peripherals |
| 0x8                  | Flash XIP   |

## Boot Sequence

**MAX10 (XIP direct):**
1. CPU resets at PC = 0x80000100 (first instruction in UFM)
2. `xip_start.S` copies `.ramfunc` and `.data` sections to RAM
3. Jumps to `main()` — firmware running in-place from UFM

**Arty S7-50 (XIP direct):**
1. FPGA loads bitstream from QSPI 0x000000
2. CPU resets at PC = 0x80100100 (QSPI user region + 0x100 firmware offset)
3. `xip_start.S` copies `.ramfunc` and `.data` sections to RAM
4. Jumps to `main()` — firmware running in-place from QSPI

Both boards use the same `xip_start.S` / `xip_link.ld`; only `__flash_origin`
differs (0x80000100 vs 0x80100100).

## Execute-In-Place (XIP)

XIP allows code to execute directly from flash without copying to RAM.

### Benefits
- **Large code space**: 172KB (MAX10) or 15MB (Arty) vs 32KB RAM
- **Small RAM footprint**: RAM used only for data and stack
- **Fast boot**: No copy delay for code

### Memory Sections
- `.text` / `.rodata` — stay in flash (XIP)
- `.ramfunc` — time-critical ISRs, copied to RAM at startup by `xip_start.S`
- `.data` — initialised globals, copied to RAM at startup
- `.bss` — zero-initialised globals, cleared in RAM

## Flash Layout (ZVIF)

Both boards use the same ZVIF image format:

```
Physical offset   Content
───────────────────────────────────────────────────
0x000000          ZVIF metadata header (256 B)
0x000100          Firmware entry point (XIP reset PC)
varies            ZVGM TOC v2 (16B header + 80B/game)
varies            Game data
varies            Per-game save rings
───────────────────────────────────────────────────
```

MAX10 physical offsets map directly to XIP addresses (base 0x80000000).
Arty physical offsets are within the QSPI user region (base 0x80100000).

## Clock Distribution

**MAX10:**
- 50 MHz external oscillator → PLL → 100 MHz system clock

**Arty S7-50:**
- 100 MHz external oscillator → MMCM → 166.66 MHz system clock
- QSPI clock derived from system clock by flash controller (÷6 = 27.8 MHz)

Both designs implement a power-on reset counter to ensure reliable startup
without requiring a manual reset after FPGA configuration.

## Design Files

### Common RTL (`common/rtl/`)
- `servant_zvibe.sv` — Parameterized SoC core (shared)
- `servant_zvibe_mux.sv` — Address decode / peripheral bus
- `servant_mem_mux.sv` — Memory subsystem
- `servant_ram.sv` — 32KB BRAM
- `uart/uart_wb.sv`, `uart_tx.sv`, `uart_rx.sv`, `fifo_sync.sv` — UART controller
- `ufm/max10_ufm_unified.sv` — UFM XIP + runtime write/erase controller (MAX10)
- `qspi/s25fl_xip.sv` — QSPI XIP controller (Arty)
- `qspi/s25fl_write.sv` — QSPI write controller (Arty)
- `qspi/qspi_cache_bram.sv` — 4KB BRAM direct-mapped cache (Arty)

### Board Wrappers
- `boards/max10_08_eval/rtl/` — MAX10 top-level, PLL, pin assignments
- `boards/arty_s7_50/rtl/` — Arty top-level, MMCM, POR, pin assignments

## References

- [SERV CPU](https://github.com/olofk/serv)
- [Wishbone B4 Specification](https://cdn.opencores.org/downloads/wbspec_b4.pdf)
- [RISC-V ISA Specification](https://riscv.org/technical/specifications/)
- [Intel MAX10 UFM User Guide](https://www.intel.com/content/www/us/en/docs/programmable/683180)
- [Arty S7 Reference Manual](https://digilent.com/reference/programmable-logic/arty-s7/reference-manual)
