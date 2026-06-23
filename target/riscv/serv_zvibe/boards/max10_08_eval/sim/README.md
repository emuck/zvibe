# MAX10 Questa Simulations

Questa simulations using the vendor Intel UFM IP model.
Validates the full XIP boot path with a bit-accurate UFM model.

For Verilator simulations (fast, behavioral UFM model), see `../tb/`.

## Prerequisites

- Questa FSE (or ModelSim) on PATH (`vsim`)
- UFM vendor IP simulation files generated: `../fpga/ip/ufm/simulation/mentor/msim_setup.tcl`
  - Generate with: `cd ../fpga && make generate-ip`
- `fpga/ip/ufm/simulation/ufm.v` must have `INIT_FILENAME_SIM` set (see `QUESTA_UFM_INIT_GUIDE.md`)

---

## Test 1: UART Echo XIP

Validates the full XIP boot path: PLL lock → reset release → UFM instruction fetch → UART output.
Uses a pre-built firmware `.dat` — no firmware rebuild needed unless source changes.

```bash
cd boards/max10_08_eval/sim
make -f Makefile.vsim uart-echo
```

Expected `uart_output.txt`:
```
==============================
MAX10 UART Echo Test
XIP from 0x80000100
==============================
Ready>
```

---

## Test 2: UFM Write

Validates runtime UFM erase/write/verify using the vendor model.
Firmware is built from source in the `sim/` directory.

```bash
cd boards/max10_08_eval/sim
make -f Makefile.vsim ufm-write
```

Expected `uart_output.txt` summary:
```
Test 1 (CONTROL safety): PASS
Test 2 (Page erase protocol): PASS
Test 3 (Erase verify): PASS
Test 4 (Program protocol): PASS
Test 5 (Read verify): PASS
```

Note: The erase `es` status bit reads 0 in this vendor simulation model (read-only model
limitation). Read-back verification is used instead to confirm writes took effect.

---

## Makefile Targets

```bash
make -f Makefile.vsim uart-echo           # Run uart-echo test
make -f Makefile.vsim uart-echo-firmware  # Build uart_echo_xip_test.dat only
make -f Makefile.vsim ufm-write           # Build firmware + run UFM write test
make -f Makefile.vsim ufm-write-firmware  # Build test_ufm_write.dat only
make -f Makefile.vsim clean
```

---

## Regenerating the uart-echo .dat

If `uart_echo_xip.c` in `common/sw/` changes:

```bash
cd common/sw
make uart_echo_xip.elf
riscv64-unknown-elf-objcopy -O verilog --verilog-data-width=4 \
  --change-addresses=-0x80000000 uart_echo_xip.elf \
  ../../boards/max10_08_eval/sim/uart_echo_xip_test.dat
```

**Critical**: `--change-addresses=-0x80000000` maps virtual addresses to UFM physical word
addresses. Without it the vendor model sees corrupted instruction encodings. See
`QUESTA_UFM_INIT_GUIDE.md` for details.

---

## Files

| File | Purpose |
|------|---------|
| `Makefile.vsim` | Questa build/run targets |
| `max10_xip_tb.sv` | SoC XIP testbench (uart-echo) |
| `servant_zvibe_ufm_write_tb.sv` | UFM write testbench |
| `run_questa_soc.tcl` | TCL compile/run script for uart-echo test |
| `run_questa_ufm_write.tcl` | TCL compile/run script for UFM write test |
| `uart_echo_xip_test.dat` | Pre-built firmware for echo test |
| `altera_onchip_flash.dat` | Active init file (copied from test .dat before sim) |
| `test_ufm_write.dat` | UFM write test firmware (built by Makefile) |
| `hex_to_ufm_dat.sh` | Converts Intel HEX to UFM .dat format |
| `ufm_wrapper.v` | UFM IP wrapper for testbench |
| `altpll_model.sv` | ALTPLL simulation model (reads defparam frequency parameters) |
| `QUESTA_UFM_INIT_GUIDE.md` | Critical: `.dat` format, address adjustment, ufm.v edit |
