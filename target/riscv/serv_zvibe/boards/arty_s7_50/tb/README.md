# Arty S7-50 Verilator Testbenches (`boards/arty_s7_50/tb/`)

SoC-level Verilator simulations for the Arty S7-50 board.  These tests
instantiate the full SoC (SERV CPU + RAM + UART + QSPI flash) against the
S25FL behavioral flash model.  No board hardware or vendor licenses needed.

Unit tests for shared RTL (UART, address mux, QSPI XIP controller) live in
`common/tb/unit/` — not here — because those modules are used by both the
Arty and MAX10 platforms.

---

## Test Suite

### `xip/` — Arty SoC XIP Boot

**Testbench**: `servant_zvibe_xip_tb.sv`
**Flash model**: `s25fl_simple_rw.v` (from `common/tb/models/`)
**Firmware**: `uart_echo_xip_arty.hex` (built from `common/sw/`)

Full SoC simulation: CPU resets directly to XIP at 0x80100100, the QSPI
stall signal holds the bus until the flash controller finishes startup, then
firmware runs and produces UART output.

| Make target | Configuration | Pass condition |
|-------------|--------------|---------------|
| `arty-echo` | Direct XIP (no cache), BURST_WORDS=1 | `Ready>` in `uart_output.txt` |
| `arty-echo-cache` | XIP + 4 KB BRAM cache, BURST_WORDS=4 | `Ready>` in `uart_output.txt` |

```bash
cd boards/arty_s7_50/tb/xip
make -f Makefile.xip arty-echo
make -f Makefile.xip arty-echo-cache
```

The cache test exercises the full `qspi_cache_bram.sv` + `s25fl_xip.sv`
burst path end-to-end in a live SoC context — complementing the isolated
cache unit test in `common/tb/unit/flash/`.

---

### `flash_write/` — Arty QSPI Flash Write Integration

**Testbench**: `servant_zvibe_flash_write_tb.sv`
**DUT**: Full SoC with QSPI write controller at 0x81000000
**Flash model**: `s25fl_simple_rw.v`
**Firmware**: `flash_write_xip_test.hex` (built from `common/sw/`)

4 tests that exercise the `qspi_mux.sv` arbitration between the XIP read
path and the `s25fl_write.sv` write controller:

| # | Test | What it checks |
|---|------|---------------|
| 1 | XIP read activity | CS# toggles during firmware execution — XIP path alive |
| 2 | Write does not reset | Flash program completes without triggering unexpected reset |
| 3 | QSPI signal validity | MOSI/SCK/CS# are never X or Z during operation |
| 4 | XIP idle detection | Write controller waits for XIP to release bus before asserting CS# |

```bash
cd boards/arty_s7_50/tb/flash_write
make
```

---

## Files

| File | Purpose |
|------|---------|
| `xip/Makefile.xip` | Build + run Arty XIP SoC tests |
| `xip/servant_zvibe_xip_tb.sv` | Parametric full-SoC testbench |
| `xip/README.md` | XIP test detail |
| `flash_write/Makefile` | Build + run Arty flash write integration test |
| `flash_write/servant_zvibe_flash_write_tb.sv` | Flash write SoC testbench |
| `flash_write/README.md` | Flash write test detail |

Shared models used by these tests:

| Model | Location |
|-------|----------|
| `s25fl_simple_rw.v` | `common/tb/models/` |
| `uart_wb_model.sv` | `common/tb/models/` |

---

## Path Layout

```
boards/arty_s7_50/
├── rtl/          ← board wrapper RTL
├── sw/           ← board-specific drivers (flash_driver.c, etc.)
├── tb/           ← this directory: SoC-level testbenches
│   ├── xip/
│   └── flash_write/
├── fpga/         ← Vivado build system
└── tests/        ← hardware test scripts (test_uart.py)
```

Firmware is built from `common/sw/` (shared across boards), not from
`boards/arty_s7_50/sw/` which contains only platform-specific drivers.
