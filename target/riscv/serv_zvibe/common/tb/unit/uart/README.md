# UART Unit Tests

Unit-level verification of the UART Wishbone controller (`uart_wb.sv`).

## Tests

### UART Wishbone Interface Test
**File**: `uart_wb_tb.sv`

Comprehensive test of the UART Wishbone interface, FIFOs, and error handling.

**Tests Included**:
1. **Basic TX/RX** - Transmit and receive functionality
2. **TX FIFO Overflow** - Error flag when writing to full TX FIFO
3. **RX FIFO Overflow** - Error flag when RX FIFO overflows
4. **Error Flag Sticky** - Errors persist until status read
5. **Frame Error** - Detect framing errors from RX core
6. **Overrun Error** - Detect overrun conditions

**Build and Run**:
```bash
# Verilator (with waveforms)
make -f Makefile.uart run

# View waveforms
gtkwave build_uart/dump.vcd
```

**Expected Output**:
```
TEST 1: Basic TX/RX
*** PASS ***

TEST 2: TX FIFO Overflow Error
*** PASS ***

...

=== TEST SUMMARY ===
Total tests: 6
Passed: 6
Failed: 0

*** ALL TESTS PASSED ***
```

## UART Module Architecture

The UART controller consists of:
- **uart_wb.sv** - Wishbone slave interface, FIFOs, error flags
- **uart_tx.sv** - Transmit serializer with prescaler
- **uart_rx.sv** - Receive deserializer with frame detection
- **fifo_sync.sv** - Synchronous FIFO (64 bytes depth)

**Features**:
- Configurable prescaler (PRESCALE parameter)
- Configurable FIFO depth (FIFO_DEPTH parameter)
- TX/RX ready flags for firmware polling
- Error flags: TX overflow, RX overflow, frame, overrun
- Sticky error flags (cleared on status read)

## Status

All tests **PASS** (Feb 2026). Each test emits `PASS: uart_wb_tb` on success for regression integration. Run via `./run_regression.sh --tier1` or `make sim-tier1` from the repo root.

## References

- UART RTL: `../../rtl/uart/uart_wb.sv`, `uart_tx.sv`, `uart_rx.sv`, `fifo_sync.sv`
- UART Documentation: `../../../docs/rtl/uart.md`
- Model: `../../models/uart_wb_model.sv` (fast Wishbone model for SoC tests)
