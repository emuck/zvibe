# Arty S7-50 — UART Hardware Tests

Automated UART testing for ZVibe running on Arty S7-50 hardware.

## Prerequisites

- Arty S7-50 programmed with a flash image (run `make program-complete` in `fpga/`)
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
1. Auto-detects the FTDI FT2232H serial port (highest-numbered `/dev/ttyUSB*`)
2. Sends `~reset~` for a deterministic clean start
3. Replays the game's walkthrough script
4. Reports pass/fail + latency statistics

## Supported Games

| Game | Script | Commands | Ships with repo |
|------|--------|----------|-----------------|
| `restaurant` (default) | `restaurant-script.txt` | 212 | Yes |
| `plunderedhearts` | `plunderedhearts-script.txt` | 232 | No (auto-download) |
| `seastalker` | `seastalker-script.txt` | 217 | No (auto-download) |

## Options

```bash
python3 test_uart.py                                    # Restaurant (default), auto-detect port
python3 test_uart.py --port /dev/ttyUSB1                # Override port
python3 test_uart.py --game plunderedhearts             # Test Plundered Hearts
python3 test_uart.py --game seastalker                  # Test Seastalker
python3 test_uart.py --auto-program                     # Build + flash game image first, then test
python3 test_uart.py --auto-program --game seastalker   # Flash + test a specific game
python3 test_uart.py --save-test                        # Shorter script that exercises SAVE/RESTORE
```

`--auto-program` rebuilds the firmware+game flash image, programs the user flash region
(openFPGALoader, ~15s), then reloads the ZVibe bitstream into FPGA SRAM before testing.
The reload step is necessary because `openFPGALoader -f` leaves a `spiOverJtag` passthrough
bitstream in FPGA SRAM after the flash write; without it the test hangs.  The FPGA
bitstream stays unchanged — no Vivado required.  Useful for CI or when you've just
modified firmware.

## Serial Port Notes

The Arty FT2232H has two channels; UART is on the **second** channel.

```bash
ls /dev/ttyUSB*   # List available ports; pick the highest-numbered one
```

Port number varies depending on other connected USB devices. Use `--port` to override.

## Script Format

Test scripts are plain text files with one command per line. Scripts support
two directives for handling random game elements:

- `@capture <name> /<regex>/` — extract a value from the previous command's output
- `{name}` in a command — substitute a previously captured value
- `#` lines — comments (skipped)
- `.` lines — Z-machine no-ops used as turn separators

Scripts without directives (Plundered Hearts, Seastalker) work as plain command lists.
See `games/scripts/restaurant-script.txt` for an example using `@capture`.

## Expected Output

```
=== Arty S7-50 — ZVibe UART Test ===
Opening /dev/ttyUSB1 @ 115200 baud
Flushing and sending ~reset~...
[ok] Game reset complete, starting script
Progress: |████████████████████████████████████████| 100.0% (232/232)

Latency Summary (seconds)
n=232
min_s=0.219
median_s=0.421
mean_s=0.524
p95_s=1.040
max_s=2.287
stdev_s=0.276
pct_over_2s=0.4
pct_over_5s=0.0

PASSED: plunderedhearts completed successfully!
```

## Test Results

### Plundered Hearts (Jun 20, 2026 — 4KB BRAM XIP cache, CLK_DIV=2)

| Metric | Result |
|--------|--------|
| 232-command Plundered Hearts script | PASS |
| n | 232 |
| min | 0.219 s |
| median | 0.421 s |
| mean | 0.524 s |
| p95 | 1.040 s |
| max | 2.287 s |
| stdev | 0.276 s |
| >2 s turns | 0.4 % |
| >5 s turns | 0.0 % |

### Restaurant (Jun 20, 2026)

| Metric | Result |
|--------|--------|
| 212-command Restaurant script | PASS (42/42) |
| n | 210 |
| median | 0.464 s |
| p95 | 3.069 s |
| max | 21.1 s |

Restaurant is compiled with PunyInform (Inform 6) which generates less optimized
Z-machine bytecode than Infocom's hand-tuned ZIL compiler. The tail latencies
(p95, max) reflect heavier per-turn daemon processing.

### Seastalker (Jun 20, 2026)

| Metric | Result |
|--------|--------|
| 217-command Seastalker script | PASS (100/100) |
| n | 217 |
| median | 0.274 s |
| p95 | 2.166 s |
| max | 2.381 s |

**How measured**: `test_uart.py` records wall-clock time from sending each command to
receiving the next `>` prompt.  Results written to
`<game>_latencies_<timestamp>.csv` (gitignored).

**Previous Plundered Hearts result (Feb 25, 2026 — same cache)**: median 0.614 s, p95 1.483 s.

## Output Files

Test runs produce two files per game (gitignored):

- `<game>_uart_test_output.log` — full UART capture
- `<game>_latencies_<timestamp>.csv` — per-command latency data
