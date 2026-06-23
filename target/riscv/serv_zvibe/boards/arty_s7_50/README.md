# Arty S7-50 Board Support

Digilent Arty S7-50 FPGA board running the ZVibe Z-machine interpreter.

## Board Specifications

- **FPGA**: Xilinx Spartan-7 XC7S50-CSGA324-1
- **CPU**: SERV (bit-serial RV32I)
- **System Clock**: 166.66 MHz (100 MHz input × MMCM)
- **UART**: 115200 baud via integrated FTDI FT2232H (typically `/dev/ttyUSB1` or `/dev/ttyUSB2` — second channel of FT2232H; varies by enumeration order)
- **Flash**: S25FL128S/S25FL127S (16MB QSPI)
- **RAM**: 32KB BRAM

## Memory Map

```
0x00000000 - 0x00007FFF    RAM (32KB)
0x40000000 - 0x4000000F    UART
0x40000010 - 0x4000001F    Timer
0x40000020 - 0x4000002F    GPIO LEDs
0x80000000 - 0x80FFFFFF    QSPI XIP (16MB)
```

## Flash Layout (ZVIF-based)

```
Physical    XIP Virtual   Component
────────────────────────────────────────────────────────
0x000000    —             FPGA Bitstream (up to ~4MB)
0x100000    0x80100000    ZVIF metadata header (256 B)
0x100100    0x80100100    Firmware entry point (~34KB)
varies      varies        ZVGM TOC v2 (16 + 80×N bytes)
varies      varies        Game data (concatenated)
varies      varies        Save rings: 4×64KB per game
────────────────────────────────────────────────────────
```

Offsets are computed by `build_flash.py` and stored in the ZVIF header and
ZVGM TOC entries. Each TOC v2 entry carries `saves_offset` and `save_slot_count`
for its game's independent save ring. QSPI XIP base is `0x80000000`.

For 24 games the all-games image uses ~2.4MB for game data + 6MB for save
rings (96 slots × 64KB), well within the 15MB user region.

## Boot Sequence

1. CPU resets at PC = `0x80100100` (XIP direct — no boot stub)
2. QSPI XIP controller stalls the bus while the flash comes ready
3. Firmware runs in-place from QSPI flash
4. Reads ZVIF header at `0x80100000`, discovers games from ZVGM TOC
5. Launches game (or shows menu for multi-game builds)

## Quick Start

### Build Firmware + Flash Image

```bash
cd sw/

# Build flash image (firmware + ZVIF metadata + ZVGM TOC + game)
make flash-test                    # restaurant (default, ships with repo)
make flash-test GAME=hitchhiker    # Hitchhiker's Guide

# Preview flash layout without building image
make check-layout GAME=zork1
```

### Build FPGA Bitstream

```bash
cd fpga/

# Build bitstream (~5-10 minutes)
make build
```

### Program Hardware

```bash
cd fpga/

# One-command: build firmware + bitstream + ZVIF flash image + SRAM reload
make program-complete GAME=restaurant        # default (ships with repo)
make program-complete GAME=zork1
make program-complete GAME=hitchhiker

# Fast update: firmware/game only (bitstream unchanged)
# Step 1: rebuild flash image in sw/
cd ../sw && make flash-test GAME=zork1
# Step 2: program flash + reload SRAM in fpga/
cd ../fpga && make program-flash && make program-sram
```

### Monitor UART

```bash
picocom -b 115200 /dev/ttyUSB1  # or ttyUSB2 depending on other devices
```

Expected output (multi-game):
```
ZVibe Game System for RISC-V
Loading game library from flash...
Found 24 games

Select a game:
  1. Ballyhoo
  2. Cutthroats
  ...
```

Single-game builds auto-launch without a menu.
Type `~reset~` and press Enter for a clean restart.

## Directory Structure

```
arty_s7_50/
├── README.md           # This file
├── build_flash.py      # ZVIF flash image builder
├── rtl/                # Board-specific RTL wrapper
│   ├── servant_zvibe_arty_s7_50.v
│   └── servant_zvibe_arty_s7_50_clock_gen.v
├── fpga/               # FPGA build system
│   ├── Makefile        # Build automation
│   ├── build.tcl       # Vivado project script
│   ├── arty_s7_50.xdc  # Pin constraints and timing
│   └── build/          # Build artifacts (generated)
└── sw/                 # Board-local firmware build
    ├── Makefile        # Firmware + flash image build
    └── flash_save_config.h  # Arty flash layout constants
```

## Pin Assignments

