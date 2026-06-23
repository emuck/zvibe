# QSPI Flash Write Controller

Flash write/erase controller for S25FL128S/S25FL127S QSPI flash memory.

## Overview

**File**: `rtl/qspi/s25fl_write.v`
**Author**: Custom design for Servant ZVibe project
**Status**: Verified in simulation and hardware
**Target Flash**: Spansion/Cypress/Infineon S25FL128S (16MB)

The write controller provides flash erase and program operations for persistent game saves. It operates separately from the XIP read controller, using a pin multiplexer (qspi_mux.v) to arbitrate access to the shared QSPI pins.

## Features

- Write Enable (WREN - 0x06) command
- Sector Erase (SE - 0xD8) - 64KB sectors
- Page Program (PP - 0x02) - up to 256 bytes per page
- Read Status Register (RDSR - 0x05) for polling WIP bit
- Status polling with configurable timeout
- SPI mode (single I/O, not QUAD) for reliability
- Register-based control interface
- Separate from XIP controller - arbitrated via qspi_mux

## Architecture

### Design Philosophy

The write controller is intentionally **separate** from the XIP read controller:

- **Read path stays fast**: XIP controller optimized for code execution
- **Write complexity isolated**: Erase/program state machines don't affect reads
- **Arbitration via mux**: qspi_mux.v handles pin sharing
- **Priority to reads**: XIP always has priority, writes wait for idle

### Pin Sharing

```
┌──────────────┐      ┌──────────┐      ┌───────────┐
│ s25fl_xip.v  │─────▶│          │      │           │
│ (XIP Read)   │      │ qspi_mux │─────▶│  S25FL128S│
│              │      │          │      │   Flash   │
│s25fl_write.v │─────▶│          │      │           │
│ (Write/Erase)│      └──────────┘      └───────────┘
└──────────────┘
     Priority: XIP > Write
```

The mux monitors `o_active` from each controller and routes signals accordingly.

## Interface

### Control Interface (Register-Based)

```verilog
// Command strobes (pulse high for 1 clock cycle)
input         i_cmd_wren       // Write enable strobe
input         i_cmd_se         // Sector erase strobe
input         i_cmd_pp         // Page program strobe
input         i_cmd_rdsr       // Read status register strobe

// Address and data
input  [23:0] i_address        // Flash address (byte-addressed)
input  [7:0]  i_data           // Write data (for PP)
input         i_data_valid     // Data valid strobe (for PP multi-byte)

// Status outputs
output        o_busy           // Operation in progress
output        o_error          // Error occurred (timeout, etc.)
output [7:0]  o_status         // Status register value (from RDSR)
output [7:0]  o_data_out       // Read data output
```

### QSPI Physical Interface

```verilog
output        o_qspi_sck       // Serial clock
output        o_qspi_cs_n      // Chip select (active low)
output [3:0]  o_qspi_dat       // Data to flash (only [0] used in SPI mode)
output [3:0]  o_qspi_oe        // Output enable per bit
input  [3:0]  i_qspi_dat       // Data from flash (only [1] used in SPI mode)
```

### Arbitration

```verilog
output        o_active         // Write controller has control
input         i_xip_idle       // XIP controller is idle
```

## Flash Commands

### 1. Write Enable (WREN - 0x06)

**Purpose**: Enable subsequent erase or program operations

**Sequence**:
```
1. Assert CS# (low)
2. Send 0x06 command byte
3. Deassert CS# (high) - CRITICAL!
```

**Timing**: <1 µs

**Note**: Must be sent before EVERY erase or program operation. The flash automatically clears the write enable latch after each erase/program.

### 2. Sector Erase (SE - 0xD8)

**Purpose**: Erase 64KB sector to 0xFF

**Sequence**:
```
1. Send WREN (must complete first)
2. Assert CS#
3. Send 0xD8 command byte
4. Send 24-bit address (sector-aligned)
5. Deassert CS#
6. Poll RDSR until WIP=0
```

**Timing**: 200-700ms typical (can take up to 2 seconds worst-case)

**Addresses**: Must be sector-aligned (0x000000, 0x010000, 0x020000, etc.)

### 3. Page Program (PP - 0x02)

**Purpose**: Program up to 256 bytes within a page

