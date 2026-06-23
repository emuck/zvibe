# zVibe

[![CI](https://github.com/emuck/zvibe/actions/workflows/ci.yml/badge.svg)](https://github.com/emuck/zvibe/actions/workflows/ci.yml)

Z-machine version 3 interpreter for resource-constrained embedded systems.

**[Development history and milestones &rarr;](docs/HISTORY.md)** — ten months of vibe coding, from first interpreter to dual-board FPGA hardware, with a Rust rewrite and web target in progress.

## Description

zVibe is a Z-machine v3 interpreter written in C targeting microcontrollers
and FPGA soft-cores with as little as 32KB RAM. It uses a split memory model
that keeps only the dynamic portion of a story file in RAM while reading static
content directly from flash, reducing working-set memory by 85–95% compared to
a conventional full-image interpreter. The same core library builds for desktop
(Linux, macOS, Windows), ARM Cortex-M4, and a SERV RISC-V soft-core on Intel
MAX10 and Xilinx Arty S7-50 FPGAs.

The Z-machine core is a substantial embedded rewrite; attribution in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

## Goals

- **Z-machine v3 only.** Scope is intentionally narrow; later versions are out of scope.
- **Minimal RAM.** The interpreter must run on targets with 32KB of working memory.
- **No heap allocation.** All state is statically sized; no `malloc` at runtime.
- **Single C library.** Platform-specific code is isolated to thin HAL layers.
- **Embeddable.** The core exposes a clean C API with callback-based I/O.

## Architecture

```
src/core/       Z-machine interpreter (platform-agnostic C99)
src/api/        Public C API (zvibe_api.h)
target/console/ Desktop frontend — Linux, macOS, Windows
target/same51/  SAM E51 Cortex-M4 port
target/riscv/   SERV RISC-V soft-core (MAX10 and Arty S7-50)
```

Memory layout at runtime:

```
┌──────────────────────┐
│   Dynamic memory     │  → RAM   (2–14 KB for v3 stories; all known titles fit in 16 KB)
├──────────────────────┤
│   Static memory      │  → Flash / XIP (read in place)
└──────────────────────┘
```

The boundary is determined by the `staticmem` field in the story file header.
Everything below the boundary is copied to RAM at load time; everything above
is read on demand from the flash address space.

For RISC-V targets, the SoC is built around the
[SERV](https://github.com/olofk/serv) bit-serial RV32I CPU with a custom
Wishbone-attached UART, UFM/QSPI XIP controller, and save-slot manager.

See [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) for a full design walkthrough.

## Supported targets

| Target | Platform | RAM | Storage |
|---|---|---|---|
| console | Linux, macOS, Windows | host | host filesystem |
| same51 | SAM E51 Cortex-M4 | 256 KB | EEPROM |
| riscv/max10 | SERV on Intel MAX10 08 | 32 KB | 172 KB UFM |
| riscv/arty | SERV on Xilinx Arty S7-50 | 32 KB | 16 MB QSPI |

## Quick start

Prerequisites: GCC (or Clang) and Make.

```bash
# Ubuntu
sudo apt install build-essential

# macOS — Xcode command-line tools supply cc and make
xcode-select --install
```

Build and run the bundled test suite:

```bash
cd target/console
make clean && make
echo quit | ./build/bin/zvibe_minimal ../../games/catalog/czech.z3
```

To play a story file interactively:

```bash
./build/bin/zvibe_console path/to/game.z3
```

See [docs/BUILDING.md](docs/BUILDING.md) for platform-specific instructions
and embedded target build procedures.

## Testing

```bash
# Unit tests (memory subsystem, 119 assertions)
cd target/console/tests && make test-unit

# Z-machine opcode conformance (368 tests, bundled game)
make test-czech

# Console build + unit tests + czech suite (matches CI)
make validate
```

RTL simulation for the RISC-V target requires Verilator and runs separately:

```bash
make sim-tier1    # Unit + SoC tests (~30 s)
```

## Status

The interpreter is stable and handles the full Z-machine v3 opcode set.
All four targets build and run end-to-end. The RISC-V targets have been
validated on physical hardware.

**Active work:**
- Regression suite expansion

**Out of scope:**
- Z-machine v4 and later
- Networking or multiplayer

## License

BSD-3-Clause. See [LICENSE](LICENSE).

Third-party components (SERV CPU, Servile SoC, verilog-uart, Microchip DFP,
CMSIS, mojozork) are included under their respective permissive licenses.
See [NOTICE](NOTICE) for full texts.

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md).
Bugs and feature requests: [GitHub Issues](https://github.com/emuck/zvibe/issues).
