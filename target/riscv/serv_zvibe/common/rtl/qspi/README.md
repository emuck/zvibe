# QSPI Flash Controllers

This directory contains QSPI flash controller implementations for the Servant ZVibe SoC.

## Active Controllers (Production Use)

### s25fl_xip.v - Read Controller (XIP)
- **Status**: ✅ Active in production builds
- **Purpose**: Execute-In-Place (XIP) from QSPI flash
- **Features**: QIOR (0xEB) quad I/O read, continuous read mode
- **Performance**: ~2.4x faster with continuous mode
- **Copyright**: (c) 2025 Martin R. Raumann
- **License**: BSD-3-Clause

### s25fl_write.v - Write Controller
- **Status**: ✅ Active in production builds
- **Purpose**: Flash erase and program operations
- **Features**: WREN, SE (sector erase), PP (page program), RDSR
- **Mode**: SPI (not QUAD) for reliability
- **Copyright**: (c) 2025 Martin R. Raumann
- **License**: BSD-3-Clause

### qspi_mux.v - Pin Multiplexer
- **Status**: ✅ Active in production builds
- **Purpose**: Arbitrate QSPI pins between read and write controllers
- **Priority**: XIP has priority (writes wait for reads)
- **Copyright**: (c) 2025 Martin R. Raumann
- **License**: BSD-3-Clause

## Test/Debug Utilities

### wb_ram_flash_mimic.v - Test RAM
- **Status**: Test utility only
- **Purpose**: Fast RAM that mimics flash address space for testing
- **Copyright**: (c) 2025 Martin R. Raumann
- **License**: BSD-3-Clause

## Controller Selection Rationale

**Why s25fl_xip.sv instead of a third-party QSPI controller?**

1. **License**: BSD-3-Clause — no copyleft constraints
2. **Customization**: Tailored specifically for S25FL128S and the SERV bit-serial CPU
3. **Simplicity**: Minimal design optimized for sequential XIP reads
4. **Continuous mode**: QIOR (0xEB) + continuous read mode gives 2.4x speedup
5. **Verified**: Tested on Arty S7-50 hardware

## Note on qflexpress.v (LGPL-3.0)

`qflexpress.v` by Gisselquist Technology, LLC (LGPL-3.0) was evaluated
during development as an alternative XIP controller and was present in
early archived testbenches.  **It has been removed from this repository.**

> **Do not reintroduce `qflexpress.v` or any other LGPL-licensed module
> into any Makefile, synthesis filelist, or simulation target.**
> LGPL-3.0 is copyleft; linking it into an FPGA bitstream would impose
> LGPL conditions on the combined work, which is incompatible with the
> project's BSD-3-Clause outbound license.

## License Compatibility

All files in this directory are BSD-3-Clause (zvibe project).
