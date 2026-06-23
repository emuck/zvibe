# UFM (User Flash Memory) Controllers

This directory contains RTL for interfacing with Intel MAX10 User Flash Memory.

## Components

### wb_to_avalon_mm.v
**Wishbone-to-Avalon-MM Bridge**

Converts Wishbone B4 protocol to Intel Avalon-MM protocol. Used to connect Servant ZVibe SoC to Intel UFM IP core.

**Features**:
- Read and write transaction support
- Address conversion (byte to word addressing)
- Waitrequest handling
- Readdatavalid pipelined read support

**Interface**:
- Wishbone B4 slave (input)
- Avalon-MM master (output)

### max10_ufm_xip.v
**MAX10 UFM Execute-In-Place (XIP) Controller**

Read-only controller for executing code from Intel MAX10 User Flash Memory.

**Features**:
- Wishbone B4 slave interface (read-only)
- Uses Wishbone-to-Avalon-MM bridge
- Address translation for XIP region (0x80000000+)
- Read-only (write operations ignored)

**Usage**:
- Instantiate in SoC for UFM-based XIP execution
- Connect to Intel On-Chip Flash IP core via Avalon-MM

## Comparison with QSPI

| Aspect | QSPI (Arty) | UFM (MAX10) |
|--------|-------------|-------------|
| **Interface** | QSPI pins (external) | Avalon-MM (on-chip) |
| **Protocol** | SPI/QSPI | Avalon-MM |
| **Size** | 16MB external | 172KB on-chip |
| **Controller** | `s25fl_xip.v` | `max10_ufm_xip.v` |
| **Bridge** | N/A (direct) | `wb_to_avalon_mm.v` |

## Memory Map

UFM XIP region maps to CPU address space:
- **CPU Address**: 0x80000000 - 0x8002BFFF (172KB)
- **Physical UFM**: UFM1 (16KB) + UFM2 (16KB) + CFM2 (82KB) + CFM1 (58KB)

## Integration

The UFM XIP controller is integrated into `servant_zvibe.v` via conditional compilation:
- Parameter: `USE_UFM` (1 = UFM, 0 = QSPI)
- When `USE_UFM=1`, instantiate `max10_ufm_xip.v`
- When `USE_UFM=0`, instantiate `s25fl_xip.v` (Arty target)

## References

- Intel MAX10 UFM User Guide: `../../boards/max10_08_eval/max10_UFM.pdf`
- QSPI XIP Controller: `../qspi/s25fl_xip.v`
