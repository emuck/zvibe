# Changelog

All notable changes to this project are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [2.0.0] - 2026-06-22

### Added

**Web terminal target (`target/webterm`)**
- Platform-agnostic Z-machine web terminal for any WiFi-capable embedded device
- `webterm_session`: session state machine (IDLE → RUNNING → PAUSED), Z-machine I/O
  callbacks, output buffering, history ring buffer, input queue — 17/17 tests passing
- `webterm_platform.h`: three-callback contract a hardware target must implement
  (send frame, save state, restore state)
- `PROTOCOL.md`: WebSocket JSON message specification
- POSIX reference server (`target/webterm/platform/posix/`) for full browser testing
  on a laptop before touching hardware
- Host-runnable test suite with mock platform (`target/webterm/tests/`)

**Intel MAX10 08 Evaluation Board (primary RISC-V target)**
- XIP-only boot verified on hardware: reset PC = 0x80000100, no bootloader copy step
- UART + UFM XIP confirmed functional end-to-end (Plundered Hearts, Zork I)
- Runtime UFM erase/write/verify proven on hardware (Feb 2026)
- SAVE/RESTORE commands functional with wear-leveling slot rotation
- Automated hardware test: `boards/max10_08_eval/tests/test_uart.py` replays 232-command script
- One-command programming: `make program-complete` (~11 s, fully automated)
- Dynamic flash layout: `build_flash.py` + shared `zvibe_flash_lib.py`; 126KB game capacity
- ZVGM TOC v2: 16-byte header + 80-byte per-game entry with `saves_offset`/`save_slot_count`
- `~reset~` control command for deterministic test-script restarts

**Arty S7-50 — QSPI cache**
- 4KB BRAM direct-mapped cache for QSPI XIP reads (Feb 2026)
- 2.7× median speedup over uncached XIP (0.546 s vs 1.481 s, Plundered Hearts 232-command run)
- Cache module: `common/rtl/qspi/qspi_cache_bram.sv`

**RTL modernization**
- All active RTL and testbench files converted from Verilog to idiomatic SystemVerilog
- `wire`/`reg` → `logic`; `always @` → `always_ff`/`always_comb`; FSMs use `typedef enum`
- Stale superseded files deleted: `max10_ufm_xip.v`, `wb_to_avalon_mm.v`, `ufm_wb_bridge*.v`, `qspi_cache.v`
- UFM controller consolidated: `max10_ufm_unified.sv` replaces three separate files

**Simulation and verification infrastructure**
- Tier-based regression runner: `run_regression.sh` / `make sim-tier1` / `make sim-questa`
- All testbenches emit canonical `PASS: <name>` / `FAIL: <name>` lines for CI
- Cycle-based watchdog in every testbench replaces time-based `#N` timeouts
- Per-test log capture to `regression_logs/`; 5/5 Tier 1 Verilator tests pass

**SAM E51 Curiosity Nano**
- Delta compression for save/restore — saves only changed bytes (typically 500–2000 bytes vs 14KB+ full saves), ported from RISC-V target
- Per-game save slots in SmartEEPROM (8 × 2KB), each game's save is independent
- Games beyond slot limit shown as `[no save]` in menu with clear error messages
- Fix XC32 v4.60 linker crash on Linux when TMPDIR is unset

## [1.0.0] - 2025-01-23

### Added

**Multi-Target Support**
- Console target for Linux, macOS, and Windows desktop systems
- Windows cross-compilation support via MinGW from WSL2
- SAM E51 Curiosity Nano (ARM Cortex-M4) embedded target
- RISC-V FPGA target (SERV CPU on Digilent Arty S7-50)

**Core Interpreter**
- Z-machine Version 3 interpreter with split memory architecture
- 85-95% RAM reduction compared to traditional implementations
- Read-only static memory accessed in place from flash/ROM
- Support for 128KB story files on microcontrollers with 2-14KB dynamic RAM

**Persistent Storage**
- Save/restore functionality with delta compression
- Flash-based persistent saves on RISC-V target
- Wear leveling system for flash endurance (16 sectors, 240 save slots)
- CRC32 integrity checking for saved games
- Multiple save slots per game (3 user-visible slots, 8 games supported)

**Multi-Game Support**
- Unified game registry system with metadata
- Multi-game firmware for embedded targets
- Game selection menu for RISC-V and SAM E51 platforms
- Automatic game catalog management via `get_games.py`
- Support for up to 8 games on RISC-V FPGA (flash capacity limited)

**RISC-V FPGA Implementation**
- SERV bit-serial RISC-V CPU (RV32I, ~200 LUTs)
- 166.66 MHz system clock via MMCM (timing margin: +2.5ns WNS)
- 32KB BRAM for data and stack
- QSPI flash XIP (Execute-In-Place) controller with continuous read mode
- Custom QSPI write controller for persistent saves
- UART peripheral (115200 baud, 64-byte FIFOs)
- Multi-board architecture with Arty S7-50 support
- Single-game and multi-game build modes

