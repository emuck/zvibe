# Building ZVibe

Get from zero to a running interpreter in under five minutes.

---

## Quick path (no hardware required)

The **console target** builds a native desktop interpreter from standard C with no dependencies beyond a C compiler and Make. It works on macOS and Linux and is the fastest way to verify the codebase.

### macOS

```bash
# One-time: install Xcode command-line tools if not already present
xcode-select --install

# Build
cd target/console
make clean && make

# Smoke test — runs Z-machine opcode suite, no interaction needed
./build/bin/zvibe_minimal ../../games/catalog/czech.z3
```

### Ubuntu / Debian

```bash
# One-time prerequisites
sudo apt update && sudo apt install -y build-essential

# Build
cd target/console
make clean && make

# Smoke test
./build/bin/zvibe_minimal ../../games/catalog/czech.z3
```

### What gets built

```
target/console/build/
├── bin/
│   ├── zvibe_console    # Full-featured interpreter (status line, saves)
│   └── zvibe_minimal    # Lightweight interpreter (scripting / testing)
└── lib/
    └── libzvibe.so      # Shared library (Linux)
    or libzvibe.dylib    # Shared library (macOS)
```

Build time: ~30 seconds on any modern machine.

### Run interactively

```bash
# Play a game (supply your own legally-obtained .z3 file)
./build/bin/zvibe_console path/to/game.z3

# Or with the minimal interpreter
./build/bin/zvibe_minimal path/to/game.z3
```

The bundled `games/catalog/czech.z3` is a public-domain Z-machine opcode test suite — it runs automatically and needs no input beyond an optional `quit` to exit.

---

## Automated verification

```bash
# From the repo root — console build + unit tests + czech suite (matches CI)
make validate
```

Exit code 0 on success.

---

## Run the test suite

```bash
cd target/console/tests
make test-unit          # 119 unit tests (split memory architecture)
make test-czech         # 368 Z-machine opcode tests (bundled game)
make test               # All of the above + game walkthrough tests
```

Game walkthrough tests (`test-plundered`, `test-seastalker`) require downloading
game files separately; `test-restaurant` uses `restaurant.z3` which ships with
the repo. The unit and Czech tests run entirely from the repo.

---

## Embedded / FPGA targets

These require dedicated hardware and toolchains. All firmware can be built
without a connected board; only the programming step needs hardware.

### Prerequisites summary

| Target | Toolchain | Board |
|---|---|---|
| RISC-V MAX10 | `gcc-riscv64-unknown-elf` + Quartus Prime | Intel MAX10 08 Eval |
| RISC-V Arty S7-50 | `gcc-riscv64-unknown-elf` + Vivado 2024+ | Xilinx Arty S7-50 |
| SAM E51 | `gcc-arm-none-eabi` | Microchip SAM E51 Xplained Pro |

### RISC-V (MAX10 or Arty)

```bash
# Install RISC-V toolchain (Ubuntu)
sudo apt install gcc-riscv64-unknown-elf binutils-riscv64-unknown-elf

# Initialize SERV CPU submodule (one-time, from repo root)
git submodule update --init target/riscv/serv_zvibe/serv

# Build firmware only (no board needed)
cd boards/max10_08_eval/sw
make firmware            # → zvibe_riscv_multi_max10.elf / .bin

# Build bitstream + program board (Quartus must be in PATH)
cd ../fpga
make generate-ip         # One-time IP core generation
make build               # ~15 min
make program-complete    # Programs board (~11 s)
```

See [`target/riscv/serv_zvibe/README.md`](../target/riscv/serv_zvibe/README.md)
and [`docs/FPGA.md`](FPGA.md) for detailed instructions.

### SAM E51

```bash
# Install ARM toolchain (Ubuntu)
sudo apt install gcc-arm-none-eabi

cd target/same51
make
```

See `target/same51/README.md` for hardware setup and programming.

---

## Compiler compatibility

The C source is standard C99. The Makefile uses `gcc` by default. To use Clang:

```bash
make CC=clang
```

Both GCC and Apple Clang work on macOS; the `xcode-select --install` step
installs whichever is appropriate for the host.

---

## Clean build

```bash
# Console only
cd target/console && make clean

# Everything (all targets)
make clean-all           # from repo root
```

---

## Troubleshooting

**`make: gcc: No such file or directory`** — install build tools:
- macOS: `xcode-select --install`
- Ubuntu: `sudo apt install build-essential`

**`./build/bin/zvibe_minimal: No such file or directory`** — the build
failed silently; re-run `make` and check for compiler errors.

**`games/catalog/czech.z3: No such file or directory`** — run `python3 games/get_games.py list` from the repo root to confirm the file is present. It ships with the repo; if missing, `git status` should show it as deleted.

**FPGA / firmware issues** — see [`docs/TROUBLESHOOTING.md`](TROUBLESHOOTING.md).
