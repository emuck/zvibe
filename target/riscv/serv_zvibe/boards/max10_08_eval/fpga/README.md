# MAX10 FPGA Build

Quartus build system for the MAX10 08 Evaluation Board.

## Prerequisites

- Quartus Prime (Lite or Standard) in PATH
- UFM IP core generated (one-time, see below)

## Quick Start

```bash
cd boards/max10_08_eval/fpga

# One-time: generate UFM IP core
make generate-ip

# Build bitstream (~15 minutes)
make build

# Build firmware + program everything (~11 seconds)
make program-complete
```

## UFM IP Core

The UFM IP core must be generated before building. Run once:

```bash
make generate-ip
```

See `UFM_IP_GENERATION.md` for details and troubleshooting.

## Build Targets

| Target | Description |
|--------|-------------|
| `make build` | Build FPGA bitstream |
| `make program-complete` | Build firmware + program CFM + UFM + SRAM |
| `make program` | Program SRAM only (fast, temporary) |
| `make program-flash` | Program CFM only |
| `make generate-ip` | Generate UFM IP core (one-time) |
| `make reports` | View timing and utilization reports |
| `make clean` | Remove build artifacts |

## Clock

- System clock: 100MHz via PLL (50MHz external CLK0p, PIN_27)
- Heartbeat clock: 116MHz internal oscillator (LED[4] blink only)
- PLL reset: PIN_124 (DIP Switch 1, normally OFF)

## Timing Results

Worst-case slack: **+0.353ns** (Slow 85C corner). Zero violations.

## Files

- `build.tcl` — Quartus synthesis script
- `Makefile` — Build and programming automation
- `max10_08_eval.sdc` — Timing constraints
- `generate_ufm_ip.tcl` — UFM IP generation script
- `UFM_IP_GENERATION.md` — IP core setup guide

## Troubleshooting

**Quartus not found:** Add Quartus `bin/` to PATH, then `source ~/.bashrc`.

**UFM IP missing:** Run `make generate-ip` or see `UFM_IP_GENERATION.md`.

**Build errors:** Check `make show-deps` to verify all RTL files are present.
