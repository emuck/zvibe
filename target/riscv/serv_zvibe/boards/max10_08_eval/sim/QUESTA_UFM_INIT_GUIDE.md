# Questa Simulation with Vendor UFM IP - Critical Setup Guide

**Status:** ✅ Validated Working (February 2026)

## Overview

This guide documents the **critical, non-obvious steps** required to get Questa simulation
working with the vendor Intel MAX10 UFM IP core.

## Problem Statement

**Symptom:** Simulation runs but CPU executes garbage (all 0xFFFFFFFF from UFM)

**Root Cause:** The vendor UFM IP's `INIT_FILENAME_SIM` parameter is empty in the generated
wrapper, so it never loads the firmware `.dat` file.

---

## Required Setup (One-Time)

### 1. Generate IP simulation files

```bash
cd boards/max10_08_eval/fpga
make generate-ip
```

### 2. Fix UFM IP Init File Parameter

**File:** `boards/max10_08_eval/fpga/ip/ufm/simulation/ufm.v` (line ~26)

Change from:
```verilog
.INIT_FILENAME_SIM                   (""),
```
To:
```verilog
.INIT_FILENAME_SIM                   ("altera_onchip_flash.dat"),
```

**Why:** Quartus IP generation leaves this empty. Without it, UFM memory is uninitialized
(all `0xFFFFFFFF`). This edit may need to be redone if the UFM IP is regenerated.

---

## .dat File Format

The vendor UFM model loads a word-addressed hex file named `altera_onchip_flash.dat`
from the simulation working directory.

### Generating from ELF (uart-echo firmware)

```bash
riscv64-unknown-elf-objcopy \
  -O verilog \
  --verilog-data-width=4 \
  --change-addresses=-0x80000000 \
  uart_echo_xip.elf \
  altera_onchip_flash.dat
```

**Why `--change-addresses=-0x80000000` is critical:**
- Firmware is linked at virtual `0x80000100`
- UFM physical memory starts at `0x00000000`
- Without the flag, `.dat` addresses are `@20000040` — the vendor model won't find the data
- With the flag, addresses become `@00000040` — correct UFM word offset

**Verify:**
```bash
head -3 altera_onchip_flash.dat
# Should start with @00000040, NOT @20000040
```

### Generating from binary flash image (for full ZVibe sim with game data)

```bash
objcopy -I binary -O verilog --verilog-data-width=4 --reverse-bytes=4 \
  max10_flash_test.bin altera_onchip_flash.dat
```

**Why `--reverse-bytes=4`:** `-I binary` outputs bytes in memory order (little-endian),
but the vendor UFM model expects big-endian 32-bit words. Without this flag, RISC-V
instructions are byte-swapped and the CPU executes garbage.

---

## Running Simulations

```bash
cd boards/max10_08_eval/sim

# uart-echo XIP test (copies uart_echo_xip_test.dat → altera_onchip_flash.dat automatically)
make -f Makefile.vsim uart-echo

# UFM write test (builds firmware from source, runs erase/write/verify)
make -f Makefile.vsim ufm-write
```

The Makefile handles `.dat` copying automatically. See `README.md` for full target list.

---

## Troubleshooting

### CPU executes 0xFFFFFFFF instructions

UFM returning uninitialized memory. Check:
1. `altera_onchip_flash.dat` exists in the sim working directory
2. `INIT_FILENAME_SIM` is set in `fpga/ip/ufm/simulation/ufm.v`
3. `.dat` file starts with `@00000040` (not `@20000040`)

### Compilation error: `fiftyfivenm_unvm` not defined

Device libraries not compiled. The TCL scripts call `dev_com` via `msim_setup.tcl` to
handle this. Run via the Makefile targets (not raw `vsim`) to ensure correct library setup.

### Erase success bit (`es`) reads 0

Expected in simulation. The vendor model is read-only with respect to status bits — it
updates memory on writes but doesn't assert `es`/`ws` in status. Read-back verification
is used instead to confirm operations succeeded.

### UFM IP regenerated — lost INIT_FILENAME_SIM edit

Re-apply step 2 above. The edit is to a generated file and is not preserved across
IP regeneration.

---

## File Format Reference

```
@00000000 00000000
...
@00000040 00008137  ← First instruction (virtual 0x80000100 → physical word 0x40)
@00000041 00010113
...
```

- Address is **word-addressed** (address N = bytes 4N..4N+3)
- Data is 32-bit big-endian hex (as expected by `$readmemh`)
- Multiple words may appear space-separated on one line
