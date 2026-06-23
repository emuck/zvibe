# Testbenches — serv_zvibe

RTL simulation testbenches for the serv_zvibe RISC-V SoC.

---

## Verification Architecture

Tests are organised in four layers of increasing scope and cost.

```
Layer 4 — Vendor-model (Tier 2, Questa)
  └─ Real Intel UFM IP model; highest fidelity; ~15 min each
       boards/max10_08_eval/sim/

Layer 3 — SoC integration (Tier 1, Verilator)
  └─ Full CPU + memory + peripherals; firmware boots to completion
       boards/max10_08_eval/tb/   (MAX10 XIP boot)
       boards/arty_s7_50/tb/      (Arty XIP boot, Arty flash write)

Layer 2 — Unit tests (Tier 1, Verilator)
  └─ Single RTL module in isolation; no CPU; direct signal drive
       common/tb/unit/            (UART, mux, QSPI flash, cache)
       boards/max10_08_eval/tb/   (UFM model, UFM bridge)

Layer 1 — Simulation models (exercised by layers above)
  └─ Behavioral stand-ins for vendor IP or hardware
       common/tb/models/          (UART, S25FL flash)
       boards/max10_08_eval/tb/   (ufm_sim.sv, altpll_model.sv)
```

**Why this structure:**

- Unit tests catch RTL bugs in minutes.  Because each test drives one module
  directly, failures are easy to localise.
- SoC tests catch integration bugs: address-decode errors, stall/ack mis-timing
  across module boundaries, firmware assumptions about hardware.
- Vendor-model tests catch anything that the behavioral models approximate
  incorrectly (UFM timing, read-latency pipeline).

**Run order convention:** always pass Tier 1 before committing.  Tier 2 runs
in CI or before a hardware build.

```bash
./run_regression.sh --tier1          # ~30 s — always green before a PR
./run_regression.sh --tier2-questa   # ~30 min — requires vsim + generated IP
./run_regression.sh --all
```

Each test prints `PASS: <name>` or `FAIL: <name>` on stdout; the regression
script greps for the `^PASS:` prefix and captures logs to `regression_logs/`.

---

## Naming Conventions

Most testbenches follow the pattern `<dut_module>_tb.sv` with a matching
module name inside.  Exceptions worth noting:

- **`ufm_model_unit_tb`** — tests the *behavioral model* (`ufm_sim.sv`), not
  a synthesisable DUT.  The "model" in the name is intentional; the "unit" is
  redundant but harmless.
- **`arty_xip_echo` / `arty_xip_echo_cache`** — SoC-level tests named after
  the test scenario rather than the DUT module; no `_tb` suffix because the
  test is driven by the Makefile, not a single top-level module.
- Everything else follows `<dut>_tb`.

---

## Tier 1 — Verilator Unit Tests

### `unit/uart/` — UART Wishbone controller

**Testbench**: `uart_wb_tb.sv`
**DUT**: `uart_wb.sv` + `uart_tx.sv` + `uart_rx.sv` + `fifo_sync.sv`
**Run**: `make -f Makefile.uart run`

6 tests: basic TX/RX loopback, TX FIFO overflow flag, RX FIFO overflow flag,
sticky error-flag clear-on-read, frame error detection, overrun error
detection.

See [`unit/uart/README.md`](unit/uart/README.md).

---

### `unit/mux/` — Address-decode mux modules

**Testbenches**: `servant_mem_mux_tb.sv`, `servant_zvibe_mux_tb.sv`
**Run**: `make` (runs both in sequence)

| Testbench | DUT | Checks | Coverage |
|-----------|-----|--------|----------|
| `servant_mem_mux_tb` | `servant_mem_mux.sv` | 48 | RAM vs flash decode; pipelined ACK; back-to-back; boundary addresses (0x7FFFFFFF/0x80000000); WE propagation |
| `servant_zvibe_mux_tb` | `servant_zvibe_mux.sv` | 11 | Peripheral select (UART/Timer/GPIO/Flash); pipelined ACK; combinatorial data routing |