| Function | Pin | Notes |
|----------|-----|-------|
| Clock (100MHz) | R2 | DDR3 clock input (NOT E3 like Arty A7!) |
| Reset (BTN0) | G15 | Active HIGH |
| UART RX | V12 | FTDI interface |
| UART TX | R12 | FTDI interface |
| LED[0] | E18 | Green LED |
| LED[1] | F13 | Green LED |
| LED[2] | E13 | Green LED |
| LED[3] | H15 | Green LED |
| QSPI CS_N | L12 | Chip select |
| QSPI DQ[0:3] | K17, K18, L14, M14 | Quad data |
| QSPI CLK | — | Via STARTUPE2 primitive |

## Makefile Targets

### `sw/Makefile`

```bash
make firmware                        # Build zvibe_riscv_multi_arty.bin
make flash-test                      # Build arty_flash_test.bin (default: restaurant)
make flash-test GAME=zork1           # Build with a specific game (always rebuilds)
make all-games                       # Build arty_flash_all.bin with all catalog games
make check-layout GAME=hitchhiker    # Preview flash layout without building
make clean
```

### `fpga/Makefile`

```bash
make build                           # Build bitstream
make program-complete                # Build firmware + program bitstream + flash + SRAM
make program-complete GAME=zork1     # Same, with a specific game
make program-flash                   # Program flash image only (after manual flash-test)
make program-sram                    # Reload ZVibe bitstream into FPGA SRAM
make reports                         # Show timing and utilization
make clean
```

## Timing

- **WNS**: +0.083 ns at 166.66 MHz (after SV modernization Feb 2026)
- **Clock**: 100 MHz input → 166.66 MHz via MMCM (× 10.0 / 6.0)
- **UART Prescaler**: 181 (`166,666,667 / (115200 × 8)`)
- **QSPI SCK**: 13.89 MHz (CLK_DIV=2; see QSPI Cache section for upgrade path)

## Resource Utilization

| Resource | Without cache | With cache (this branch) | Delta |
|----------|--------------|--------------------------|-------|
| LUTs (logic) | 719 (2.21%) | 1421 (4.36%) | +702 |
| LUTs (LUTRAM) | 24 (0.25%) | 24 (0.25%) | 0 |
| Slice Registers | 734 (1.13%) | 1456 (2.23%) | +722 |
| BRAM tiles | 8.5 (11.3%) | 10 (13.3%) | +1.5 |

Cache cost breakdown: +1× BRAM36 (32Kbit data array) + 1× RAMB18 (tag/valid
array, 256×21 bits) + ~722 FFs (128-bit burst buffer dominates) + ~702 logic
LUTs (cache FSM + s25fl_xip BURST_WORDS=4 expansion + 128→32-bit word mux).
All resources remain well within XC7S50 capacity (32,600 LUTs / 75 BRAM tiles).

## QSPI Cache

The Arty build uses a 4 KB direct-mapped BRAM cache between the SoC's
Wishbone instruction-fetch bus and the `s25fl_xip` QSPI controller.

### Architecture

| Parameter | Value |
|-----------|-------|
| Type | Direct-mapped, single-port BRAM |
| Capacity | 4 KB (256 lines × 16 bytes) |
| Line size | 16 bytes (4 × 32-bit words) |
| QSPI fetch | 128-bit burst (BURST_WORDS=4) per miss |
| Hit latency | 4 sys_clk cycles |
| Miss latency | 4 cycles + QSPI burst (~40–50 cycles total) |
| FPGA resource | 1× BRAM36 for data; distributed RAM for tags |

On a hit (S_IDLE → S_LOOKUP → S_BRAM_RD → S_HIT_ACK) the transaction completes
in 4 cycles. On a miss the cache issues a 128-bit aligned burst to `s25fl_xip`,
writes all 4 words into the BRAM sequentially (S_FILL, 4 cycles), then acknowledges
the original request (S_FILL_ACK).

The tag array is LUTRAM. At 166 MHz the 8-level RAMS64E→compare→FSM critical
path fails timing; a pipeline register in S_LOOKUP reduces it to 3–4 levels.
A ghost-request guard (`was_acked`) prevents SERV's one-cycle STB hold-after-ACK
from triggering a spurious second lookup.

### Performance (hardware, Plundered Hearts, 232 commands)

| Build | Median | Mean | p95 | Max | >2 s |
|-------|--------|------|-----|-----|------|
| No cache (CLK_DIV=1, Feb 2026) | 1.481 s | — | 3.578 s | 8.271 s | — |
| **Cache (CLK_DIV=2, Jun 20 2026)** | **0.421 s** | **0.524 s** | **1.039 s** | **2.287 s** | **0.4 %** |
| MAX10 08 (100 MHz, UFM, no cache, Jun 20 2026) | 0.512 s | 0.638 s | 1.240 s | 2.783 s | 0.9 % |

**How measured**: `tests/test_uart.py` replays `plunderedhearts-script.txt` (232
commands) on real hardware.  Each command's latency is wall-clock time from sending
the command to receiving the next `>` prompt.  Results written to
`plunderedhearts_latencies_<timestamp>.csv`; latest files:
- Arty:  `tests/plunderedhearts_latencies_20260620_120021.csv`
- MAX10: `tests/plunderedhearts_latencies_20260620_202129.csv`

