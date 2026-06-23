# MAX10 Testing and Validation

Validation status for the MAX10 08 Evaluation Board platform as of February 2026.

**Note (Feb 13, 2026):** Simulation infrastructure (`common/tb/`, `boards/max10_08_eval/tb/`,
`boards/max10_08_eval/sim/`) was accidentally deleted in commit `49716ac` (platform-agnostic
save system refactor) and restored in the subsequent commit.

## Status Summary

| Test Category | Tool | Status |
|--------------|------|--------|
| Unit Tests (UART) | Verilator | PASS (6/6) |
| Unit Tests (Mux) | Verilator | PASS (59/59) |
| Behavioral Simulation | Verilator | PASS |
| Vendor UFM Simulation | Questa | PASS |
| FPGA Build + Timing | Quartus | PASS (setup +0.946 ns, hold +0.274 ns) |
| Flash Image Generation | Python | PASS |
| Hardware Programming | quartus_pgm | PASS |
| Hardware UART + XIP | Manual | VERIFIED Feb 2026 |
| UFM Write (save/restore) | Hardware | VERIFIED Feb 12, 2026 |

## Unit Tests

### UART Controller (`common/tb/unit/uart/`)
6/6 tests passing:
- Basic TX/RX functionality
- TX/RX FIFO overflow error flags
- Multiple simultaneous error flags
- RX frame error and overrun flags

```bash
make -f Makefile.uart run
```

### Memory Mux (`common/tb/unit/mux/`)
59/59 tests passing across two modules:
- `servant_mem_mux`: 20/20 (RAM/flash/UFM decode, pipelining, ack passthrough)
- `servant_zvibe_mux`: 39/39 (UART/Timer/GPIO/Flash decode, all peripherals)

```bash
make test
```

## Behavioral Simulation (Verilator)

Testbench: `boards/max10_08_eval/tb/servant_zvibe_max10_08_eval_xip_tb.sv`
UFM model: `boards/max10_08_eval/tb/ufm_sim.sv` (behavioral)

Tests:
- PLL lock detection
- Reset release sequencing
- Boot from XIP at 0x80000100
- UART transmission

Status: ALL TESTS PASSED

## Vendor UFM Simulation (Questa)

Testbenches: `boards/max10_08_eval/sim/max10_xip_tb.sv` (echo), `boards/max10_08_eval/sim/servant_zvibe_ufm_write_tb.sv` (write)
UFM model: Intel `fiftyfivenm_unvm_encrypted` (vendor primitive)

Tests:
- XIP read at 0x80000000 (ZVIF magic: 0x5A564946)
- XIP read at 0x80000004 (version: 0x00000001)
- Wishbone to Avalon-MM bridge timing

Status: ALL TESTS PASSED

**Setup note:** Vendor IP requires init file `altera_onchip_flash.dat` and
`INIT_FILENAME_SIM` set in `fpga/ip/ufm/simulation/ufm.v`. Generate .dat with:
```bash
riscv64-unknown-elf-objcopy -O verilog --verilog-data-width=4 \
  --change-addresses=-0x80000000 firmware.elf altera_onchip_flash.dat
```

## FPGA Build

- Bitstream: `output_files/servant_zvibe_max10_08_eval_xip.sof`
- Timing: Setup **+0.946 ns**, Hold **+0.274 ns** @ 100 MHz (Slow 85C corner), FMAX 118.65 MHz
- Logic: ~2,800 / 8,000 LEs (35%), 31/46 M9K (67%), 1/1 PLL
- Build time: ~2 minutes (incremental)
- PLL: direct `altpll` instantiation (no wizard wrapper); SDC clock `pll_inst|auto_generated|pll1|clk[0]`

```bash
make build && make reports
```

## Flash Image Generation

Output: `common/sw/max10_flash_test.bin` (172KB)

Validated fields:
- ZVIF magic: 0x5A564946
- ZVGM magic: 0x4D47565A
- Firmware at offset 0x000100
- Game data follows ZVGM TOC

```bash
cd common/sw && make max10-flash-test
```

## Hardware Verification

### UART + XIP Boot (Verified)
- Device: CP2102 on `/dev/ttyUSB0`
- Baud rate: 115200
- ZVibe boot banner appears within 1 second of reset
- Game (Zork I, 83KB) auto-launches (single-game mode)
- UART I/O fully functional throughout game

### Automated End-to-End Test (Verified Jun 20, 2026)
- Restaurant: 212 commands, PASSED — median 0.565 s/cmd, max 21.4 s
- Plundered Hearts: 232 commands, PASSED — median 0.512 s/cmd, max 2.783 s
- Seastalker: 217 commands, PASSED — median 0.330 s/cmd, max 2.911 s
- Run: `python3 tests/test_uart.py [--game plunderedhearts|seastalker]`

### UFM Write / Save System (Verified Feb 12, 2026)
- SAVE command writes game state to UFM
- RESTORE command reads it back correctly
- Wear-leveling slot rotation confirmed
- Round-trip verified: save → power cycle → restore
- Slot count: 5 (dynamic; computed by build_flash.py from remaining UFM after firmware + game)
- Save slot size: 2KB per slot; firmware size ~34.5KB

### Programming
- USB-BlasterII auto-detected
- CFM + UFM programmed in ~11 seconds via `make program-complete`

## Run Full Flow

```bash
# Unit tests
cd common/tb/unit
make -C uart/ -f Makefile.uart run
make -C mux/ test

# Build and program
cd boards/max10_08_eval/fpga
make build
make program-complete

# Hardware test (assumes board already programmed)
cd ../tests
python3 test_uart.py

# Or: fully automated (downloads game if missing, builds flash, programs, tests)
python3 test_uart.py --auto-program
```