**Sequence**:
```
1. Send WREN (must complete first)
2. Assert CS#
3. Send 0x02 command byte
4. Send 24-bit address
5. Send 1-256 data bytes
6. Deassert CS#
7. Poll RDSR until WIP=0
```

**Timing**: 0.4-3ms typical per page

**Page boundaries**: Address must not cross 256-byte page boundary
(addresses 0x000-0x0FF, 0x100-0x1FF, etc.)

**Flash physics**: Can only change bits from 1→0. Must erase (set all to 1) before programming.

### 4. Read Status Register (RDSR - 0x05)

**Purpose**: Poll Write In Progress (WIP) bit

**Sequence**:
```
1. Assert CS#
2. Send 0x05 command byte
3. Read status byte
4. Deassert CS#
```

**Status Register Bits**:
- Bit 0 (WIP): 1 = operation in progress, 0 = ready
- Bit 1 (WEL): 1 = write enable latch set
- Bits 2-7: Error flags and protection bits

## State Machine

### States

```
STATE_IDLE           - Wait for command
STATE_WREN           - Send WREN command
STATE_WREN_END       - Deassert CS# after WREN (required!)
STATE_SE_CMD         - Send SE (0xD8) command
STATE_SE_ADDR        - Send SE 24-bit address
STATE_PP_CMD         - Send PP (0x02) command
STATE_PP_ADDR        - Send PP 24-bit address
STATE_PP_DATA        - Send PP data bytes
STATE_RDSR_CMD       - Send RDSR (0x05) command
STATE_RDSR_DATA      - Read RDSR status byte
STATE_POLL_STATUS    - Poll status until WIP=0
STATE_WAIT_COMPLETE  - Final state before idle
STATE_ERROR          - Error occurred
```

### Typical Operation Flows

**Sector Erase**:
```
IDLE → WREN → WREN_END → SE_CMD → SE_ADDR →
  POLL_STATUS (loop) → WAIT_COMPLETE → IDLE
```

**Page Program**:
```
IDLE → WREN → WREN_END → PP_CMD → PP_ADDR → PP_DATA (loop) →
  POLL_STATUS (loop) → WAIT_COMPLETE → IDLE
```

**Status Read**:
```
IDLE → RDSR_CMD → RDSR_DATA → IDLE
```

## Usage Example

### Firmware API

The write controller is accessed via `boards/arty_s7_50/sw/flash_driver.c`:

```c
// Erase sector (64KB)
int flash_erase_sector(uint32_t addr);

// Write bytes (handles page boundaries)
int flash_write(uint32_t addr, const uint8_t *data, size_t len);

// Read status
uint8_t flash_read_status(void);
```

### Low-Level Register Access

```c
#define FLASH_WRITE_BASE  0x40000040

#define FLASH_CMD_WREN   (FLASH_WRITE_BASE + 0x00)
#define FLASH_CMD_SE     (FLASH_WRITE_BASE + 0x04)
#define FLASH_CMD_PP     (FLASH_WRITE_BASE + 0x08)
#define FLASH_CMD_RDSR   (FLASH_WRITE_BASE + 0x0C)
#define FLASH_ADDR       (FLASH_WRITE_BASE + 0x10)
#define FLASH_DATA       (FLASH_WRITE_BASE + 0x14)
#define FLASH_STATUS     (FLASH_WRITE_BASE + 0x18)

// Erase sector at 0xF00000
*(volatile uint32_t*)FLASH_ADDR = 0xF00000;
*(volatile uint32_t*)FLASH_CMD_SE = 1;

// Wait for completion
while (*(volatile uint32_t*)FLASH_STATUS & 0x01) { }
```

## Timing and Performance

### Operation Latencies

| Operation | Typical | Maximum |
|-----------|---------|---------|
| WREN | <1 µs | <10 µs |
| Sector Erase (64KB) | 200-700 ms | 2000 ms |
| Page Program (256B) | 0.4-3 ms | 5 ms |
| RDSR | <10 µs | <50 µs |

### Status Polling

The controller polls RDSR every ~1ms during erase/program operations. Timeouts prevent infinite loops:

- Erase timeout: 3 seconds
- Program timeout: 10 ms

## Design Notes

