#!/bin/bash
# Copyright (c) 2025 Martin R. Raumann
# SPDX-License-Identifier: BSD-3-Clause
# run_vendor_burst4_xsim.sh
#
# Vivado xsim simulation: s25fl_xip BURST_WORDS=4 vs s25fl128s vendor model
#
# Uses xelab -debug all + xsim add_force to set QUAD=1/WIP=0 at t=0,
# bypassing the 2ms WRR wait (SPEEDSIM tdevice_WRR).
#
# Usage:
#   cd common/tb/unit/flash
#   ./run_vendor_burst4_xsim.sh

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RTL_DIR="$SCRIPT_DIR/../../../rtl"
VENDOR="$HOME/max10/s25fl128s.v"
BD="$SCRIPT_DIR/build_vendor_burst4"

mkdir -p "$BD"

# xsim resolves mem files relative to the xsim.dir location's parent, which is
# the build directory. Copy them there so $readmemh in s25fl128s can find them.
cp "$SCRIPT_DIR/s25fl128s_test.mem" "$BD/"
cp "$SCRIPT_DIR/s25fl128sOTP.mem"   "$BD/"

cd "$BD"

echo "=== xvlog: compiling sources ==="
xvlog -sv -d SPEEDSIM \
    "$VENDOR" \
    "$RTL_DIR/qspi/s25fl_xip.v" \
    "$SCRIPT_DIR/s25fl_xip_burst4_vendor_tb.sv"

echo "=== xelab: elaborating with -debug all ==="
xelab -debug all \
    s25fl_xip_burst4_vendor_tb \
    -s sim_burst4 \
    -timescale 1ns/1ps \
    --log xelab.log

echo "=== xsim: running with force ==="
xsim sim_burst4 -tclbatch "$SCRIPT_DIR/xsim_burst4_force.tcl" --log xsim.log

echo ""
echo "=== Results ==="
grep -E "PASS|FAIL|TIMEOUT|Results:|ALL TESTS" xsim.log || true
