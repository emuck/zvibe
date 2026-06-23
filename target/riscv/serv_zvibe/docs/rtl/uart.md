# UART Controller

Wishbone UART with TX/RX FIFOs for reliable serial communication.

## Overview

**File**: `rtl/uart/uart_wb.sv`
**Author**: Integration and Wishbone interface by zvibe project
**TX/RX Cores**: Based on algorithms from alexforencich/verilog-uart (MIT License)
**Status**: Verified in simulation and hardware

The UART provides 115200 baud serial communication with 64-byte FIFOs for both transmit and receive paths.

## Features

- Wishbone B4 slave interface
- Fixed baud rate: 115200 (8N1 configuration)
- 64-byte TX FIFO
- 64-byte RX FIFO
- Status flags: TX_READY, RX_READY
- Error detection: Frame, overrun, FIFO overflow
- Combinational ACK (single-cycle response)

## Interface

### Wishbone Slave
```verilog
input  logic        i_wb_clk      // System clock (100 MHz)
input  logic        i_wb_rst      // Reset
input  logic [31:0] i_wb_adr      // Address
input  logic [31:0] i_wb_dat      // Write data
output logic [31:0] o_wb_dat      // Read data
input  logic        i_wb_we       // Write enable
input  logic        i_wb_cyc      // Cycle
input  logic        i_wb_stb      // Strobe
output logic        o_wb_ack      // Acknowledge
```

### UART Physical
```verilog
output logic        uart_txd      // Transmit data
input  logic        uart_rxd      // Receive data
```

## Register Map

Base address: 0x40000000

| Offset | Name     | Access | Description                    |
|--------|----------|--------|--------------------------------|
| 0x00   | TX_DATA  | W      | Transmit data byte             |
| 0x00   | RX_DATA  | R      | Receive data byte              |
| 0x04   | STATUS   | R      | Status and error flags         |

### STATUS Register (0x04)

| Bit | Name | Description |
|-----|------|-------------|
| 0   | TX_READY | TX FIFO not full, can accept data |
| 1   | RX_READY | RX FIFO not empty, data available |
| 2   | RX_FRAME_ERROR | Framing error detected (sticky) |
| 3   | RX_OVERRUN_ERROR | Overrun detected (sticky) |
| 4   | RX_FIFO_OVERFLOW | RX FIFO overflow, data dropped (sticky) |
| 5   | TX_FIFO_OVERFLOW | TX write rejected, FIFO full (sticky) |
| 31:6 | Reserved | Read as zero |

Error bits are sticky: set on error, cleared when STATUS register is read.

## Operation

### Transmit
```c
// Wait for TX FIFO space
while (!(UART_STATUS & TX_READY));
UART_TX_DATA = byte;
```

The TX FIFO buffers up to 64 bytes. When the FIFO has space, TX_READY is asserted. Writing to TX_DATA appends the byte to the FIFO.

### Receive
```c
// Check for received data
if (UART_STATUS & RX_READY) {
    byte = UART_RX_DATA;
}
```

The RX FIFO buffers up to 64 bytes. When data is available, RX_READY is asserted. Reading RX_DATA removes one byte from the FIFO.

### Error Handling
```c
uint32_t status = UART_STATUS;
if (status & RX_FRAME_ERROR) {
    // Handle frame error
}
if (status & RX_OVERRUN_ERROR) {
    // Handle overrun
}
// Reading STATUS clears error bits
```

## Timing

### Baud Rate

Fixed at 115200 baud:
- Bit time: 8.68 µs
- Byte time: 86.8 µs (including start/stop bits)

### Prescaler

System clock: 100 MHz
Baud rate: 115200
Prescale: 108 (Fclk / (baud × 8))

```verilog
parameter int PRESCALE = 108;
```

### Wishbone Timing

Combinational ACK - responds in the same cycle as request:

```
CLK      __|‾|_|‾|_|‾|_|‾|_
CYC      ____|‾‾‾‾‾‾|_____
STB      ____|‾‾‾‾‾‾|_____
ACK      ____|‾‾‾‾‾‾|_____
DATA     ====<valid>======
```

This single-cycle response is intentional for compatibility with SERV CPU's memory interface expectations.

## FIFO Details

### TX FIFO

