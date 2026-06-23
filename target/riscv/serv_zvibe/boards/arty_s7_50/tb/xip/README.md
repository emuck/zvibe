# SoC Execute-In-Place (XIP) Tests

System-level integration tests with programs executing directly from QSPI flash.

These tests verify the complete SoC with CPU executing code from flash via the QSPI XIP controller.

## Main Test

### Servant ZVibe XIP Testbench
**File**: `servant_zvibe_xip_tb.sv`
**Makefile**: `Makefile.xip`

Comprehensive verification of QSPI XIP integration with full SoC.

**Features**:
- Full SoC instantiation (CPU + RAM + Flash + UART + peripherals)
- s25fl_simple flash model with `$readmemh` file loading
- UART output capture to `uart_output.txt`
- Optional UART input stimulus from `uart_input.txt`
- Configurable simulation time and timeouts
- Graceful exit via ##EXIT## sequence
- Fast UART mode for quick iterations (100x+ faster)

**Build and Run**:
```bash
# Build XIP firmware first
cd ../../sw
make uart_echo_xip.bin

# Return to testbench directory
cd ../tb/soc/xip

# Run test with echo program
make -f Makefile.xip echo

# Run with custom firmware
make -f Makefile.xip test1

# Build only
make -f Makefile.xip build

# Run only (if already built)
make -f Makefile.xip run

# Clean
make -f Makefile.xip clean
```

## XIP Boot Sequence

1. **CPU Reset** → PC = 0x00000000 (RAM)
2. **Boot Stub Execution** (8 bytes in RAM):
   ```assembly
   lui  t0, 0x80400      # t0 = 0x80400000
   jalr zero, t0, 0      # Jump to flash
   ```
3. **Flash Execution** → CPU continues from 0x80400000 via QSPI
4. **XIP Code Running** → All instructions fetched from flash

## Memory Map

```
0x00000000 - 0x000001FF   Boot stub (512 bytes)
0x00000200 - 0x00007FFF   RAM (31.5KB for data/stack)
0x40000000 - 0x4000000F   UART peripheral
0x40000010 - 0x4000001F   Timer peripheral
0x40000020 - 0x4000002F   GPIO LEDs
0x40000030 - 0x4000003F   Flash status (read-only)
0x80400000 - 0x80FFFFFF   Flash XIP region (12MB)
```

## Firmware Structure

### Boot Stub (RAM)
**File**: `../../sw/flash_boot_banner.hex`
- Loaded into RAM at 0x00000000
- 8-byte jump to flash: `lui t0, 0x80400; jalr zero, t0, 0`

### XIP Application (Flash)
**File**: `../../sw/uart_echo_xip.bin` (or other XIP program)
- Loaded into flash at offset 0x400000
- CPU executes from 0x80400000 (maps to flash 0x400000)
- Stack and variables use RAM (0x00000200+)

**Linker Script**: `../../sw/xip_link.ld`

## UART File I/O

See **README_FILE_IO.md** for detailed documentation on UART file capture and stimulus.

### Output Capture
UART transmissions are automatically saved to `uart_output.txt`:
```
BOOT!
GO!
FLASH=DEADBEEF
Running from XIP!
```

### Input Stimulus
Create `uart_input.txt` with characters to send to RX:
```
Hello
Test
```

## Simulation Control

See **README_TIMEOUT.md** for detailed timeout and exit control documentation.

### Parameters
- **MAX_CYCLES**: Maximum simulation time (0 = unlimited)
  - 1,000,000 = 10ms (quick tests)
  - 100,000,000 = 1 second (default)
  - 600,000,000,000 = 1 hour @ 166.66MHz (3600 ns/cycle)

- **INACTIVITY_TIMEOUT**: Auto-exit if no UART activity (0 = disabled)
  - 50,000,000 = 500ms of no UART = exit (default)

- **FAST_UART**: Use fast Wishbone model (bypasses bit-level serialization)
  - 0 = Real UART (accurate, ~100x slower)
  - 1 = Fast model (instant writes, for development)

### Graceful Exit
Firmware can write `##EXIT##` to UART to end simulation:
```c
uart_puts("##EXIT##");  // Simulation ends immediately
```

## Building XIP Firmware

```bash
cd ../../sw

# Build XIP application
make uart_echo_xip.bin

# Required files for testbench:
# - flash_boot_banner.hex (boot stub, copied to ram_boot.hex)
# - uart_echo_xip.bin (converted to test_firmware.hex with offset)

# The Makefile.xip automatically prepares these files
```

## Typical Simulation Flow

```bash
# 1. Build firmware
cd ../../sw && make uart_echo_xip.bin && cd ../tb/soc/xip

# 2. Run simulation (builds if needed)
make -f Makefile.xip echo

# 3. View UART output
cat uart_output.txt

# 4. View waveforms
gtkwave build_xip/dump.vcd
```

## Debugging XIP Issues

### Common Issues

1. **No UART output**
   - Check boot stub hex file is loaded correctly
   - Verify firmware built with XIP linker script
   - Check flash model loaded firmware at correct offset

2. **CPU stuck/infinite loop**
   - View waveforms at PC and instruction fetch
   - Check flash model responds to QSPI transactions
   - Verify firmware endianness (little-endian for RISC-V)

3. **Simulation timeout**
   - Increase MAX_CYCLES
   - Use FAST_UART=1 for quicker iteration
   - Check INACTIVITY_TIMEOUT isn't too aggressive

### Debug Flags
```systemverilog
// In testbench, enable debug output:
parameter FLASH_DEBUG = 1;  // Flash model debug
```

## Performance

**Real UART** (FAST_UART=0):
- Accurate bit-level timing
- ~10ms simulation time per character @ 115200 baud
- Good for final verification

**Fast UART** (FAST_UART=1):
- Instant character transmission
- ~100x+ faster simulation
- Good for development and debugging

**Recommendation**: Use FAST_UART=1 during development, FAST_UART=0 for final verification.

## Status

All XIP tests **PASS** as of 2026-01-08.

Test examples:
- ✅ Boot stub execution
- ✅ Flash jump and XIP entry
- ✅ UART output from XIP code
- ✅ GPIO LED control from XIP
- ✅ UART echo from XIP
- ✅ ZVibe interpreter boot

## References

- Flash Controller: `../../rtl/qspi/s25fl_xip.v`
- Flash Model: `../../models/s25fl_simple.v`
- Boot Stub: `../../sw/flash_boot_banner.c`
- XIP Linker: `../../sw/xip_link.ld`
- Architecture: `../../../docs/architecture.md`
- File I/O Guide: `README_FILE_IO.md`
- Timeout Guide: `README_TIMEOUT.md`
