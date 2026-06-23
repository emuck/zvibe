# Build Guide

Complete build and programming reference for both supported boards.

## Required Tools

### All Platforms
| Tool | Purpose | Notes |
|------|---------|-------|
| `riscv64-unknown-elf-gcc` | Firmware compiler | `riscv32-unknown-elf-gcc` also accepted |
| `riscv64-unknown-elf-objcopy` | Binary conversion | Part of the same toolchain package |
| Python 3.8+ | Flash image builders | `build_flash.py`, `zvibe_flash_lib.py` |
| picocom or screen | UART terminal | `picocom -b 115200 /dev/ttyUSBx` |
| Git | SERV submodule | One-time init |

### MAX10 08 Eval Board
| Tool | Purpose | Notes |
|------|---------|-------|
| Quartus Prime (Lite or Standard) | FPGA synthesis + programming | `quartus_sh`, `quartus_pgm`, `quartus_cpf` must be in PATH |
| USB-Blaster driver | JTAG programming | Typically needs udev rule on Linux |

### Arty S7-50
| Tool | Purpose | Notes |
|------|---------|-------|
| Xilinx Vivado 2024.x or 2025.1 | FPGA synthesis | `vivado` must be in PATH |
| openFPGALoader | Flash programming | `sudo apt install openfpgaloader` or build from source |

### Simulation Only
| Tool | Purpose | Board |
|------|---------|-------|
| Questa FSE / ModelSim (`vsim`) | Vendor UFM model simulation | MAX10 |
| Verilator 5.x | RTL unit + integration tests | Arty |
| Vivado xsim | Xilinx IP simulation | Arty |

---

## Repository Setup (One-Time)

Initialize the SERV submodule — see [First-Time Setup](../README.md#first-time-setup--serv-submodule) in the platform README for the correct command.

---

## MAX10 08 Eval Board

### 1. Generate UFM IP Core (One-Time)

```bash
cd boards/max10_08_eval/fpga
make generate-ip
```

### 2. Build FPGA Bitstream (~15 min)

```bash
make build
# Output: output_files/servant_zvibe_max10_08_eval_xip.sof/.pof
```

### 3. Build Firmware + Flash Image + Program (~11 seconds)

```bash
cd ../fpga
make program-complete GAME=czech        # default (always present)
make program-complete GAME=hitchhiker   # or any downloaded game
```

Rebuilds firmware and flash image, then programs CFM (bitstream) + UFM
(firmware + game) + SRAM (for clean reset) in one command via `quartus_pgm`.

To preview the flash layout without programming:
```bash
cd ../sw && make check-layout GAME=hitchhiker
```

### UART Connection

```bash
picocom -b 115200 /dev/ttyUSB0    # CP2102 USB-UART module
```

Expected output: ZVibe banner → `Press RETURN to start...` → press Enter →
game launches. When the game ends it loops back to `Press RETURN to start...`.
Type `~reset~` + Enter at any game prompt for an immediate clean restart.

---

## Arty S7-50

### 1. Build FPGA Bitstream (~5-10 min)

```bash
cd boards/arty_s7_50/fpga
make build
# Output: build/servant_zvibe_arty_s7_50.bit
```

### 2. Build Firmware + Flash Image

```bash
cd ../sw
make flash-test                    # Single game (Plundered Hearts)
make flash-test GAME=hitchhiker    # Different game
make all-games                     # All 24 catalog games
make check-layout                  # Preview layout
```

### 3. Program

```bash
cd ../fpga

# Bitstream + single game (full, ~3 min)
make program-complete

# All 24 games (bitstream must already be programmed)
make program-flash ARTY_FLASH=../sw/arty_flash_all.bin

# Flash image only — fast update when bitstream unchanged (~30s)
make program-flash
```

### UART Connection

```bash
picocom -b 115200 /dev/ttyUSB1    # FT2232H second channel
# or /dev/ttyUSB2 depending on other connected devices
```

Check: `ls /dev/ttyUSB*` — JTAG is usually the lowest-numbered port, UART is next.

---

## Flash Image Format (ZVIF)

Both boards use the same ZVIF-based flash layout:

```
Physical    Content
──────────────────────────────────────────────────────
0           ZVIF metadata header (256 B)
+0x100      Firmware entry point
varies      ZVGM TOC v2 (16B header + 80B per game entry)
varies      Game data (concatenated)
varies      Per-game save rings (offset in each TOC entry)
──────────────────────────────────────────────────────
```

Offsets are computed at build time by `boards/*/build_flash.py`
(imports `common/sw/zvibe_flash_lib.py`). Flash layout written to `flash_layout.h`.

### Hex File Formats

**RAM simulation (32-bit words):**
```bash
riscv64-unknown-elf-objcopy -O verilog --verilog-data-width=4 firmware.elf fw.hex
```

**Flash simulation (ELF → .dat, address-adjusted):**
```bash
riscv64-unknown-elf-objcopy -O verilog --verilog-data-width=4 \
  --change-addresses=-0x80000000 firmware.elf altera_onchip_flash.dat
```

**Flash simulation (binary → .dat, byte-swapped):**
```bash
objcopy -I binary -O verilog --verilog-data-width=4 --reverse-bytes=4 \
  max10_flash_test.bin altera_onchip_flash.dat
```

---

## Simulation

### MAX10 (Questa — vendor UFM model)

```bash
# Generate IP first (fpga/ must exist)
cd boards/max10_08_eval/fpga && make generate-ip

# UART XIP echo test
cd ../sim && make -f Makefile.vsim questa-batch

# UFM write/erase/verify test
vsim -c -do run_questa_ufm_write.tcl
```

See `boards/max10_08_eval/sim/QUESTA_UFM_INIT_GUIDE.md` for UFM init file setup.

### Arty (Verilator + xsim)

```bash
# S25FL R/W unit tests (Verilator)
cd common/tb/unit/flash_rw && make

# SoC XIP boot test (Verilator)
cd common/tb/soc/xip && make -f Makefile.xip arty-echo

# QSPI flash write SoC test (Verilator)
cd common/tb/soc/flash_write && make test-xip
```

---

## Troubleshooting

**Quartus not found:**
```bash
which quartus_sh   # should return a path
# If not: export PATH=/path/to/quartus/bin:$PATH
```

**USB-Blaster not detected (MAX10):**
```bash
# Check device appears
lsusb | grep Altera
# May need udev rule: /etc/udev/rules.d/51-usbblaster.rules
# SUBSYSTEM=="usb", ATTR{idVendor}=="09fb", MODE="0666"
```

**openFPGALoader permission denied (Arty):**
```bash
sudo openFPGALoader ...
# Or add udev rule for Digilent/FTDI devices
```

**RISC-V toolchain not found:**
```bash
riscv64-unknown-elf-gcc --version
# Ubuntu: sudo apt install gcc-riscv64-unknown-elf
# Or: download from SiFive toolchain releases
```

**No UART output after programming:**
- MAX10: verify CP2102 module is connected to the right pins (PIN_69/70)
- Arty: use second ttyUSB port (FT2232H UART = higher-numbered port)
- Both: confirm baud rate 115200, 8N1
