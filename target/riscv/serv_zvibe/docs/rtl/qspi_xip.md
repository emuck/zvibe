# QSPI XIP Controller

Custom Execute-In-Place (XIP) controller for S25FL128S/S25FL127S QSPI flash memory.

## Overview

**File**: `rtl/qspi/s25fl_xip.v`
**Author**: Custom design for Servant ZVibe project
**Status**: Verified in simulation and hardware
**Target Flash**: Spansion/Cypress/Infineon S25FL128S (16MB)

The controller enables the RISC-V CPU to execute code directly from flash memory using the QIOR (0xEB) Quad I/O Read command.

## Features

- Wishbone B4 slave interface
- QIOR (0xEB) Quad I/O Read command
- 6 dummy cycles for safe operation at 16.7 MHz QSPI clock
- Supports back-to-back reads without gaps
- Classic Wishbone timing (single-cycle ACK)
- Read-only (writes handled by separate s25fl_write.v controller)

## Interface

### Wishbone Slave
```verilog
input         i_wb_cyc         // Wishbone cycle
input         i_wb_stb         // Wishbone strobe
input         i_wb_we          // Wishbone write enable (unused, read-only)
input  [29:0] i_wb_addr        // Wishbone address (word-addressed)
input  [31:0] i_wb_data        // Wishbone write data (unused)
input  [3:0]  i_wb_sel         // Wishbone byte select (unused)
output        o_wb_ack         // Wishbone acknowledge
output        o_wb_stall       // Wishbone stall
output [31:0] o_wb_data        // Wishbone read data
```

### QSPI Flash
```verilog
output        o_qspi_cs_n      // Chip select (active low)
output        o_qspi_clk       // QSPI clock (~16.7 MHz)
output [3:0]  o_qspi_oe        // Output enable for IO[3:0]
output [3:0]  o_qspi_do        // Data output to flash
input  [3:0]  i_qspi_di        // Data input from flash
```

## Flash Command Protocol

### QIOR (0xEB) - Quad I/O Read

Command sequence:
1. **Command**: 0xEB (1 byte, single I/O)
2. **Address**: 3 bytes (quad I/O)
3. **Mode**: 0x00 (1 byte, quad I/O) - continuous mode disabled
4. **Dummy**: 6 cycles (quad I/O) - LC=00 default for S25FL128S
5. **Data**: N bytes (quad I/O)

**Note**: Continuous read mode (mode byte 0xA0) is supported by the hardware but disabled in production builds. Testing showed it was unreliable and unnecessary for the UART-limited interactive gameplay.

## Timing

### Clock Generation

System clock: 100 MHz
QSPI clock: 16.7 MHz (divide by 6)

```verilog
reg [2:0] clk_divider;
always @(posedge clk) clk_divider <= clk_divider + 1;
wire spi_clk_en = (clk_divider == 0);
```

### Read Latency

**Per read** (typical):
- Command phase: 8 cycles
- Address phase: 24 cycles (6 cycles × 4 nibbles)
- Mode phase: 2 cycles
- Dummy phase: 6 cycles (LC=00)
- Data phase: 32 cycles (8 cycles × 4 nibbles)
- Total: ~168 system clock cycles @ 166.66 MHz = 1.0 µs

For reference, SERV CPU instruction execution takes ~32 cycles, so flash reads are ~5 instructions of latency. This is acceptable for interactive text games where user input is the primary bottleneck.

### Wishbone Timing

The controller asserts ACK for one cycle when data is ready:

```
CLK      __|‾|_|‾|_|‾|_|‾|_|‾|_|‾|_|‾|_
CYC      ______|‾‾‾‾‾‾‾‾‾‾‾|________
STB      ______|‾‾‾‾‾‾‾‾‾‾‾|________
ACK      ________________|‾|________
DATA     ----------------<valid>----
```

Classic Wishbone timing: ACK only asserted when CYC and STB are both high.

## State Machine

States:
- **STATE_IDLE**: Wait for Wishbone request
- **STATE_SEND_CMD**: Send 0xEB command
- **STATE_SEND_ADDR**: Send 24-bit address
- **STATE_LOAD_MODE**: Load mode byte 0x00
- **STATE_SEND_MODE**: Send mode byte
- **STATE_DUMMY**: Wait for 6 dummy cycles
- **STATE_READ_DATA**: Read 32-bit data
- **STATE_LATCH**: Latch data and assert ACK