Arty (166.66 MHz + 4 KB cache) outperforms MAX10 (100 MHz, on-chip UFM, no cache) —
cache hits (4 cycles vs ~130 cycles uncached) combined with the higher clock overcome
UFM's zero-latency on-chip access advantage.

### Increasing QSPI Clock Speed (CLK_DIV=1)

Current build: `CLK_DIV=2` → SCK = 166.66 / 12 = **13.89 MHz**.
The S25FL128S is rated to **104 MHz**; `CLK_DIV=1` → SCK = **20.83 MHz** is safe
and cuts cache miss fill time by ~33 %.

**Expected gain**: miss latency –33 %; overall median ~5–10 % (most accesses hit).
Benefit shows mainly in cold-start and max/p95 tail.

**Steps to enable CLK_DIV=1:**

1. `rtl/servant_zvibe_arty_s7_50.v` — change both `s25fl_xip` and `s25fl_write`:
   ```verilog
   .CLK_DIV(1),
   ```
2. `fpga/arty_s7_50.xdc` — data now changes every 4 sys_clk cycles (not 6):
   ```tcl
   set_multicycle_path -setup 4 -to   [get_ports qspi_cs_n]
   set_multicycle_path -hold  3 -to   [get_ports qspi_cs_n]
   set_multicycle_path -setup 4 -to   [get_ports {qspi_d[*]}]
   set_multicycle_path -hold  3 -to   [get_ports {qspi_d[*]}]
   set_multicycle_path -setup 4 -from [get_ports {qspi_d[*]}]
   set_multicycle_path -hold  3 -from [get_ports {qspi_d[*]}]
   ```
   Also update the descriptive comment block above those constraints.
3. `make build` → `make reports` — verify WNS ≥ 0 on QSPI paths.
4. `tests/test_uart.py` — confirm hardware pass.

## Save System

Game saves use QSPI flash writes with round-robin wear leveling.

- **Save format**: 12-byte header (magic/seq/size/crc16) + delta-compressed game state
- **Slot size**: 64KB (one S25FL128S erase sector per slot)
- **Slot count**: 4 slots per game (stored in ZVGM TOC entry; slot base address from
  `saves_offset` field, populated by `build_flash.py`)
- `flash_save_init()` is called at game launch using the TOC entry's `saves_offset`
  and `save_slot_count` fields
- Maximum save data per slot: 65,524 bytes (64KB − 12-byte header)

For a 24-game all-games build, save rings occupy 96 × 64KB = 6MB of the 15MB user
region, well within the flash capacity.

## Simulation

Arty uses **Vivado xsim** for RTL simulation (not Verilator — the Xilinx QSPI
IP primitives are not compatible with Verilator).  The shared `common/tb/`
testbenches use Spansion flash behavioral models and support Arty RTL validation:

```bash
# QSPI XIP controller unit test (Vivado xsim required)
cd common/tb/unit/flash
make -f Makefile.xip_wb xsim   # Tests 1-4 pass; test 5 (true back-to-back) hangs
                                 # (s25fl_xip.v does not support zero-gap consecutive reads;
                                 #  this is fine — SERV fetch has natural gaps)

# S25FL R/W model unit test (Verilator)
cd common/tb/unit/flash_rw
make                             # 6/6 tests pass: RDSR, WREN, SE, PP, write protection

# Arty SoC XIP integration test (Verilator — full boot with UART echo)
cd common/tb/soc/xip
make -f Makefile.xip arty-echo        # XIP direct boot (no stub), prints banner + "Ready>"
make -f Makefile.xip arty-echo-cache  # Full SoC with 4KB BRAM cache, prints banner + "Ready>"

# Arty SoC flash write integration test (Verilator — full SoC with write controller)
cd common/tb/soc/flash_write
make test-xip                    # Full SoC boots, QSPI mux, UART output verified
```

There is no `boards/arty_s7_50/sim/` directory — board-level simulation uses
the shared `common/tb/` infrastructure above.  MAX10 has additional vendor-model
simulations (Questa + Intel UFM IP) that have no Arty equivalent.

## Troubleshooting

**No UART output**: The FTDI FT2232H UART is the **second channel** — check `/dev/ttyUSB1`, `/dev/ttyUSB2`, etc. Use `ls /dev/ttyUSB*` to list available ports. Verify flash image was programmed at offset `0x100000`.

**Build error "file not found"**: Initialize the SERV submodule — see [First-Time Setup](../../README.md#first-time-setup--serv-submodule) in the platform README.

**openFPGALoader fails**: Ensure board is connected and powered; check
USB-JTAG permissions (`sudo` or udev rules).

