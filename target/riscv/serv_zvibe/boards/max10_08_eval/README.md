# MAX10 08 Evaluation Board

Intel MAX10 10M08SAE144C8GES running ZVibe (Z-machine interpreter) with XIP from on-chip UFM.

## Hardware

| Item | Spec |
|------|------|
| FPGA | Intel MAX10 10M08SAE144C8GES |
| CPU | SERV (bit-serial RV32I, ~200 LUTs) |
| Clock | External 50 MHz input → PLL → 100 MHz system clock |
| RAM | 32KB M9K blocks |
| Flash | 172KB on-chip UFM (User Flash Memory) |
| UART | 115200 baud, 8N1; requires external USB-to-UART adapter (CP2102/FT232R/CH340) |
| LEDs | 5 GPIO LEDs |
| JTAG | USB-Blaster |

## Memory Map

| Address | Size | Description |
|---------|------|-------------|
| 0x00000000 | 32KB | RAM |
| 0x40000000 | 16B | UART peripheral |
| 0x40000010 | 16B | Timer peripheral |
| 0x40000020 | 16B | GPIO LEDs |
| 0x80000000 | 172KB | UFM XIP region |

## Flash Layout (XIP — No Bootloader)

```
Physical    XIP Virtual   Component
────────────────────────────────────────────────────────────
0x000000    0x80000000    ZVIF metadata header (256 B)
0x000100    0x80000100    Firmware entry point (~34.5 KB)
varies      varies        ZVGM TOC v2 (16 + 80×N bytes)
varies      varies        Game data (up to ~126 KB)
varies      varies        Save slots: N×2 KB per game
────────────────────────────────────────────────────────────
Total: 172 KB UFM
```

Offsets are computed at build time by `build_flash.py` and stored in the ZVIF header and
ZVGM TOC entries. Each TOC v2 entry carries `saves_offset` and `save_slot_count` for
its game's independent save ring.

## Boot Sequence

1. CPU resets at PC = `0x80000100` (UFM XIP direct — no bootloader copy)
2. Firmware runs in-place from UFM; all instruction fetches go directly to the UFM controller
3. Reads ZVIF header at `0x80000000` to discover layout (TOC offset, firmware size)
4. Reads ZVGM TOC, populates game registry
5. Auto-launches if one game present; shows menu for multi-game builds

## Quick Start

### Prerequisites

- Quartus Prime (Lite or Standard) in PATH
- RISC-V toolchain (`riscv64-unknown-elf-gcc`)
- USB-Blaster for FPGA programming
- USB-to-UART adapter (CP2102, FT232R, or CH340-based module)

**USB-to-UART adapter**: The MAX10 08 eval board exposes UART TX/RX on 2.54 mm header
pins but has no onboard USB-UART bridge. A separate adapter module is required. The
IZOKEE CP2102 module was used during development and works well; any CP2102-, FT232R-,
or CH340-based breakout is compatible. See the SparkFun CP2102 hookup guide for pinout
and wiring reference:
[SparkFun CP2102 USB-to-Serial Hookup Guide](https://learn.sparkfun.com/tutorials/cp2102-usb-to-serial-converter-hook-up-guide/all)

Wire the adapter to the board's 3-pin UART header using 3.3V-compatible logic (the MAX10
GPIO is 3.3V; most CP2102/FT232R breakouts default to 3.3V signal level):

| Adapter pin | Board signal | FPGA pin | Arduino header | Note |
|-------------|--------------|----------|----------------|------|
| TXO (TX out) | UART RXD    | PIN_70   | J3.3 (IO13)    | Adapter transmits → FPGA receives |
| RXI (RX in)  | UART TXD    | PIN_69   | J3.4 (IO12)    | FPGA transmits → adapter receives |
| GND          | GND          | —        | J3.2           | Common ground (required) |

On Linux the adapter appears as `/dev/ttyUSB0`; run `ls /dev/ttyUSB*` to confirm the
port if multiple USB-serial devices are connected.

### Step 1 — One-time setup

