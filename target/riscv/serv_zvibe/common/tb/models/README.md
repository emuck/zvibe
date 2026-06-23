# Behavioral Models

Simulation models used across all testbenches.

## Flash Models

### s25fl_simple.v
Simplified behavioral model of Spansion/Cypress S25FL128S QSPI flash.

**Features**:
- Compatible with Verilator and Vivado xsim
- QIOR (0xEB) Quad I/O Read command
- Continuous read mode (mode byte 0xA0)
- File loading via `$readmemh`
- Configurable debug output
- 16MB capacity (24-bit addressing)

**Usage**:
```systemverilog
s25fl_simple #(
    .MEM_FILE("firmware.hex"),
    .MEM_OFFSET(24'h400000),
    .DEBUG(1)
) flash_model (
    .qspi_sck(sck),
    .qspi_cs_n(cs_n),
    .qspi_d(d)
);
```

**Parameters**:
- `MEM_FILE`: Hex file to load (byte-addressed)
- `MEM_OFFSET`: Offset to load file at (default: 0x400000)
- `DEBUG`: Enable debug output (0=off, 1=on)

**Debug Output**:
```
[FLASH] CS# asserted
[FLASH] Command: 0xEB (QIOR)
[FLASH] Address: 0x400000
[FLASH] Mode: 0xA0 (continuous read)
[FLASH] Data nibble: 0x7
```

### s25fl128s.v
Full vendor behavioral model from Spansion/Cypress.

**Features**:
- Complete functional model
- All timing parameters
- All commands supported
- Industry-standard model

**Source**: Spansion/Cypress S25FL128S datasheet
**Size**: ~330KB (comprehensive)
**Usage**: For vendor model validation and compliance testing

**Note**: Requires vendor model file, not included in repository.

## UART Model

### uart_wb_model.sv
Fast UART Wishbone model for rapid simulation.

**Features**:
- Bypasses bit-level serialization
- Instant character transmission
- 100x+ faster than real UART timing
- Wishbone B4 interface

**Usage**:
```systemverilog
uart_wb_model #(
    .UART_BASE_ADDR(32'h40000000)
) uart_model (
    .wb_clk(clk),
    .wb_rst(rst),
    .wb_adr(wb_adr),
    .wb_dat_i(wb_dat_w),
    .wb_dat_o(wb_dat_r),
    .wb_we(wb_we),
    .wb_cyc(wb_cyc),
    .wb_stb(wb_stb),
    .wb_ack(wb_ack),
    .uart_tx_char(tx_char),
    .uart_tx_valid(tx_valid),
    .uart_rx_char(rx_char),
    .uart_rx_valid(rx_valid)
);
```

**When to Use**:
- Development and debugging (fast iteration)
- Testing CPU/firmware logic
- Long-running simulations

**When Not to Use**:
- UART timing verification
- Baud rate testing
- Final hardware validation

## Model Selection Guide

| Test Type | Flash Model | UART Model | Rationale |
|-----------|-------------|------------|-----------|
| Unit test | s25fl_simple | Real UART | Accurate peripheral behavior |
| SoC XIP test | s25fl_simple | Fast model | Quick iteration during development |
| Final verification | s25fl_simple | Real UART | Accurate end-to-end timing |
| Vendor validation | s25fl128s | Real UART | Maximum accuracy |

## Model Tests

The `tests/` subdirectory contains verification tests for the models themselves:

- **test_flash_file_load.sv** - Verify s25fl_simple file loading
- **servant_zvibe_s25fl128s_tb.sv** - SoC test with vendor flash model

See `tests/README.md` for details.

## Adding New Models

When adding a new model:

1. Place source file in this directory
2. Document parameters and usage
3. Create unit test in `tests/`
4. Update this README
5. Update testbench Makefiles to include new model

## References

- S25FL128S Datasheet: Spansion/Cypress
- Flash Controller: `../rtl/qspi/s25fl_xip.v`
- UART Controller: `../rtl/uart_wb.sv`