**Build System**
- Unified build system: `games/build_games.py` for all targets
- Target-specific builders: `games/game_builder/same51.py`
- Multi-target validation: `make validate`
- Automated dependency tracking in FPGA builds
- Cross-platform support (Linux, macOS, Windows/WSL2)

**Testing**
- Czech Z-machine test suite integration (368 tests, public domain)
- 119 unit tests for memory subsystem
- Hardware validation suite for RISC-V FPGA
- Automated UART testing with latency metrics
- Simulation testbenches (Verilator and Vivado xsim)
- Multi-hour simulation support with timeout management

**Documentation**
- 16 comprehensive documentation files (2,380+ lines)
- Doxygen API documentation with groups and examples
- Target-specific build and deployment guides
- Architecture documentation with memory layout diagrams
- Troubleshooting guide for common issues
- Story file sourcing and licensing guide

### Technical Details

**Memory Architecture**
- Split memory model: dynamic (RAM) and static (ROM/flash)
- Unified API: `zmem_read_byte()`, `zmem_write_byte()`, `zmem_get_ptr()`
- Conditional compilation for minimal builds (`ZVIBE_MINIMAL_FEATURES`)
- Thread-safety: single-threaded design (no locks required)

**RISC-V System-on-Chip**
- Memory map: RAM (0x00000000), peripherals (0x40000000), flash XIP (0x80100000)
- Flash layout: bitstream (0x000000), firmware (0x100000), games (0x110000+)
- Boot flow: 8-byte stub in RAM jumps to flash XIP region
- Power-on reset: automatic 256-cycle reset pulse
- Resource usage: 539 LUTs (1.65%), 566 registers (0.87%), 8.5 BRAM tiles (11.33%)

**QSPI Flash Controller**
- Continuous read mode: 2.4x performance improvement over command mode
- Commands: QIOR (0xEB), WREN (0x06), SE (0x20), PP (0x02), RDSR (0x05)
- Dual controller architecture: separate XIP (read) and write controllers
- Pin multiplexer for read/write arbitration
- Quad I/O mode for fast reads, single I/O for writes

**Performance**
- SERV CPU: ~5 MIPS effective at 166.66 MHz (32 cycles per instruction typical)
- QSPI read: ~68 cycles per read in continuous mode (vs ~164 cycles command mode)
- UART I/O: human-limited interactive performance
- Save compression: 8-24x reduction (typical Zork save: 500-1500 bytes vs 12KB full)

### Dependencies

**Console Target**
- gcc or clang (C99 compiler)
- make (GNU Make or compatible)
- python3 (for test framework, optional)

**Windows Target**
- mingw-w64 (cross-compiler)
- WSL2 or native Linux build environment

**SAM E51 Target**
- arm-none-eabi-gcc (ARM embedded toolchain)
- Microchip Studio or compatible (optional, for debug)

**RISC-V Target**
- riscv64-unknown-elf-gcc or riscv32-unknown-elf-gcc (RV32I support)
- Vivado 2024.x or 2025.1 (Xilinx/AMD FPGA tools)
- Verilator 5.042+ (for simulation)
- Python 3 with requests (for game management)

### Known Limitations

**Z-machine Support**
- Version 3 only (Zork I/II/III, Deadline, Planetfall, etc.)
- No support for Version 4+ features (timed input, colors, mouse)
- No Blorb format support (graphics, sounds)
- Status line limited to text mode

**Hardware Requirements**
- RISC-V target requires Vivado license and Arty S7-50 FPGA
- SAM E51 target requires Curiosity Nano development board
- Flash write endurance: ~100,000 erase cycles per sector (wear leveling extends lifespan)

**Story File Limitations**
- Maximum story file size: 128KB (Z-machine V3 limit)
- Dynamic memory limited by target RAM (console: unlimited, embedded: 2-14KB for v3 stories)
- Multi-game mode: up to 8 games on RISC-V (flash capacity constraint)

### License

ZVibe is licensed under the BSD-3-Clause license (permissive, commercial-friendly).

Based on mojozork by Ryan C. Gordon.

Third-party components:
- SERV CPU by Olof Kindgren (ISC License)
- Servile wrapper by Olof Kindgren (Apache-2.0)
- UART cores by Alex Forencich (MIT License)

See LICENSE and target/riscv/serv_zvibe/LICENSE for details.

### Contributors

- Martin R. Raumann (primary developer)
- Ryan C. Gordon (mojozork original implementation)

[2.0.0]: https://github.com/emuck/zvibe/compare/v1.0.0...v2.0.0
[1.0.0]: https://github.com/emuck/zvibe/releases/tag/v1.0.0
