# Servant ZVibe (serv_zvibe) — RISC-V SoC with XIP

Minimal RISC-V SoC platform with Execute-In-Place (XIP) from flash. Modular multi-board
architecture supporting various FPGA targets.

**Directory Note**: Renamed from `servant_zvibe` to `serv_zvibe` (Feb 2026) to align with
SERV CPU naming and enable future CPU variants (e.g., `vexriscv_zvibe`). SERV submodule
moved inside this directory.

## Overview

- **CPU**: SERV (bit-serial RV32I, ~200 LUTs)
- **RAM**: 32KB BRAM for data/stack
- **Flash**: On-chip UFM (MAX10) or external QSPI (Arty) for XIP execution and saves
- **Peripherals**: UART (115200 baud), Timer, GPIO
- **Application**: ZVibe Z-machine interpreter with SAVE/RESTORE support
- **Architecture**: Common RTL/firmware core with thin board-specific wrappers

## Supported Boards

| Board | FPGA | Clock | Flash | Notes |
|-------|------|-------|-------|-------|
| MAX10 08 Eval | 10M08SAU169C8G | 100 MHz | 172KB UFM | XIP direct boot, save/restore verified |
| Arty S7-50 | XC7S50-CSGA324 | 166.66 MHz | 16MB QSPI | Multi-game (24 games), save/restore verified |

See `boards/` for board-specific implementations.

## First-Time Setup — SERV Submodule

The SERV CPU is a git submodule.  Initialize it once from the **repo root**:

```bash
git submodule update --init target/riscv/serv_zvibe/serv
```

> The submodule path must be specified in full from the repo root.
> Running `git submodule update --init serv` from inside this directory
> will not work — git resolves submodule paths relative to the repo root.

Makefiles auto-initialize the submodule if it is missing, but running
the command explicitly is the reliable first-time path.

## Directory Structure

```
serv_zvibe/
├── README.md                    # This file
├── docs/                        # Documentation
│   ├── quickstart.md
│   ├── build.md
│   ├── architecture.md
│   └── archive/                 # Historical development docs
│
├── common/                      # Shared across all boards
│   ├── rtl/                     # Board-independent RTL
│   │   ├── servant_zvibe.sv     # Parameterized SoC core
│   │   ├── servant_zvibe_mux.sv # Address decode / peripheral bus
│   │   ├── servant_mem_mux.sv   # Memory subsystem
│   │   ├── servant_ram.sv       # 32KB BRAM
│   │   ├── uart/                # UART controller
│   │   │   ├── uart_wb.sv       #   Wishbone slave, FIFOs, error flags
│   │   │   ├── uart_tx.sv       #   TX serializer
│   │   │   ├── uart_rx.sv       #   RX deserializer
│   │   │   └── fifo_sync.sv     #   64-byte synchronous FIFO
│   │   ├── gpio_leds.sv         # GPIO peripheral
│   │   ├── ufm/                 # UFM XIP + write controller (MAX10)
│   │   │   └── max10_ufm_unified.sv  # Unified XIP/write/erase controller
│   │   └── qspi/                # QSPI flash controllers (Arty)
│   │       ├── s25fl_xip.sv
│   │       ├── s25fl_write.sv
│   │       ├── qspi_cache_bram.sv  # 4KB BRAM direct-mapped cache
│   │       └── qspi_mux.sv
│   │
│   ├── sw/                      # Shared firmware
│   │   ├── zvibe_riscv_multi.c  # Z-machine interpreter
│   │   ├── flash_save.c         # Save/restore with wear leveling
│   │   ├── save_delta.c         # Delta compression
│   │   ├── game_registry_riscv.c# ZVGM game registry
│   │   ├── menu_system.c        # Multi-game menu
│   │   ├── flash_metadata.c     # ZVIF serialization
│   │   ├── zvibe_flash_lib.py   # Shared ZVIF/ZVGM Python library
│   │   └── xip_link.ld          # XIP linker script
│   │
│   └── tb/                      # Shared testbenches
│       ├── unit/                # Unit tests (flash, UART, mux)
│       └── models/              # Behavioral models
│
├── boards/
│   ├── max10_08_eval/           # Intel MAX10 08 Evaluation Board
│   │   ├── README.md
│   │   ├── TESTING.md
│   │   ├── build_flash.py       # ZVIF flash image builder
│   │   ├── rtl/                 # Board wrapper
│   │   ├── fpga/                # Quartus build + programming
│   │   ├── sw/                  # Board-specific firmware config
│   │   ├── tb/                  # UFM unit + SoC XIP testbenches
│   │   ├── sim/                 # Questa simulation (vendor UFM model)
│   │   └── tests/               # Hardware UART test
│   │
│   └── arty_s7_50/              # Digilent Arty S7-50
│       ├── README.md
│       ├── TESTING.md
│       ├── build_flash.py       # ZVIF flash image builder
│       ├── rtl/                 # Board wrapper
│       ├── fpga/                # Vivado build + programming
│       ├── sw/                  # Board-specific firmware config
│       ├── tb/                  # Arty SoC XIP + flash write testbenches
│       └── tests/               # Hardware UART test
│
└── serv/                        # SERV CPU submodule
    ├── rtl/serv_*.v
    ├── servile/servile.v
    └── servant/servant_timer.v
```

## Required Tools