---

### `unit/flash/` — QSPI XIP controller and BRAM cache

**DUT**: `qspi/s25fl_xip.sv`, `qspi/qspi_cache_bram.sv`
**Flash model**: `s25fl_simple.v` (read-only)
**Run**: see targets below

| Make target | Testbench | Checks | Coverage |
|-------------|-----------|--------|----------|
| `make -f Makefile.xip_wb run` | `s25fl_xip_wb_tb.sv` | 7 | Single-word WB read; address translation (0x80xxxxxx → physical); STALL generation; back-to-back reads (CYC-toggle); startup busy; unaligned word-alignment |
| `make -f Makefile.xip_wb burst` | `s25fl_xip_burst_tb.sv` | 4 words | BURST_WORDS=4 FSM path; 128-bit fetch; little-endian byte reordering |
| `make -f Makefile.xip_wb cache-bram` | `qspi_cache_bram_tb.sv` | 22 | Cold miss; hit; eviction (LRU); stall propagation; reset; multi-line fill |

See [`unit/flash/README.md`](unit/flash/README.md).

---

### `unit/flash_rw/` — QSPI read/write flash model

**Testbench**: `s25fl_rw_tb.sv`
**DUT**: `s25fl_simple_rw.v` (behavioral S25FL with erase/program)
**Run**: `make`

6 sub-tests at the SPI protocol level: RDSR, WREN (sets WEL), sector erase
(64 KB → 0xFF), page program (256 bytes), status polling, and write-without-
WREN rejection.

---

### MAX10 UFM tests — `boards/max10_08_eval/tb/`

| Make target | Testbench | DUT | Tests | Coverage |
|-------------|-----------|-----|-------|----------|
| `make -f Makefile.ufm_unit_test test-ufm` | `ufm_model_unit_tb.sv` | `ufm_sim.sv` (model) | 5 | Model self-test: STATUS read, erased-state read, CONTROL write (safety), page program, page erase |
| `make -f Makefile.ufm_unit_test test-bridge` | `max10_ufm_unified_tb.sv` | `max10_ufm_unified.sv` | 7 (8 checks) | XIP read; CSR STATUS read; WRITE_ADDR round-trip; UFM program+verify; UFM erase+verify; address boundary decode (XIP vs CSR); WRITE_DATA default |

See [`boards/max10_08_eval/tb/README.md`](../../boards/max10_08_eval/tb/README.md).

---

## Tier 1 — Verilator SoC Integration Tests

Board-specific SoC testbenches live under their respective board directory.
Only genuinely shared infrastructure (unit tests, models) lives here in
`common/tb/`.

### MAX10 board SoC tests — `boards/max10_08_eval/tb/`

See [`boards/max10_08_eval/tb/README.md`](../../boards/max10_08_eval/tb/README.md).

Covers: UFM model self-test, UFM WB bridge unit test, full SoC XIP boot.

### Arty S7-50 board SoC tests — `boards/arty_s7_50/tb/`

See [`boards/arty_s7_50/tb/README.md`](../../boards/arty_s7_50/tb/README.md).

Covers: XIP direct boot (with and without BRAM cache), QSPI flash write +
mux arbitration.

---

## Tier 2 — Questa Vendor-Model Tests

Uses the Intel-provided simulation model for the UFM IP — the most faithful
representation of the hardware available short of real silicon.

**Prerequisites**: `vsim` in PATH; UFM IP simulation files generated via
`cd boards/max10_08_eval/fpga && make generate-ip`.
See [`boards/max10_08_eval/sim/QUESTA_UFM_INIT_GUIDE.md`](../../boards/max10_08_eval/sim/QUESTA_UFM_INIT_GUIDE.md).

| Test | Make target | Runtime | Validates |
|------|------------|---------|-----------|
| `max10_xip_questa_uart_echo` | `uart-echo` | ~14 min | Full XIP boot path: PLL → reset → UFM fetch → UART banner |
| `max10_xip_questa_ufm_write` | `ufm-write` | ~15 min | Runtime UFM erase/program/read-back; 5 sub-tests |

