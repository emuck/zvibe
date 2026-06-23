# QSPI Flash Controller Unit Tests

Unit-level verification of the QSPI XIP flash controller (`s25fl_xip.v`).

## Tests

### QSPI XIP Wishbone Interface Test
**File**: `s25fl_xip_wb_tb.sv`

Comprehensive test of the QSPI XIP controller's Wishbone interface and flash protocol.

**Tests Included**:
1. **Wishbone Single Read** - Read 0x80400000 → 0xEFBEADDE (little-endian)
2. **Wishbone STALL** - Verify STALL generation during flash access
3. **Address Translation** - CPU address 0x80400000 → flash offset 0x400000
4. **Back-to-back Reads** - Multiple sequential reads with idle cycles
5. **True Back-to-back** - Continuous reads without idle cycles
6. **Startup Busy** - Handle STALL during WREN/WRR initialization
7. **Unaligned Addresses** - Critical for Z-machine dictionary reads

**Build and Run**:
```bash
# Verilator (with waveforms)
make -f Makefile.xip_wb run

# Vivado xsim (for cross-simulator validation)
make -f Makefile.xip_wb xsim

# Both simulators
make -f Makefile.xip_wb all-sims

# View waveforms (Verilator)
gtkwave build_xip_wb/dump.vcd
```

**Expected Output**:
```
TEST 1: Wishbone single read
*** PASS ***

TEST 2: Wishbone STALL generation
*** PASS ***

...

=== TEST SUMMARY ===
Total tests: 7
Passed: 7
Failed: 0

*** ALL TESTS PASSED ***
```

## Flash Controller Architecture

The QSPI XIP controller implements Execute-In-Place from QSPI flash:

**Module**: `s25fl_xip.v`
- Wishbone B4 slave interface
- QIOR (0xEB) Quad I/O Read command
- Continuous read mode (mode byte 0xA0)
- Address translation (0x804xxxxx → 0x4xxxxx)
- Startup initialization (WREN + WRR for QUAD mode)

**Key Features**:
- **Clock Divider**: sys_clk/6 = 16.67MHz QSPI clock (@ 100MHz sys_clk)
- **Continuous Mode**: Skips command byte on subsequent reads (2.4x faster)
- **STALL Generation**: Asserted during flash access
- **Direct Output Enable**: Fine-grained control of QSPI data tristate

**Flash Model**: `s25fl_simple.v`
- Simplified S25FL128S behavioral model
- Compatible with Verilator and Vivado xsim
- Supports `$readmemh` for file loading
- Debug mode for detailed operation logging

## Protocol Details

### Initial Read Sequence
```
Command: 0xEB (QIOR)
Address: 24 bits
Mode: 0xA0 (continuous read enable)
Dummy: 4 cycles (16 clock cycles)
Data: 32 bits (8 nibbles, little-endian)
```

### Continuous Read Sequence
```
Address: 24 bits (no command byte!)
Mode: 0xA0
Dummy: 4 cycles
Data: 32 bits
```

### Address Translation
- CPU address: `0x80400000 - 0x80FFFFFF` (12MB)
- Flash offset: `0x400000 - 0xFFFFFF` (12MB at 4MB offset)

## Dual-Simulator Support

The testbench is verified with both Verilator and Vivado xsim to ensure:
- Portability across simulation tools
- Consistent flash model behavior
- Cross-validation of results

**TCL Script**: `run_xip_wb_xsim.tcl` - Vivado xsim automation

## Status

All tests **PASS** on both Verilator and Vivado xsim as of 2026-01-08.

## References

- Flash Controller RTL: `../../rtl/qspi/s25fl_xip.v`
- Flash Model: `../../models/s25fl_simple.v`
- Flash Controller Documentation: `../../rtl/qspi/FLASH_CONTROLLER_AUDIT.md`
- Vendor Model: `../../models/s25fl128s.v` (full Spansion/Cypress model)
