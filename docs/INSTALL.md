# Installation

Dependencies and toolchains for each platform.

## Console Target (Linux/macOS)

### Required

- GCC or Clang
- Make
- Python 3 (for tests and game tools)

### Optional

- `requests` Python package (for game download tools)
- Doxygen (for API documentation)
- GraphViz, including the `dot` command (for call graphs in API docs)
- TeX distribution with `pdflatex` (only for building the optional Doxygen PDF)

```bash
# Ubuntu/Debian
sudo apt install build-essential python3 python3-pip doxygen graphviz
pip3 install requests

# macOS
xcode-select --install
brew install doxygen graphviz
pip3 install requests
```

For Doxygen PDF output, install a TeX distribution separately. On macOS, either
MacTeX or BasicTeX works if `pdflatex` is available on `PATH`.

### Build

```bash
cd target/console
make all
```

## Windows Target (Cross-compilation from WSL2)

### Required

- WSL2 with Ubuntu
- MinGW-w64 cross-compiler

```bash
sudo apt install mingw-w64 gcc-mingw-w64
```

### Build

```bash
cd target/windows
make deploy    # Build and install to /mnt/c/tmp/zvibe_win/
```

## SAM E51 Target (ARM)

### Required

- ARM GCC toolchain (`arm-none-eabi-gcc`)
- OpenOCD or Atmel/Microchip programming tools

```bash
# Ubuntu/Debian
sudo apt install gcc-arm-none-eabi
```

### Build

```bash
cd target/same51
make program
```

See `target/same51/README.md` for hardware setup.

## RISC-V FPGA Target

Two boards are supported. Install the toolchain for the board you have.

### Required (both boards)

- RISC-V GCC toolchain (`riscv64-unknown-elf-gcc` or `riscv32-unknown-elf-gcc`)
- Python 3

```bash
# Ubuntu - RISC-V toolchain
sudo apt install gcc-riscv64-unknown-elf binutils-riscv64-unknown-elf
# Or download from https://github.com/riscv-collab/riscv-gnu-toolchain
```

### Intel MAX10 08 Eval Board

- Quartus Prime (Lite or Standard) — `quartus_sh`, `quartus_pgm`, `quartus_cpf` must be in PATH
- USB-Blaster driver (udev rule required on Linux)

```bash
# udev rule for USB-Blaster
echo 'SUBSYSTEM=="usb", ATTR{idVendor}=="09fb", MODE="0666"' | \
  sudo tee /etc/udev/rules.d/51-usbblaster.rules
sudo udevadm control --reload-rules
```

### Arty S7-50

- Xilinx Vivado 2024.x or 2025.1 — `vivado` must be in PATH
- openFPGALoader

```bash
sudo apt install openfpgaloader
# Or build from source: https://github.com/trabucayre/openFPGALoader
```

### SERV Submodule

See [`target/riscv/serv_zvibe/README.md`](../target/riscv/serv_zvibe/README.md#first-time-setup--serv-submodule) for the correct one-time setup command.

### Build

```bash
# MAX10 08 Eval Board
cd target/riscv/serv_zvibe/boards/max10_08_eval/fpga
make generate-ip    # One-time
make build
make program-complete

# Arty S7-50
cd target/riscv/serv_zvibe/boards/arty_s7_50/fpga
make build
make program-complete
```

See `target/riscv/serv_zvibe/README.md` for detailed instructions.

## Python Tools

Game management tools require:

```bash
pip3 install requests
```

Optional for interactive browsing:

```bash
pip3 install simple-term-menu
```

## Verifying Installation

### Console

```bash
cd target/console
make all
./build/bin/zvibe_minimal ../../games/catalog/czech.z3
```

Expected: Czech test suite runs and reports pass/fail.

### Tests

```bash
cd target/console/tests
make test-unit test-integration
```

Expected: 119 unit tests pass, integration tests complete.