All reads go through the full command sequence for reliability.

## Address Translation

Wishbone address (word-addressed) to flash byte address:

```verilog
wire [23:0] flash_addr = {i_wb_addr[21:0], 2'b00};
```

CPU address 0x80400000 maps to:
- Wishbone address: 0x00000000 (after interconnect decoding)
- Flash byte address: 0x00000000

Flash physical offset 0x400000 is handled by the flash programming tool (.mcs file generation).

## Design Notes

### Classic Wishbone Compatibility

The controller uses `cyc_acked` to prevent duplicate ACKs:

```verilog
reg cyc_acked;

always @(posedge clk) begin
    if (!i_wb_cyc) begin
        cyc_acked <= 1'b0;
    end else if (o_wb_ack) begin
        cyc_acked <= 1'b1;
    end
end

assign o_wb_ack = (state == STATE_LATCH) && !cyc_acked;
```

This ensures ACK is asserted only once per Wishbone cycle, preventing the CPU from seeing duplicate data.

### Output Enable Control

QSPI IO pins are bidirectional. The controller explicitly controls the output enable:

```verilog
assign o_qspi_oe[0] = (sending command or address);
assign o_qspi_oe[3:0] = (sending mode byte);
assign o_qspi_oe[3:0] = 4'b0000 (reading data);
```

### Mode Byte Buffering

The mode byte (0xA0) is loaded in STATE_LOAD_MODE before being sent in STATE_SEND_MODE. This one-cycle delay ensures clean timing.

## Verification

### Simulation Tests

File: `tb/s25fl_xip_wb_tb.sv`

Tests:
1. Single read from address 0x00000000
2. Sequential reads (continuous mode)
3. Back-to-back reads (tests duplicate ACK fix)
4. Random address reads
5. Address translation verification

All tests pass with correct data and single ACKs.

### Hardware Tests

Platform: Arty S7-50 FPGA with S25FL128S flash

Verified:
- Boot sequence (RAM stub → flash jump → XIP execution)
- Continuous read mode activation
- UART output from flash-based firmware
- ZVibe Z-machine interpreter execution

## Known Limitations

- Read-only (writes handled by separate s25fl_write.v controller)
- Fixed QSPI clock divider (not configurable)
- No error detection or retry logic
- Assumes flash is pre-initialized to QUAD mode
- Continuous read mode code present but disabled (ENABLE_CONTINUOUS_MODE=0)

## Performance

Measured on Arty S7-50 with 166.66 MHz system clock:

| Metric | Value |
|--------|-------|
| QSPI Clock | 16.7 MHz (CLK_DIV=2) |
| Read Latency | 1.0 µs (168 cycles) |
| Throughput | ~4 MB/s |
| LUTs Used | ~150 |
| Performance | Sufficient for interactive text games |

## Flash Initialization

The controller assumes the flash is already configured for QUAD mode. This is done during SoC initialization by the write controller (s25fl_write.v) which sets CR1[1]=1 via Write Register (WRR) command.

Required flash configuration:
- QUAD mode enabled (CR1[1] = 1)
- Latency Code LC=00 (6 dummy cycles)

## References

- [S25FL128S Datasheet](https://www.infineon.com/assets/row/public/documents/10/49/infineon-s25fl128s-s25fl256s-128-mb-16-mb-256-mb-32-mb-fl-s-flash-spi-multi-io-3-v-datasheet-en.pdf)
- [Wishbone B4 Specification](https://cdn.opencores.org/downloads/wbspec_b4.pdf)

## Related Files

- `rtl/qspi/s25fl_xip.v` - XIP read controller RTL
- `rtl/qspi/s25fl_write.v` - Flash write controller (erase/program)
- `rtl/qspi/qspi_mux.v` - Pin multiplexer (arbitrates XIP vs write)
- `tb/s25fl_xip_wb_tb.sv` - Wishbone testbench
- `tb/servant_zvibe_xip_tb.sv` - Full SoC testbench
- `tb/models/s25fl_simple_rw.v` - Flash behavioral model with R/W
