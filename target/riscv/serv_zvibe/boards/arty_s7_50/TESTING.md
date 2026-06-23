# Arty S7-50 — Testing & Validation Status

Last updated: February 2026

## Summary

| Layer | Status | Notes |
|-------|--------|-------|
| Firmware build | Pass | 34KB, all sources compile cleanly |
| Flash image build | Pass | ZVIF header, TOC, game, 237 save slots |
| S25FL R/W model unit test | 6/6 pass | RDSR, WREN, SE, PP, write protection |
| QSPI XIP controller unit test | 4/5 pass | Test 5 hangs (see known limitations) |
| SoC XIP integration test | Pass | Full boot + UART echo banner verified |
| SoC flash write integration test | Pass | Full SoC with write controller; UART verified |
| Hardware: FPGA detected | Pass | XC7S50 visible via openFPGALoader |
| Hardware: UART boot | Pass | ZVibe banner + Plundered Hearts launch confirmed |
| Hardware: Game execution | Pass | Plundered Hearts running on hardware |
| Hardware: Game script (232 commands) | Pass | test_uart.py 232/232, exit 0, Jun 2026 |
| Hardware: Save/restore | Pass | Verified Feb 2026 |
| Hardware: Restaurant (212 commands) | Pass | test_uart.py 212/212, Jun 2026 |
| Hardware: Seastalker (217 commands) | Pass | test_uart.py 217/217, Jun 2026 |

---

## Simulation Results (February 2026)

Simulation uses the shared `common/tb/` infrastructure with Spansion S25FL behavioral
models.  All simulations run with **Verilator** except the QSPI XIP controller unit
test which requires **Vivado xsim** (tri-state ports incompatible with Verilator).

### S25FL R/W Model Unit Test

**Tool**: Verilator
**Location**: `common/tb/unit/flash_rw/`
**Command**: `make`

```
 Test Summary
================================================================================
Total:  6 tests
Passed: 6 tests
Failed: 0 tests
```

Tests cover: RDSR (Read Status Register), WREN (Write Enable), write protection
rejection without WREN, sector erase (SE), page program (PP), and polling-based
completion detection.

---

### QSPI XIP Controller Unit Test

**Tool**: Vivado xsim
**Location**: `common/tb/unit/flash/`
**Command**: `make -f Makefile.xip_wb xsim`

| Test | Result |
|------|--------|
| 1: Wishbone single read | PASS |
| 2: STALL generation | PASS |
| 3: Address translation | PASS |
| 4: Back-to-back reads (with gaps) | PASS |
| 5: True back-to-back reads (no gaps) | HANG |

**Known limitation**: Test 5 waits for a second ACK that never arrives because
`s25fl_xip.v` does not support zero-gap consecutive reads.  This is benign in
practice — SERV's bit-serial instruction fetch naturally has gap cycles between
consecutive accesses.  Tests 1–4 cover all functionally relevant cases.

---

### Arty SoC XIP Integration Test

**Tool**: Verilator
**Location**: `boards/arty_s7_50/tb/xip/`
**Command**: `make -f Makefile.xip arty-echo`

Result: Full SoC boots via XIP direct boot (reset_pc=0x80100100), CPU
fetches instructions from `s25fl_simple` behavioral model, and UART outputs:

```
==============================
UART Echo Test (XIP)
==============================
Ready>
```

Simulation completes after 500M cycles (UART idle timeout). QSPI activity
confirmed (4011 QSPI toggles at 1M cycles, tracking instruction fetches).

---

### SoC Flash Write Integration Test

**Tool**: Verilator
**Location**: `boards/arty_s7_50/tb/flash_write/`
**Command**: `make build && make run` (from `build/` dir with hex files)

Result: Full SoC with `s25fl_xip.v` + `s25fl_write.v` + `qspi_mux.v` all
instantiated.  Boot stub loads, CPU jumps to XIP flash, UART outputs the echo
banner.  Confirms QSPI mux arbitration between XIP reads and write controller
does not break CPU execution.

```
UART Echo Test (XIP)
==============================
Ready>
```

QSPI toggles: 249,011 at 50M cycles (continuous XIP fetches after banner).

---

## Known Limitations

1. **Test 5 (true back-to-back reads)**: `s25fl_xip.v` requires at least one
   idle cycle between consecutive Wishbone requests.  SERV's fetch pattern
   satisfies this naturally.

2. **Board-local testbenches** are in `boards/arty_s7_50/tb/xip/` and
   `boards/arty_s7_50/tb/flash_write/`; the `common/tb/unit/` suite covers
   shared peripherals (UART, mux, QSPI).

3. **No Questa/vendor-model simulation**: Unlike MAX10 (which has Intel UFM IP
   models for Questa), Arty has no equivalent Xilinx IP simulation.  The
   Spansion behavioral model (`s25fl_simple.v`) covers functional XIP testing.

---

## Hardware Testing

### Prerequisites

- Arty S7-50 connected via USB
- FTDI FT2232H detected (`openFPGALoader -b arty_s7_50 --detect`)
- UART port: `/dev/ttyUSB1  # or ttyUSB2 depending on other devices` (second FT2232H channel)
- Vivado in PATH for bitstream build

### Build and Program

```bash
# Step 1: Build firmware + flash image
cd boards/arty_s7_50/sw
make flash-test              # Default: Plundered Hearts

# Step 2: Build FPGA bitstream (~10-15 min first time)
cd ../fpga
make build

# Step 3: Program everything
make program-complete        # Bitstream → 0x000000; flash image → 0x100000
```

### Test UART

```bash
# Manual
picocom -b 115200 /dev/ttyUSB1  # or ttyUSB2 depending on other devices

# Automated
cd tests && python3 test_uart.py  # auto-detects FT2232H port
```

Expected boot output:
```
ZVibe Game System for RISC-V
Initializing save system...
Loading game library from flash...
Found 1 game
Auto-launching game...

Plundered Hearts
[...]
```

### Current Hardware Status

| Test | Date | Result |
|------|------|--------|
| FPGA detection (XC7S50) | Feb 2026 | Verified |
| UART boot + ZVibe banner | Feb 2026 | Verified (`/dev/ttyUSB2`) |
| Timing closure @ 166MHz | Feb 14 2026 | WNS=+0.288ns (false path on MMCM reset) |
| Plundered Hearts (232 cmds) | Jun 2026 | Pass — median 0.421 s/cmd, p95 1.039 s |
| Restaurant (212 cmds) | Jun 2026 | Pass — median 0.464 s/cmd |
| Seastalker (217 cmds) | Jun 2026 | Pass — median 0.274 s/cmd |
| Save/restore | Feb 2026 | Verified |

---

## Comparison with MAX10

| Aspect | MAX10 | Arty |
|--------|-------|------|
| Simulation | Verilator + Questa (vendor UFM) | Verilator + xsim |
| Hardware validated | Jun 2026 | Jun 2026 |
| Save slot size | 2KB (UFM page) | 64KB (QSPI sector) |
| Save slot count | 5 (with Zork I) | 237 (with Plundered Hearts) |
| Boot strategy | XIP direct | Boot stub → XIP |
| Flash capacity | 172KB | 16MB |