Run from `boards/max10_08_eval/sim/`:
```bash
make -f Makefile.vsim uart-echo
make -f Makefile.vsim ufm-write
```

---

## Simulation Models

| File | Location | Used by |
|------|----------|---------|
| `uart_wb_model.sv` | `models/` | SoC XIP tests — fast WB UART, writes to file |
| `s25fl_simple.v` | `models/` | `unit/flash/` QSPI XIP unit tests (read-only) |
| `s25fl_simple_rw.v` | `models/` | `unit/flash_rw/`, `soc/flash_write/`, `soc/xip/` |
| `ufm_sim.sv` | `boards/max10_08_eval/tb/` | MAX10 unit tests; MAX10 board XIP TB |
| `altpll_model.sv` | `boards/max10_08_eval/tb/` | MAX10 board XIP TB (50 → 100 MHz) |

---

## Full Test Registry (Tier 1)

| # | Regression label | Layer | DUT | Tests/Checks | Runtime |
|---|-----------------|-------|-----|-------------|---------|
| 1 | `uart_wb_tb` | Unit | UART WB controller | 6 | < 1 s |
| 2 | `servant_mem_mux_tb` | Unit | RAM/flash mux | 48 checks | < 1 s |
| 3 | `servant_zvibe_mux_tb` | Unit | Peripheral mux | 11 checks | < 1 s |
| 4 | `s25fl_xip_wb_tb` | Unit | QSPI XIP (single-word) | 7 | < 1 s |
| 5 | `s25fl_xip_burst_tb` | Unit | QSPI XIP (burst×4) | 4 words verified | < 1 s |
| 6 | `qspi_cache_bram_tb` | Unit | BRAM cache | 22 | < 1 s |
| 7 | `ufm_model_unit_tb` | Unit | UFM behavioral model | 5 | < 1 s |
| 8 | `max10_ufm_unified_tb` | Unit | UFM WB bridge | 7 (8 checks) | < 1 s |
| 9 | `max10_board_xip_tb` | SoC (MAX10) | MAX10 full SoC | boot + 7 UART chars | ~5 s |
| 10 | `arty_xip_echo` | SoC (Arty) | Arty SoC (no cache) | UART echo | ~15 s |
| 11 | `arty_xip_echo_cache` | SoC (Arty) | Arty SoC (BRAM cache) | UART echo | ~15 s |
| 12 | `s25fl_flash_rw_tb` | Unit | S25FL R/W model | 6 | < 1 s |
| 13 | `arty_soc_flash_write_tb` | SoC (Arty) | Arty flash write | 4 | ~10 s |

**Total Tier 1: 13 tests, ~100 individual checks, ~30 s wall time.**

---

## Coverage Summary

| Subsystem | Unit coverage | SoC/integration coverage |
|-----------|--------------|--------------------------|
| UART WB controller | 6 tests (TX, RX, FIFO, errors) | Arty/MAX10 SoC boot |
| RAM/flash address mux | 48 checks (all decode paths) | Implicit in SoC tests |
| Peripheral mux | 11 checks (UART/Timer/GPIO) | Implicit in SoC tests |
| QSPI XIP (S25FL) | 7 + burst + 22 cache checks | Arty SoC echo (×2) |
| QSPI flash R/W | 6 SPI-level checks | Arty flash write SoC |
| MAX10 UFM model | 5 checks (erase/write/read) | Questa Tier 2 |
| MAX10 UFM WB bridge | 8 checks (all WB txn types) | MAX10 board XIP TB |
| MAX10 full SoC boot | — | MAX10 board XIP TB |
| Arty full SoC | — | XIP echo (direct + cache) |

**Not covered by simulation** (hardware-only verification):
- Real UFM erase/write timing vs 100 ms hardware spec
- PLL lock time and jitter
- UART baud rate accuracy (CP2102 clock tolerance)
- GPIO LED drive strength
