# Copyright (c) 2025 Martin R. Raumann
# SPDX-License-Identifier: BSD-3-Clause
## run_vendor_burst4_vsim.tcl
##
## Questa FSE simulation: s25fl_xip BURST_WORDS=4 vs s25fl128s vendor model
##
## Tests the 128-bit burst read path used by qspi_cache_bram.
##
## Strategy: vsim -O0 disables optimization so all internal signals of
## s25fl128s are accessible for the SV 'force' in the testbench's initial block.
## WRR_WAIT_CYCLES=500 is used; QUAD+WIP are pre-forced before any commands.
##
## Usage:
##   cd common/tb/unit/flash
##   vsim -c -do run_vendor_burst4_vsim.tcl

set RTL_DIR  "../../../rtl"
set VENDOR   "~/max10/s25fl128s.v"

# Expand ~
set VENDOR [file normalize $VENDOR]

vlib work
vlog -sv +define+SPEEDSIM \
    $VENDOR \
    $RTL_DIR/qspi/s25fl_xip.sv \
    s25fl_xip_burst4_vendor_tb.sv

# -O0 disables simulation optimization — all internal signals stay accessible
# for the testbench 'force' statements without needing vopt +acc tricks.
vsim -O0 -t 1ps -c work.s25fl_xip_burst4_vendor_tb -do "run -all; quit -f"
