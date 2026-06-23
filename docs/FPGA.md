# FPGA Support

RISC-V SoC implementation running ZVibe on two FPGA targets.

## Supported Hardware

| Board | FPGA | Vendor | Status | Notes |
|-------|------|--------|--------|-------|
| Intel MAX10 08 Eval | 10M08SAU169C8G | Intel | ✅ Primary | 100 MHz, 172KB on-chip UFM |
| Digilent Arty S7-50 | XC7S50-CSGA324-1 | Xilinx | ✅ Supported | 166.66 MHz, 16MB QSPI flash |

## System Architecture

Both boards share the same common RTL core (`servant_zvibe.v`) with thin board-specific wrappers.

```
┌──────────────────────────────────────────────────────────┐
│                    servant_zvibe SoC                     │
│                                                          │
│  ┌─────────┐  ┌────────┐  ┌──────┐  ┌───────┐  ┌──────┐  │
│  │  SERV   │  │ 32KB   │  │ UART │  │ Timer │  │ GPIO │  │
│  │  CPU    │  │  BRAM  │  │115200│  │       │  │ LEDs │  │
│  │ (RV32I) │  │        │  │ baud │  │       │  │      │  │
│  └────┬────┘  └────────┘  └──────┘  └───────┘  └──────┘  │
│       │              Wishbone Bus                        │
│       └──────────────────────────────────────────┐       │
│                                                  │       │
│  ┌──────────────────────┐  ┌─────────────────────┴────┐  │
│  │    MAX10 only:       │  │      Arty only:          │  │
│  │  UFM XIP bridge      │  │  QSPI XIP + write ctrl   │  │
│  │  (172KB on-chip UFM) │  │  (16MB S25FL128S)        │  │
│  └──────────────────────┘  └──────────────────────────┘  │
└──────────────────────────────────────────────────────────┘
```

## Memory Maps

**MAX10:**
```
0x00000000 - 0x00007FFF    32KB RAM
0x40000000 - 0x4000000F    UART
0x40000010 - 0x4000001F    Timer
0x40000020 - 0x4000002F    GPIO LEDs
0x80000000 - 0x8002AFFF    172KB UFM XIP
```

**Arty S7-50:**
```
0x00000000 - 0x00007FFF    32KB RAM
0x40000000 - 0x4000000F    UART
0x40000010 - 0x4000001F    Timer
0x40000020 - 0x4000002F    GPIO LEDs
0x80000000 - 0x80FFFFFF    16MB QSPI XIP
```

## Flash Layout (ZVIF)

Both boards use the same ZVIF (ZVibe Image Format):

```
Physical    Content
──────────────────────────────────────────────────────
0x000000    ZVIF metadata header (256 B)
0x000100    Firmware entry point (XIP reset PC)
varies      ZVGM TOC v2 (16B header + 80B per game)
varies      Game data (concatenated)
varies      Per-game save rings (offset stored in TOC entry)
──────────────────────────────────────────────────────
```

**MAX10**: firmware + 1 game + save slots fill the 172KB UFM.
**Arty**: FPGA bitstream at 0x000000; user region (ZVIF) at 0x100000 in 16MB QSPI.

## Boot Sequence

**MAX10 (XIP direct):**
1. CPU resets at PC = 0x80000100 (UFM XIP)
2. Firmware runs in-place from UFM
3. Reads ZVIF metadata, discovers game from ZVGM TOC
4. Auto-launches single game

**Arty S7-50 (boot stub → XIP):**
1. FPGA loads bitstream from QSPI 0x000000
2. CPU resets at PC = 0x00000000 (RAM); boot stub preloaded via SERV `memfile`
3. Boot stub waits for QSPI XIP controller startup, jumps to 0x80100100
4. Firmware runs XIP from QSPI
5. Reads ZVIF metadata, shows game menu (multi-game) or auto-launches (single game)

## Build and Programming

### Prerequisites

| Tool | MAX10 | Arty |
|------|-------|------|
| `riscv64-unknown-elf-gcc` | ✅ | ✅ |
| Quartus Prime (Lite/Standard) | ✅ | — |
| Vivado 2024.x / 2025.1 | — | ✅ |
| openFPGALoader | — | ✅ |

### MAX10

```bash
cd target/riscv/serv_zvibe/boards/max10_08_eval

# One-time: generate UFM IP
(cd fpga && make generate-ip)

# Build bitstream (~15 min)
(cd fpga && make build)

# Build firmware + program everything (~11s)
(cd fpga && make program-complete)
```

### Arty S7-50

```bash
cd target/riscv/serv_zvibe/boards/arty_s7_50

# Build bitstream (~5-10 min)
(cd fpga && make build)

# Single game
(cd sw && make flash-test)
(cd fpga && make program-complete)

# All 24 catalog games
(cd sw && make all-games)
(cd fpga && make program-flash ARTY_FLASH=../sw/arty_flash_all.bin)
```

## Resource Utilization

| Resource | MAX10 | Arty S7-50 | Notes |
|----------|-------|------------|-------|
| LEs / LUTs | ~2,800 LEs (35%) | ~1,445 LUTs (4.4%) | SERV + SoC + peripherals |
| Registers | — | ~1,456 | |
| Block RAM | 31×M9K (67%) | 8.5 RAMB tiles | 32KB RAM; Arty +1 BRAM36 for cache |
| Timing (WNS) | +0.946 ns setup @ 100 MHz | +0.288 ns @ 166.66 MHz | Both pass |

Arty figures are with 4KB BRAM cache enabled. See [`boards/arty_s7_50/README.md`](../target/riscv/serv_zvibe/boards/arty_s7_50/README.md) and [`boards/max10_08_eval/TESTING.md`](../target/riscv/serv_zvibe/boards/max10_08_eval/TESTING.md) for exact utilization reports.

## Simulation

**MAX10 (Questa — vendor UFM model required):**
```bash
cd target/riscv/serv_zvibe/boards/max10_08_eval/sim
make -f Makefile.vsim questa-batch   # UART XIP echo test
vsim -c -do run_questa_ufm_write.tcl # UFM write/erase/verify test
```

**Arty (Verilator + xsim):**
```bash
cd target/riscv/serv_zvibe/boards/arty_s7_50/tb/xip
make -f Makefile.xip arty-echo       # Full SoC XIP boot test
```

## SERV CPU

Bit-serial RV32I core, ~200 LUTs, world's smallest RISC-V.

- Submodule: `target/riscv/serv_zvibe/serv/`
- Repository: https://github.com/olofk/serv

## Detailed Documentation

- [`target/riscv/serv_zvibe/docs/build.md`](../target/riscv/serv_zvibe/docs/build.md) — full tool requirements and build steps
- [`target/riscv/serv_zvibe/boards/max10_08_eval/README.md`](../target/riscv/serv_zvibe/boards/max10_08_eval/README.md) — MAX10 details
- [`target/riscv/serv_zvibe/boards/arty_s7_50/README.md`](../target/riscv/serv_zvibe/boards/arty_s7_50/README.md) — Arty details
- [`target/riscv/serv_zvibe/boards/max10_08_eval/sim/QUESTA_UFM_INIT_GUIDE.md`](../target/riscv/serv_zvibe/boards/max10_08_eval/sim/QUESTA_UFM_INIT_GUIDE.md) — Questa setup