### SPI Mode vs QUAD Mode

The write controller uses **SPI mode** (single I/O) instead of QUAD mode:

**Reasons**:
- Simpler state machine (fewer bits to manage)
- More reliable (proven, less complex)
- Write operations are infrequent (only during saves)
- Speed not critical (user waits for save dialog)
- QUAD mode complexity not justified for ~3ms page program

### WREN_END State

The flash datasheet requires deasserting CS# after WREN before the next command. The dedicated `STATE_WREN_END` state ensures proper timing.

### Arbitration Priority

XIP (read) controller always has priority over writes:
- Games need code execution without interruption
- Writes only occur during user-initiated saves
- Write operations wait for XIP to be idle

### Initial Values

The module uses `initial` blocks to set proper startup state:
- CS# deasserted (high)
- All outputs tristated
- `o_active` low (allows XIP to work immediately)

This is **critical** for FPGA configuration - ensures flash isn't confused during bitstream loading.

## Resource Usage

Measured on Arty S7-50 (XC7S50):

| Resource | Count | % of FPGA |
|----------|-------|-----------|
| LUTs | ~120 | 0.37% |
| Slice Registers | ~154 | 0.24% |
| QSPI Pins | Shared with XIP | N/A |

Combined with XIP controller (~150 LUTs), total flash controller overhead is ~270 LUTs.

## Verification

### Simulation Tests

File: `tb/unit/flash/flash_write_test.sv`

Tests:
1. WREN command
2. Sector erase with status polling
3. Single-byte page program
4. Multi-byte page program (sequential)
5. Page boundary handling
6. Timeout detection
7. Error conditions

### Hardware Tests

File: `common/sw/flash_save_test.c`

23 tests covering:
- Write enable
- Sector erase verification
- Page program verification
- Multi-sector operations
- Save/restore round-trip
- Wear leveling slot allocation
- CRC integrity checking

All tests pass on Arty S7-50 hardware.

## Known Limitations

- SPI mode only (not QUAD) - intentional design choice
- No error correction or retry logic
- Assumes flash is already in correct mode (initialized by bootloader)
- Timeout values are conservative (could be tuned for faster detection)
- No DMA or interrupt support (polled operation only)

## Flash Write Endurance

S25FL128S specification:
- **Endurance**: 100,000 program/erase cycles per sector
- **Data retention**: 20 years @ 85°C

ZVibe wear leveling spreads writes across 16 sectors (240 save slots):
- 100,000 cycles/sector × 16 sectors = 1.6 million saves total
- 3 slots/game × 8 games = 24 active slots
- Average wear: 1.6M / 24 ≈ 66,000 saves per slot before wear out

**Typical usage**: Interactive fiction players save 10-50 times per game
**Lifespan**: Easily supports decades of gameplay

## Related Files

### RTL
- `rtl/qspi/s25fl_write.v` - Write controller RTL
- `rtl/qspi/s25fl_xip.v` - XIP read controller
- `rtl/qspi/qspi_mux.v` - Pin multiplexer (arbitration)

### Firmware
- `boards/arty_s7_50/sw/flash_driver.c` - Low-level flash operations (Arty QSPI HAL)
- `sw/flash_save.c` - Save/restore system with wear leveling
- `sw/save_delta.c` - Delta compression for saves

### Tests
- `tb/unit/flash/flash_write_test.sv` - RTL testbench
- `sw/flash_save_test.c` - Hardware test suite

### Documentation
- `docs/FLASH_SAVE_RESTORE_ANALYSIS.md` - Save system design
- `docs/FLASH_WRITE_INTEGRATION_PLAN.md` - Integration architecture
- `docs/FLASH_WRITE_WISHBONE_INTERFACE.md` - Interface design (archived)
- `docs/rtl/qspi_xip.md` - XIP read controller

## References

- [S25FL128S Datasheet](https://www.infineon.com/assets/row/public/documents/10/49/infineon-s25fl128s-s25fl256s-128-mb-16-mb-256-mb-32-mb-fl-s-flash-spi-multi-io-3-v-datasheet-en.pdf)
- Flash command set: Section 9 of datasheet
- Write operations: Section 9.5-9.7 of datasheet
- Status register: Section 9.2.4 of datasheet