Initialize the SERV submodule first (one-time, from repo root) — see
[`target/riscv/serv_zvibe/README.md`](../../README.md#first-time-setup--serv-submodule).

```bash
cd target/riscv/serv_zvibe/boards/max10_08_eval/fpga
make generate-ip                           # Generate UFM IP core (Quartus required)
make build                                 # Build FPGA bitstream (~15 min)
```

See [`fpga/UFM_IP_GENERATION.md`](fpga/UFM_IP_GENERATION.md) for IP generation details.
The bitstream only needs to be rebuilt if RTL changes.

### Step 2 — Choose a game

`restaurant.z3` ships with the repo — no download needed for the default path.
To use a different game, download it first:

```bash
cd "$(git rev-parse --show-toplevel)"   # back to repo root

# Download a game by name
python3 games/get_games.py download zork1       # Zork I (~83KB)
python3 games/get_games.py download hitchhiker  # Hitchhiker's Guide (~112KB)
python3 games/get_games.py download plundered   # Plundered Hearts (~129KB)

# Or browse interactively
python3 games/get_games.py browse
```

Games must be Z-machine v3 files (`.z3`) and fit within ~126KB of UFM.

### Step 3 — Build firmware + flash image and program the board

```bash
cd target/riscv/serv_zvibe/boards/max10_08_eval/fpga

make program-complete                   # restaurant (default, ships with repo)
make program-complete GAME=czech        # Czech test suite
make program-complete GAME=zork1        # Zork I (requires download)
make program-complete GAME=hitchhiker   # Hitchhiker's Guide (requires download)
```

`program-complete` rebuilds the firmware and flash image automatically, then programs
CFM + UFM + SRAM (~11 sec). The `GAME=` ID must match a key in `games/registry.json`;
use `get_games.py list` to see available IDs.

To preview the flash layout without building:
```bash
cd ../sw && make check-layout GAME=restaurant
```

### Step 4 — Play

```bash
picocom -b 115200 /dev/ttyUSB0
```

Expected output: ZVibe boot banner → `Press RETURN to start...` → press Enter →
game launches. When the game ends it loops back to `Press RETURN to start...`.
Type `~reset~` + Enter at any game prompt to restart without waiting for the game
to finish (useful for scripted testing).

### Step 5 — Automated test

Verify the board end-to-end without picocom: replays the full restaurant game script
and checks for the winning completion marker.

```bash
cd target/riscv/serv_zvibe/boards/max10_08_eval/tests
python3 test_uart.py
```

Expected: `PASSED: restaurant completed successfully!`

To test with Plundered Hearts (requires download):
```bash
python3 games/get_games.py download plundered   # from repo root
cd target/riscv/serv_zvibe/boards/max10_08_eval/fpga
make program-complete GAME=plunderedhearts
cd ../tests
python3 test_uart.py --game plunderedhearts
```

## Directory Structure

```
boards/max10_08_eval/
├── rtl/                    # Board-level RTL wrapper
├── fpga/                   # Quartus build system
│   ├── Makefile            # Build + programming targets
│   ├── build.tcl           # Quartus synthesis script
│   ├── ip/ufm/             # Generated UFM IP core
│   └── UFM_IP_GENERATION.md # IP core generation guide
├── build_flash.py          # Flash image builder (ZVIF/ZVGM)
├── tests/                  # Hardware test scripts
└── TESTING.md              # Test results and validation status
```

## Pin Assignments

| FPGA pin | Signal | Arduino header | Description |
|----------|--------|----------------|-------------|
| PIN_70   | uart_rxd    | J3.3 (IO13) | FPGA input from CP2102 TXO |
| PIN_69   | uart_txd    | J3.4 (IO12) | FPGA output to CP2102 RXI |
| PIN_132  | gpio_led[0] | — (D1)      | User LED 1 |
| PIN_134  | gpio_led[1] | — (D2)      | User LED 2 |
| PIN_135  | gpio_led[2] | — (D3)      | User LED 3 |
| PIN_140  | gpio_led[3] | — (D4)      | User LED 4 |
| PIN_141  | gpio_led[4] | — (D5)      | User LED 5 |
| PIN_27   | clk_50mhz   | —           | 50 MHz oscillator (X1) |
| PIN_124  | dip_sw1     | — (SW3.2)   | DIP switch position 2 |

## Makefile Targets

### `sw/Makefile`

```bash
make firmware                        # Build zvibe_riscv_multi_max10.elf/.bin
make flash-test                      # Build max10_flash_test.bin (default: czech)
make flash-test GAME=hitchhiker      # Build with a specific game
make check-layout GAME=hitchhiker    # Preview flash layout without building
make sim-dat                         # Generate altera_onchip_flash.dat for Questa
make clean
```

### `fpga/Makefile`

```bash
make build                           # Build FPGA bitstream
make generate-ip                     # Generate UFM IP core (one-time)
make program-complete                # Build firmware + program CFM + UFM + SRAM
make program                         # Program SRAM only (fast iteration)
make program-flash                   # Program CFM only
make reports                         # View timing and utilization reports
make clean
```

## Timing

- **WNS (setup)**: +0.812 ns at 100 MHz (Slow 85C corner)
- **WNS (hold)**: +0.340 ns at 100 MHz
- **FMAX**: 119.39 MHz (constrained to 100 MHz)
- **Clock**: 50 MHz input → 100 MHz via `altpll` (×2 / 1, instantiated directly in RTL)
- **UART Prescaler**: 108 (`100,000,000 / (115200 × 8)`)

## Resource Utilization

From Quartus Fitter (Feb 19, 2026 build, 10M08SAE144C8GES):

| Resource | Used | Available | Utilization |
|----------|------|-----------|-------------|
| Logic elements | 1,930 | 8,064 | 24% |
| — Combinational functions | 1,793 | 8,064 | 22% |
| — Dedicated registers | 1,073 | 8,064 | 13% |
| Memory bits (M9K) | 264,320 | 387,072 | 68% |
| PLLs | 1 | 1 | 100% |
| UFM blocks | 1 | 1 | 100% |

The 68% memory utilization reflects 32KB BRAM for the Z-machine heap/stack plus
internal UFM IP control structures. UFM itself (172KB) is accessed via the XIP and
write controllers, not through M9K.

## Save System

Game saves use UFM runtime writes with round-robin wear leveling.

- **Save format**: 16-byte header (magic/seq/size/crc16) + delta-compressed game state
- **Slot size**: 2KB (one UFM page per slot)
- **Slot count**: computed by `build_flash.py` from remaining UFM after firmware + game
  and stored in the ZVGM TOC entry (e.g. Zork I at 83KB → ~5 slots; Plundered Hearts
  at 129KB → ~3 slots)
- `flash_save_init()` is called at game launch using the TOC entry's `saves_offset`
  and `save_slot_count` fields

## Performance

Hardware test: `tests/test_uart.py` replays `plunderedhearts-script.txt` (232 commands).
Each command's latency is wall-clock time from sending the command to receiving the next
`>` prompt. Results written to `plunderedhearts_latencies_<timestamp>.csv` (gitignored).

| Metric | Value |
|--------|-------|
| n | 232 |
| min | 0.260 s |
| median | 0.512 s |
| mean | 0.638 s |
| p95 | 1.240 s |
| max | 2.783 s |
| stdev | 0.324 s |
| >2 s turns | 0.9% |
| >5 s turns | 0.0% |

**Measured**: Jun 20, 2026 — `tests/plunderedhearts_latencies_20260620_202129.csv`

See `boards/arty_s7_50/README.md` for a cross-board comparison (Arty S7-50 at 166.66 MHz
with 4KB BRAM XIP cache achieves 0.421 s median — the clock advantage plus cache hits
outperform the MAX10's on-chip UFM zero-latency access).

## Simulation

MAX10 uses **Questa FSE / ModelSim** (requires `vsim` in PATH) for vendor-model
simulations using Intel's UFM behavioral IP. Verilator handles the shared `common/tb/`
unit tests.

### Prerequisites

- Questa FSE or ModelSim (`vsim` in PATH)
- UFM IP simulation files generated: `cd fpga && make generate-ip`

### Questa tests

```bash
cd boards/max10_08_eval/sim

# UART echo XIP test (~14 min, validates full XIP boot path)
make -f Makefile.vsim uart-echo
# Output: uart_output.txt — expect boot banner + "Ready>"

# UFM write/erase/verify test (~15 min)
make -f Makefile.vsim ufm-write
# Output: uart_output.txt — expect 5 sub-tests + "completed!"
```

### Generating simulation .dat files

```bash
# From ELF (firmware only):
riscv64-unknown-elf-objcopy -O verilog --verilog-data-width=4 \
  --change-addresses=-0x80000000 firmware.elf altera_onchip_flash.dat

# From full flash image (firmware + game):
cd sw && make sim-dat    # → sim/altera_onchip_flash.dat
```

See [`sim/QUESTA_UFM_INIT_GUIDE.md`](sim/QUESTA_UFM_INIT_GUIDE.md) for detailed setup
and `--change-addresses` / `--reverse-bytes` requirements.

## Troubleshooting

**No UART output**
- Verify adapter wiring: TXO→J3.3/PIN_70 (RXD), RXI→J3.4/PIN_69 (TXD), GND→J3.2
- Confirm device: `ls /dev/ttyUSB*` — adapter typically appears as `/dev/ttyUSB0`
- Verify CFM was programmed: `make program-complete` step 4 must report
  "Configuration succeeded". If silent, check that `quartus_cpf` and
  `update_ufm_pof.py` ran without errors.
- Verify UFM flash image was written: step 4 programs CFM+UFM together via `.pof`

**Boot fails / all LEDs on**
- All 5 LEDs illuminated = firmware error state
- Check UFM programming succeeded (step 4 output)
- Verify ZVIF metadata: `flash_metadata_init()` returns false if magic bytes are
  wrong — regenerate the flash image with `make flash-test`

**Game won't load**
- Check game file is Z-machine v3 (`.z3`), not v5 or v8
- Verify game fits: `make check-layout GAME=<id>` — must be ≤ ~126KB
- Verify ZVGM TOC: magic `0x4D47565A`, version 1, correct entry count

**`make program-complete` hangs or shows no output**
- Confirm USB-Blaster is detected: `quartus_pgm --list`
- Try unplugging/replugging USB-Blaster and re-running
- If step 4 is silent, `quartus_cpf` likely failed — check `.cof` path generation
