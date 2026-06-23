# MAX10 08 Eval Board — UART Hardware Tests

Automated UART testing for ZVibe running on MAX10 08 Eval hardware.

## Prerequisites

- MAX10 programmed with a flash image (run `make program-complete` in `fpga/`)
- Python 3 with `pyserial`:
  ```bash
  pip3 install pyserial
  ```

## Quick Start

```bash
cd tests/
python3 test_uart.py
```

The script:
1. Auto-detects the CP2102 USB-to-UART adapter (`/dev/ttyUSB0`)
2. Sends `~reset~` for a deterministic clean start
3. Replays `plunderedhearts-script.txt` (232 commands)
4. Reports pass/fail + latency statistics

## Options

```bash
python3 test_uart.py                        # Auto-detect port
python3 test_uart.py --port /dev/ttyUSB0    # Override port
python3 test_uart.py --auto-program         # Build + flash game image first, then test
python3 test_uart.py --save-test            # Shorter script that exercises SAVE/RESTORE
```

`--auto-program` rebuilds the firmware+game flash image and programs the MAX10 (CFM +
UFM via `make program-complete`, ~11s) before running the test.  Requires RISC-V
toolchain and Quartus in PATH.  Useful for CI or when you've just modified firmware.

## Test Results (Feb 25, 2026 — 100 MHz, on-chip UFM, no cache)

| Metric | Result |
|--------|--------|
| 232-command Plundered Hearts script | ✅ PASS |
| n | 232 |
| min | 0.287 s |
| median | 0.638 s |
| mean | 0.791 s |
| p95 | 1.549 s |
| max | 3.485 s |
| stdev | 0.402 s |
| >2 s turns | 2.2 % |
| >5 s turns | 0.0 % |

**How measured**: `test_uart.py` records wall-clock time from sending each command to
receiving the next `>` prompt.  Results written to
`plunderedhearts_latencies_<timestamp>.csv` (gitignored).
Latest file: `plunderedhearts_latencies_20260225_110238.csv`

See `boards/arty_s7_50/README.md` for a cross-board comparison.

## Status

- ✅ UART test script — working (verified Feb 2026)
- ✅ Plundered Hearts end-to-end replay — passing
- ✅ Save/restore commands — verified on hardware