Depth: 64 bytes
Operation: Write to TX_DATA appends to FIFO, UART core drains FIFO
Flags:
- `tx_fifo_full`: FIFO full, writes rejected
- `tx_fifo_empty`: FIFO empty, no transmission active
- `TX_READY`: !tx_fifo_full (exposed to software)

### RX FIFO

Depth: 64 bytes
Operation: UART core fills FIFO, reading RX_DATA removes from FIFO
Flags:
- `rx_fifo_full`: FIFO full, incoming data dropped (overflow error)
- `rx_fifo_empty`: FIFO empty, no data available
- `RX_READY`: !rx_fifo_empty (exposed to software)

## Hardware vs Simulation

### Hardware Behavior
On Arty S7-50 hardware, TX_READY polling is **required**:
```c
while (!(UART_STATUS & TX_READY));  // REQUIRED on hardware
UART_TX_DATA = byte;
```

Without polling, characters are dropped when the FIFO is full.

### Simulation Behavior
With `FAST_UART=1` flag, simulation bypasses FIFOs:
```c
UART_TX_DATA = byte;  // Works in simulation without polling
```

This speeds up simulation but doesn't reflect hardware behavior. Test programs should use polling to be hardware-compatible.

## Error Conditions

### Frame Error
Occurs when stop bit is not detected. Usually indicates:
- Wrong baud rate
- Noise on line
- Cable disconnected

### Overrun Error
Occurs when a new byte arrives before the previous one is read from the core. The FIFO should prevent this in normal operation.

### RX FIFO Overflow
Occurs when RX FIFO is full and new data arrives. Data is dropped.

Prevention: Read RX_DATA regularly when RX_READY is asserted.

### TX FIFO Overflow
Occurs when software writes to TX_DATA while TX FIFO is full. Write is rejected, byte is lost.

Prevention: Check TX_READY before writing.

## Attribution and Licensing

### UART TX/RX Cores
Original work: **alexforencich/verilog-uart**
- Copyright (c) 2015-2023 Alex Forencich
- License: MIT License
- Repository: https://github.com/alexforencich/verilog-uart
- Files: uart_tx.v, uart_rx.v algorithms

### Integration and Wishbone Interface
- Copyright (c) 2025 zvibe project
- License: BSD-3-Clause
- Files: uart_wb.sv, fifo_sync.sv integration

## Design Notes

### Combinational ACK
The UART uses combinational ACK (asserted same cycle as request) for SERV CPU compatibility. This is intentional and simplifies the CPU's memory interface.

### FIFO Sizing
64-byte FIFOs provide sufficient buffering for interactive applications. For bulk data transfer, larger FIFOs may be needed.

### Baud Rate
Fixed at 115200. To change, modify the PRESCALE parameter:
```verilog
PRESCALE = (CLK_FREQ / (BAUD_RATE * 8))
```

Example: 9600 baud @ 100 MHz:
```verilog
PRESCALE = (100000000 / (9600 * 8)) = 1302
```

## Performance

| Metric | Value |
|--------|-------|
| Baud Rate | 115200 |
| Throughput | ~11.5 KB/s |
| TX FIFO Depth | 64 bytes |
| RX FIFO Depth | 64 bytes |
| LUTs Used | ~50 |

## Verification

### Simulation
- Testbench: `tb/servant_zvibe_xip_tb.sv`
- Tests: UART echo, continuous output, back-to-back characters
- UART output captured to `uart_output.txt`
- UART input from `uart_input.txt`

See `tb/README_FILE_IO.md` for details.

### Hardware
Platform: Arty S7-50 FPGA

Verified:
- Character echo at 115200 baud
- Continuous text output (ZVibe interpreter)
- TX_READY polling requirement
- FIFO operation under load

## Related Files

- `rtl/uart/uart_wb.sv` - UART controller
- `rtl/uart/fifo_sync.sv` - Synchronous FIFO
- `rtl/uart/uart_tx.sv` - UART TX core (based on alexforencich)
- `rtl/uart/uart_rx.sv` - UART RX core (based on alexforencich)
- `tb/servant_zvibe_xip_tb.sv` - SoC testbench with UART capture

## References

- [alexforencich/verilog-uart](https://github.com/alexforencich/verilog-uart) - Original TX/RX core algorithms
- [Wishbone B4 Specification](https://cdn.opencores.org/downloads/wbspec_b4.pdf)
