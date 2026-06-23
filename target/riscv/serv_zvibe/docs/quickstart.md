# Quick Start

Get ZVibe running on hardware in ~15 minutes.

## Prerequisites

- RISC-V GCC toolchain (`riscv64-unknown-elf-gcc`)
- Python 3.8+
- UART terminal (`picocom`)
- **MAX10**: Quartus Prime (Lite or Standard) in PATH + USB-Blaster + USB-to-UART adapter (CP2102/FT232R/CH340)
- **Arty S7-50**: Vivado 2024.x or 2025.1 in PATH + openFPGALoader

One-time submodule init — see [First-Time Setup](../README.md#first-time-setup--serv-submodule) for the correct command.

---

## MAX10 08 Eval Board (Primary)

All commands run from the **repo root** (`zvibe/`).

```bash
# One-time: submodule + IP core + bitstream (~15 min)
git submodule update --init target/riscv/serv_zvibe/serv
make -C target/riscv/serv_zvibe/boards/max10_08_eval/fpga generate-ip
make -C target/riscv/serv_zvibe/boards/max10_08_eval/fpga build

# Download a game
python3 games/get_games.py download zork1    # Zork I (~83KB)
python3 games/get_games.py download plundered  # Plundered Hearts (~54KB)
python3 games/get_games.py browse            # interactive picker

# Build firmware + flash image with your chosen game
make -C target/riscv/serv_zvibe/boards/max10_08_eval/sw flash-test GAME=zork1

# Program the board (~11 sec)
make -C target/riscv/serv_zvibe/boards/max10_08_eval/fpga program-complete

# Play
picocom -b 115200 /dev/ttyUSB0
```

Expected: ZVibe banner → game auto-launches. Type `~reset~` to restart.

To switch games: re-run the `flash-test` and `program-complete` lines with a different `GAME=` ID.
Available IDs: `python3 games/get_games.py list`

---

## Arty S7-50

```bash
cd boards/arty_s7_50

# Build FPGA bitstream (~5-10 min, one-time or when RTL changes)
(cd fpga && make build)

# Build firmware + flash image + program everything
(cd fpga && make program-complete GAME=czech)    # default; or GAME=zork1 etc.

# Connect UART (FT2232H second channel)
picocom -b 115200 /dev/ttyUSB1   # or ttyUSB2 — check ls /dev/ttyUSB*
```

### All 24 Catalog Games (Arty)

```bash
cd boards/arty_s7_50

(cd sw && python3 ../../games/get_games.py fetch --target riscv)
(cd sw && make all-games)
(cd fpga && make program-flash ARTY_FLASH=../sw/arty_flash_all.bin)
```

---

## Flash Layout

Both boards use ZVIF format — offsets computed at build time:

```
0x000000    ZVIF metadata (256 B)
0x000100    Firmware entry point
varies      ZVGM TOC v2 (80 B/game)
varies      Game data
varies      Per-game save rings
```

MAX10: 172KB UFM, firmware + 1 game + save slots
Arty: 15MB user region, firmware + up to 24 games + 4×64KB save slots/game

---

## Troubleshooting

| Symptom | Fix |
|---------|-----|
| `quartus_sh: command not found` | Add Quartus `bin/` to PATH |
| USB-Blaster not detected | Check udev rules; `lsusb \| grep Altera` |
| `openFPGALoader` fails | `sudo` or add udev rule for FTDI/Digilent |
| No UART output | Check port with `ls /dev/ttyUSB*`; verify baud 115200 |
| Game won't load | Run `make check-layout` to verify flash image built correctly |

See [build.md](build.md) for full tool installation and simulation details.
