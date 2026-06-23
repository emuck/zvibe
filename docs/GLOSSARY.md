# Glossary

Canonical terminology for the ZVibe project.

## Components

| Term | Definition |
|------|------------|
| ZVibe | Z-machine interpreter project |
| story file | Z-machine game data file (`.z3` format for version 3) |
| Z-machine | Virtual machine specification for running Infocom games |

## Memory Model

| Term | Definition |
|------|------------|
| dynamic memory | Writable region from address 0 to `staticmem_addr`. Stored in RAM. |
| static memory | Read-only region from `staticmem_addr` to end of story. May be accessed directly from flash. |
| staticmem_addr | Boundary address between dynamic and static memory. Read from story file header at offset 0x0E. |
| shared buffer | Single memory allocation holding dynamic memory at bottom and stack at top. Size controlled by `ZVIBE_MEMORY_BUFFER_SIZE`. |
| split memory | Architecture where dynamic memory lives in RAM while static memory is accessed from flash/ROM. Reduces RAM usage by 85-95%. |

## RISC-V / FPGA

| Term | Definition |
|------|------------|
| XIP | Execute-In-Place. CPU fetches instructions directly from flash memory. |
| QSPI | Quad Serial Peripheral Interface. Four-wire flash interface. |
| UFM | User Flash Memory. On-chip non-volatile storage in Intel MAX10 FPGAs. |
| continuous read mode | Flash optimization that skips command byte on sequential reads. Provides ~2.4x speedup. |
| boot stub | Small code in RAM that jumps to flash entry point. Used on Arty S7-50 where CPU resets to RAM address 0. MAX10 boots directly from UFM (XIP direct). |
| SERV | Bit-serial RISC-V CPU core. External project at https://github.com/olofk/serv |
| ZVIF | ZVibe Image Format. Flash layout standard used by both boards: 256-byte header, firmware, ZVGM TOC, game data, per-game save rings. |
| ZVGM TOC | ZVibe Game Manager Table of Contents. Binary index in flash listing game names, offsets, sizes, and per-game save ring locations. |

## Save System

| Term | Definition |
|------|------------|
| delta compression | Save format storing only differences from original story data. Reduces save size and flash wear. |
| wear leveling | Distribution of flash writes across sectors to extend device lifespan. |
| save slot | Logical location for a saved game. Multiple slots per game supported. |

## Multi-Game System

| Term | Definition |
|------|------------|
| game registry | JSON file (`registry.json`) listing available games with metadata. |
| TOC | Table of Contents. Binary header in flash listing game locations and sizes. |
| menu system | Interactive game selection UI on embedded targets. |

## Build Artifacts

| Artifact | Description |
|----------|-------------|
| `zvibe_console` | Console executable with full features |
| `zvibe_minimal` | Console executable without save support |
| `zvibe_riscv_multi.bin` | Multi-game RISC-V firmware |
| `games.bin` | TOC and concatenated game data |
| `flash_image_multi.bin` | Combined firmware and games for flash programming |

## Memory Addresses (RISC-V)

Both boards share the same peripheral and RAM addresses. Flash XIP starts at `0x80000000`; firmware entry points differ by board.

| Address | Region |
|---------|--------|
| `0x00000000` | RAM base (32KB) |
| `0x40000000` | Peripheral base (UART, timer, GPIO) |
| `0x80000000` | Flash XIP base (MAX10 UFM and Arty QSPI) |
| `0x80000100` | Firmware entry point — MAX10 (UFM XIP direct) |
| `0x80100100` | Firmware entry point — Arty S7-50 (QSPI, after boot stub) |

## Targets

| Name | Platform |
|------|----------|
| console | Desktop Linux, macOS, Windows (native build) |
| windows | Windows (cross-compiled from WSL2) |
| same51 | SAM E51 Curiosity Nano (ARM Cortex-M4) |
| riscv | RISC-V FPGA (SERV on Intel MAX10 08 Eval or Digilent Arty S7-50) |

## File Formats

| Extension | Description |
|-----------|-------------|
| `.z3` | Z-machine version 3 story file |
| `.bin` | Raw binary (firmware, flash images) |
| `.hex` | Intel HEX or Verilog hex format |
| `.bit` | Xilinx FPGA bitstream |
