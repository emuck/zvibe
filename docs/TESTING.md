# Testing

Test structure and procedures.

## Test Suite Location

Tests are located in `target/console/tests/`.

## Test Categories

### Unit Tests

**File:** `test_split_memory.c`

Tests the split memory abstraction layer:
- Memory initialization
- Address validation
- Byte and word operations
- Stack operations
- Pointer access
- Boundary conditions

**Run:**
```bash
cd target/console/tests
make test-unit
```

**Expected:** 119 tests pass.

### Integration Tests

Tests run complete games with scripted input.

**Czech Test Suite:**
- File: `games/catalog/czech.z3` (public domain)
- Tests Z-machine opcode implementation
- 368 individual tests

**Game Completion Tests (individual targets):**
- Restaurant: `make test-restaurant` — game ships with the repo, runs walkthrough + stress test
- Plundered Hearts: `make test-plundered` — downloads game file, then runs full playthrough
- Seastalker: `make test-seastalker` — downloads game file, then runs full playthrough

**Run:**
```bash
cd target/console/tests
make test-integration
```

## Running All Tests

```bash
cd target/console/tests
make test
```

Or explicitly:
```bash
make test-unit test-integration
```

## Test Scripts

| Script | Purpose |
|--------|---------|
| `target/console/tests/test_czech.sh` | Z-machine conformance — runs as part of `make test-integration` |
| `target/console/tests/test_script.sh` | Game-completion runner (wraps `test_script.py`) — restaurant in default run; plundered/seastalker via individual targets |
| `games/scripts/restaurant-script.txt` | Restaurant walkthrough command input |
| `games/scripts/restaurant-text-check.txt` | Restaurant text-regression markers |
| `games/scripts/restaurant-stress-test.txt` | Restaurant stress-test command input |
| `games/scripts/plunderedhearts-script.txt` | Plundered Hearts walkthrough command input |
| `games/scripts/seastalker-script.txt` | Seastalker walkthrough command input |

## Script Format

Test scripts are plain text files with one command per line. The format supports
directives for handling random game elements (e.g., randomized puzzle codes):

| Syntax | Meaning |
|--------|---------|
| `examine panel` | Plain command — sent to the game |
| `.` | Z-machine no-op — used as a turn separator |
| `@capture name /regex/` | Extract a value from the previous command's output |
| `{name}` | Substitute a previously captured value into a command |
| `# comment` | Comment — skipped by the test harness |

Example from `restaurant-script.txt`:
```
examine panel
@capture panel_code /code:\s*(\d+)/
.
enter {panel_code}
```

Scripts without directives (Plundered Hearts, Seastalker) work as plain command
lists and are fully backward compatible. The restaurant walkthrough uses
`@capture` to handle the randomized panel code, allowing it to run with any
random seed — no deterministic seed required.

## Adding Regression Tests

### Adding a Unit Test

1. Add test function to `test_split_memory.c`:
   ```c
   static void test_new_feature(void) {
       // Setup
       // Action
       // Assert with TEST_ASSERT()
   }
   ```

2. Call from `main()`:
   ```c
   test_new_feature();
   ```


## Test Output

Test results are written to:
- `czech_test_output.log`
- `restaurant_test_output.log`
- `plunderedhearts_test_output.log`
- `seastalker_test_output.log`

## Hardware Tests

### SAM E51

```bash
cd target/same51
make test
```

Tests SmartEEPROM save/restore functionality.

### RISC-V FPGA — MAX10 08 Eval Board

```bash
cd target/riscv/serv_zvibe/boards/max10_08_eval/tests
python3 test_uart.py
```

Replays a scripted game session via serial. Auto-detects CP2102 on `/dev/ttyUSB0`.

```bash
# Questa simulation (validate before hardware programming)
cd target/riscv/serv_zvibe/boards/max10_08_eval/sim
make -f Makefile.vsim questa-batch   # XIP boot + UART echo test
```

### RISC-V FPGA — Arty S7-50

```bash
cd target/riscv/serv_zvibe/boards/arty_s7_50/tests
python3 test_uart.py
```

Tests UART communication with hardware.

## Simulation Tests

RISC-V target RTL tests run through a tiered regression runner. All Tier 1 tests use Verilator and require no vendor tools.

### Tier 1 — Verilator (no vendor tools required)

```bash
make sim-tier1          # From repo root — runs all 13 Verilator tests (~2 min)
```

Or individually:

```bash
cd target/riscv/serv_zvibe
./run_regression.sh --tier1
```

Tests included in Tier 1:

| Test | What it exercises |
|------|-------------------|
| `uart_wb_tb` | UART Wishbone controller: TX/RX, FIFO overflow, error flags |
| `servant_mem_mux_tb` | RAM/flash address decode, pipelined ACK |
| `servant_zvibe_mux_tb` | Peripheral decode, case statement, pipelined ACK |
| `s25fl_xip_wb_tb` | QSPI XIP controller: address translation, burst, back-to-back reads |
| `s25fl_xip_burst_tb` | QSPI burst read, cache-line fill, contiguous access |
| `qspi_cache_bram_tb` | 4KB BRAM cache: hit/miss, LRU eviction, write-through |
| `ufm_model_unit_tb` | MAX10 UFM behavioral model: program, erase, status |
| `max10_ufm_unified_tb` | MAX10 Wishbone→Avalon-MM bridge: all transaction types |
| `max10_board_xip_tb` | MAX10 board-level: PLL lock, reset sequencing, boot to UART output |
| `arty_xip_echo` | Arty S7-50 SoC XIP boot path, UART echo (uncached) |
| `arty_xip_echo_cache` | Arty S7-50 SoC XIP boot path, UART echo (4KB BRAM cache) |
| `s25fl_flash_rw_tb` | S25FL QSPI flash read/write/erase model |
| `arty_soc_flash_write_tb` | Arty SoC flash write controller arbitration |

**Pass/fail**: Each test emits `PASS: <name>` or `FAIL: <name> (N errors)` on stdout. The regression script greps for `^PASS:` and exits 0 if all pass.

Logs are captured to `target/riscv/serv_zvibe/regression_logs/`.

### Tier 2 — Questa (vendor UFM model; requires `vsim` license)

```bash
make sim-questa         # From repo root
```

Tests included:

| Test | What it exercises |
|------|-------------------|
| `max10_xip_questa_uart_echo` | Full XIP boot path with Intel vendor UFM model; verifies UART banner output |
| `max10_xip_questa_ufm_write` | Runtime UFM erase/page-write/verify against vendor model |

Prerequisites: Questa FSE on PATH; UFM vendor IP generated (`make generate-ip` in `boards/max10_08_eval/fpga/`); `fpga/ip/ufm/simulation/ufm.v` has `INIT_FILENAME_SIM` set. See `boards/max10_08_eval/sim/QUESTA_UFM_INIT_GUIDE.md`.

### Tier 2 — Vivado xsim (placeholder)

```bash
make sim-xsim           # Currently a no-op (infrastructure not yet implemented)
```

See [`target/riscv/serv_zvibe/common/tb/README.md`](../target/riscv/serv_zvibe/common/tb/README.md) for the full testbench inventory.
