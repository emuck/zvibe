# Copyright (c) 2025 Martin R. Raumann
# SPDX-License-Identifier: BSD-3-Clause
# Vivado xsim TCL script for QSPI XIP Controller Wishbone Interface Test
# Usage: vivado -mode batch -source run_xip_wb_xsim.tcl

# Set project name and directory
set project_name "xip_wb_test"
set project_dir "xsim_xip_wb"

# Clean previous project
file delete -force $project_dir

# Create project
create_project $project_name $project_dir -part xc7s50csga324-1 -force

# Set project properties for simulation
set_property target_language Verilog [current_project]
set_property simulator_language Mixed [current_project]

# Add source files
add_files -norecurse {
    ../../../rtl/qspi/s25fl_xip.sv
    ../../models/s25fl_simple.v
    s25fl_xip_wb_tb.sv
}

# Update compile order
update_compile_order -fileset sources_1
update_compile_order -fileset sim_1

# Set top module
set_property top s25fl_xip_wb_tb [get_filesets sim_1]

# Configure simulation
# Run until test completes or timeout (200us should be plenty for all 5 tests)
set_property -name {xsim.simulate.runtime} -value {2ms} -objects [get_filesets sim_1]
set_property -name {xsim.simulate.log_all_signals} -value {true} -objects [get_filesets sim_1]

# Launch simulation
puts "=========================================="
puts "Launching xsim simulation..."
puts "=========================================="

launch_simulation

# Check results
set log_file ${project_dir}/${project_name}.sim/sim_1/behav/xsim/simulate.log
if {[file exists $log_file]} {
    set log_contents [read [open $log_file r]]
    if {[string match "*ALL TESTS PASSED*" $log_contents]} {
        puts "\n=========================================="
        puts "SUCCESS: All XIP Wishbone tests passed!"
        puts "=========================================="
        exit 0
    } else {
        puts "\n=========================================="
        puts "FAILURE: Some tests failed"
        puts "Check log: $log_file"
        puts "=========================================="
        exit 1
    }
} else {
    puts "ERROR: Log file not found: $log_file"
    exit 1
}
