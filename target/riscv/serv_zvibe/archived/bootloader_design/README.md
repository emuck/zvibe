# Archived: Bootloader Design

**Status:** Obsolete - Replaced by XIP (Execute-In-Place) architecture

## What This Was

This directory contains the original bootloader design for MAX10, which used:
- **Bootloader FSM** to copy boot stub from UFM to RAM
- **Boot stub** in RAM at 0x00000000
- RAM initialization on every power-up

## Why It Was Replaced

The XIP design is simpler, faster, and more elegant:
- Firmware executes directly from UFM (no copy needed)
- No bootloader FSM complexity
- No boot stub in RAM
- Faster boot (no copy delay)
- More RAM available for application

## Files Archived

- `max10_bootloader_fsm.v` - Bootloader FSM RTL
- `bootloader_fsm/` - Testbench and documentation

## Current Design

See `boards/max10_08_eval/` for the XIP implementation:
- `rtl/servant_zvibe_max10_08_eval_xip.v` - XIP top-level
- `common/rtl/ufm/max10_ufm_xip.v` - XIP controller
- `boards/max10_08_eval/fpga/HARDWARE_TEST.md` - Working procedure

**Date Archived:** 2026-02-01
**Reason:** Switched to XIP architecture (hardware verified)
