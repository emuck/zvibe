# Copyright (c) 2025 Martin R. Raumann
# SPDX-License-Identifier: BSD-3-Clause
# xsim_burst4_force.tcl
#
# xsim TCL batch script for s25fl_xip BURST_WORDS=4 vendor model test.
#
# 'add_force' in xsim works reliably with -debug all elaboration:
# it forces internal regs/wires regardless of optimizer state.
#
# Hierarchy root: /s25fl_xip_burst4_vendor_tb
# flash_model is s25fl128s instance — not inlined by xelab.
#
# QUAD = Config_reg1[1] = 1  → flash uses all 4 IO lines for QIOR
# WIP  = Status_reg1[0] = 0  → flash accepts commands (not busy)
# WEL  = Status_reg1[1] = 0  → WRR ignored (not needed)

add_force {/s25fl_xip_burst4_vendor_tb/flash_model/Config_reg1} -radix hex {02 0ns}
add_force {/s25fl_xip_burst4_vendor_tb/flash_model/Status_reg1} -radix hex {00 0ns}

run -all

quit
