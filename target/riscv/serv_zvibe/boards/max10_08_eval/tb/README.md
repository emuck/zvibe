# MAX10 Verilator Testbenches (`boards/max10_08_eval/tb/`)

Verilator simulations for the MAX10 board using behavioral models for the
Intel UFM IP (`ufm_sim.sv`) and PLL (`altpll_model.sv`).  No Questa license
required.  For Questa simulations with the vendor UFM model, see `../sim/`.

---

## UFM Test Suite

The MAX10 UFM path is verified in three progressive layers:

```
Layer 2 (Questa) — vendor Intel UFM model          ../sim/Makefile.vsim
                        ↑ highest fidelity
Layer 1b — board SoC TB (full CPU + UFM)            Makefile
                        ↑
Layer 1a — UFM bridge unit test (WB → Avalon-MM)    Makefile.ufm_unit_test test-bridge
                        ↑
Layer 0  — UFM behavioral model self-test            Makefile.ufm_unit_test test-ufm
```

Running from top to bottom (fastest → slowest):

```bash
make -f Makefile.ufm_unit_test test-ufm      # Layer 0: model sanity check
make -f Makefile.ufm_unit_test test-bridge   # Layer 1a: WB bridge unit test
make run                                      # Layer 1b: full SoC boot
# Layer 2 requires vsim — see ../sim/
```

---

## Test Details

### Layer 0 — UFM Behavioral Model Self-Test

**Testbench**: `ufm_model_unit_tb.sv`
**DUT**: `ufm_sim.sv` (behavioral model — no synthesisable RTL)
**Make target**: `make -f Makefile.ufm_unit_test test-ufm`

Tests the behavioral UFM model directly at the Avalon-MM interface, before any
bridge logic is involved.  If this fails, the problem is in the model, not the
DUT.

| # | Test | What it checks |
|---|------|---------------|
| 1 | STATUS read | Bits 1/3/4 set (rs/ws/es=1), bit 2 clear (busy=0) |
| 2 | Erased-state read | Fresh memory reads 0xFFFFFFFF |
| 3 | CONTROL write (safety) | Inactive pe/se fields accepted without erase |
| 4 | Page program | Write then read-back a single word |
| 5 | Page erase | Erase, then verify 0xFFFFFFFF |

Expected output: `PASS: 5  FAIL: 0  *** ALL TESTS PASSED ***`

---

### Layer 1a — UFM Wishbone Bridge Unit Test

**Testbench**: `max10_ufm_unified_tb.sv`
**DUT**: `max10_ufm_unified.sv` (Wishbone → Avalon-MM bridge)
**Models**: `ufm_sim.sv` connected directly to DUT outputs (no pipeline regs)
**Make target**: `make -f Makefile.ufm_unit_test test-bridge`

Drives the bridge via WB master tasks (`wb_read`, `wb_write`, `wait_not_stall`)
and verifies all seven WB transaction types defined by the bridge state machine.

| # | WB address | Transaction | What it checks |
|---|-----------|-------------|---------------|
| 1 | 0x80000100 | XIP read | `addr[19:18]=00` → avmm_data; correct word returned |
| 2 | 0x80040000 | CSR STATUS read | `addr[19:18]=01`, `addr[3:2]=00` → avmm_csr addr=0; STATUS = 0x1A |
| 3 | 0x80040008 | WRITE_ADDR write + read | Latches 17-bit addr in register; reads back cleanly |
| 4 | 0x8004000C | WRITE_DATA write | Triggers UFM program; XIP-read 0x80000200 verifies data |
| 5 | 0x80040004 | CONTROL write (erase) | pe=0x00000 triggers erase; XIP-read 0x80000000 → 0xFFFFFFFF |
| 6 | 0x8003FFFC / 0x80040000 | Boundary decode | Last XIP word vs first CSR word; correct path taken |
| 7 | 0x8004000C | WRITE_DATA read | Returns hardcoded 0xDEADBEEF default |

**Stall behaviour**: tests 4 and 5 call `wait_not_stall` after triggering
UFM program (~10 cycles) or erase (~100 cycles) to let `avmm_data_waitrequest`
deassert before issuing the verification read.

Expected output: `Results: 8 passed, 0 failed  PASS: max10_ufm_unified_tb`

---

### Layer 1b — MAX10 Board SoC XIP Boot

**Testbench**: `servant_zvibe_max10_08_eval_xip_tb.sv`
**DUT**: Complete MAX10 top-level SoC
**Models**: `ufm_sim.sv` + `altpll_model.sv`
**Make target**: `make run`

Instantiates the full SoC with a minimal "Hello!\n" firmware pre-loaded into
the UFM model.  End-to-end path: 50 MHz clock → altpll model → 100 MHz →
reset release → CPU boot at 0x80000100 → UFM XIP fetch → UART output.

Verifies:
- PLL lock signal asserted within timeout
- CPU first fetch from 0x80000100 (correct reset PC)
- Exactly 7 UART characters received (`H e l l o ! \n`)
- 300 000-cycle watchdog (3 ms at 100 MHz)

Expected output:
```
[TB] ✓ SUCCESS: CPU started at correct address 0x80000100
[TB] ✓ SUCCESS: Received 7 UART characters (expected 7)
✓✓✓ ALL TESTS PASSED ✓✓✓
PASS: max10_board_xip_tb
```

---

## Files

| File | Purpose |
|------|---------|
| `Makefile` | Board SoC Verilator build + run |
| `Makefile.ufm_unit_test` | UFM unit tests (`test-ufm`, `test-bridge`) |
| `tb_main.cpp` | Verilator C++ driver for board SoC TB |
| `servant_zvibe_max10_08_eval_xip_tb.sv` | Board-level SoC testbench (Layer 1b) |
| `ufm_model_unit_tb.sv` | UFM behavioral model self-test (Layer 0) |
| `max10_ufm_unified_tb.sv` | UFM WB bridge unit test (Layer 1a) |
| `ufm_sim.sv` | Behavioral UFM model (erase/write/read, Avalon-MM) |
| `altpll_model.sv` | Behavioral PLL model (50 → 100 MHz) |

---

## Coverage Gaps

The behavioral `ufm_sim.sv` model approximates hardware timing:

| Behaviour | Sim model | Hardware |
|-----------|-----------|---------|
| Erase delay | 100 cycles (~1 µs) | ~100 ms |
| Program delay | 10 cycles (~100 ns) | ~10 ms |
| Read latency | 1 cycle | 1 cycle (matches) |
| Waitrequest | Deasserts after delay | Same |

Real timing is validated by the Questa Tier 2 tests against the Intel vendor
model.  See `../sim/`.