| Tool | MAX10 | Arty | Purpose |
|------|-------|------|---------|
| `riscv64-unknown-elf-gcc` | Yes | Yes | Firmware compiler |
| Python 3.8+ | Yes | Yes | Flash image builders |
| Quartus Prime (Lite/Standard) | Yes | — | FPGA synthesis + programming (`quartus_sh`, `quartus_pgm`) |
| USB-Blaster driver | Yes | — | JTAG programming |
| Vivado 2024.x / 2025.1 | — | Yes | FPGA synthesis |
| openFPGALoader | — | Yes | Flash programming |
| Questa FSE / `vsim` | sim only | — | Vendor UFM model simulation |
| Verilator 5.x | Yes | Yes | RTL unit tests |
| picocom or screen | Yes | Yes | UART terminal |

See [`docs/build.md`](docs/build.md) for installation hints and troubleshooting.

## Quick Start

### MAX10 08 Eval Board

```bash
# Build FPGA bitstream (one-time, ~10-20 min)
cd boards/max10_08_eval/fpga
make build

# Build firmware + flash image + program everything (~11s)
make program-complete GAME=restaurant   # default (ships with repo)
make program-complete GAME=czech        # bundled test suite
# make program-complete GAME=zork1     # any downloaded game

# Test
cd ../tests
python3 test_uart.py             # Replays 232-command game script
```

See [`boards/max10_08_eval/README.md`](boards/max10_08_eval/README.md) for full details.

### Arty S7-50

```bash
# Build FPGA bitstream (one-time, ~5-10 min, requires Vivado)
cd boards/arty_s7_50/fpga
make build

# Build firmware + flash image + program everything
make program-complete GAME=restaurant        # default (ships with repo)
# make program-complete GAME=zork1          # any downloaded game

# Test
cd ../tests
python3 test_uart.py
```

See [`boards/arty_s7_50/README.md`](boards/arty_s7_50/README.md) for full details.

## Flash Layout (ZVIF)

Both platforms use the ZVIF (ZVibe Image Format) flash layout:

```
Offset     Content
────────────────────────────────────────────
0x000000   ZVIF metadata header (256 B)
0x000100   Firmware entry point
varies     ZVGM TOC v2 (16B header + 80B/game)
varies     Game data
varies     Per-game save rings (offset in TOC entry)
```

Exact offsets are computed at build time by each board's `build_flash.py`
(which imports the shared `common/sw/zvibe_flash_lib.py`).

## Memory Maps

**MAX10:**
```
0x00000000 - 0x00007FFF    32KB RAM
0x40000000 - 0x4000002F    Peripherals (UART, Timer, GPIO)
0x80000000 - 0x8002AFFF    172KB UFM XIP
```

**Arty S7-50:**
```
0x00000000 - 0x00007FFF    32KB RAM
0x40000000 - 0x4000002F    Peripherals (UART, Timer, GPIO)
0x80000000 - 0x80FFFFFF    16MB QSPI XIP
```

## Simulation

### Tier 1 — Verilator (no vendor tools required)

Run from the repo root or from this directory:

```bash
# From repo root:
make sim-tier1

# Or directly:
./run_regression.sh --tier1
```

Runs 13 tests covering UART, address decode, QSPI XIP, BRAM cache, UFM bridge, MAX10/Arty board-level SoC, and flash write arbitration. Each emits `PASS: <name>` on success. Logs go to `regression_logs/`.

### Tier 2 — Questa (vendor UFM model; requires `vsim`)

```bash
make sim-questa          # From repo root
# or:
./run_regression.sh --tier2-questa
```

Runs 2 tests against the Intel vendor UFM model: UART echo XIP boot, UFM write/verify. See `boards/max10_08_eval/sim/README.md` for prerequisites.

See [`common/tb/README.md`](common/tb/README.md) for the full testbench inventory.

## Board Comparison

| Aspect | MAX10 08 Eval | Arty S7-50 |
|--------|--------------|------------|
| Flash | 172KB on-chip UFM | 16MB external QSPI |
| Boot | XIP direct (PC=0x80000100) | XIP direct (PC=0x80100100) |
| Save slot size | 2KB (UFM page) | 64KB (QSPI sector) |
| Save slots | all remaining UFM (2KB/slot) | 4 per game (64KB/slot) |
| Simulation | Questa + Intel vendor model | Verilator + xsim |
| Programming tool | Quartus quartus_pgm | openFPGALoader |

## Documentation

- [`boards/max10_08_eval/README.md`](boards/max10_08_eval/README.md) — MAX10 specs, build, programming
- [`boards/max10_08_eval/TESTING.md`](boards/max10_08_eval/TESTING.md) — MAX10 test results
- [`boards/arty_s7_50/README.md`](boards/arty_s7_50/README.md) — Arty specs, build, programming
- [`boards/arty_s7_50/TESTING.md`](boards/arty_s7_50/TESTING.md) — Arty test results
- [`common/tb/README.md`](common/tb/README.md) — Simulation testbenches
- [`boards/max10_08_eval/sim/QUESTA_UFM_INIT_GUIDE.md`](boards/max10_08_eval/sim/QUESTA_UFM_INIT_GUIDE.md) — Questa UFM setup

## License

- **RTL & Firmware**: BSD-3-Clause (see LICENSE)
- **SERV CPU**: ISC License (see serv/LICENSE)
- **Servile**: Apache-2.0 License (see serv/LICENSE)
- **UART (alexforencich)**: MIT License (attributed in source files)
- **ZVibe Core**: BSD-3-Clause (see ../../../../LICENSE)
- **Bundled Czech story data** retains its own license; see the root `NOTICE` and
  `games/catalog/CZECH-NOTICE.txt`, and include it with any image that contains Czech

## References

- [SERV CPU](https://github.com/olofk/serv) — World's smallest RISC-V
- [ZVibe Project](../../../../README.md) — Main project README
