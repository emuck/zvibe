# Copyright (c) 2025 Martin R. Raumann
# SPDX-License-Identifier: BSD-3-Clause
##
## Vivado Build Script for Servant ZVibe on Arty S7-50
## Uses SERV submodule instead of local copies
##

set design_name "servant_zvibe_arty_s7_50"
set part "xc7s50csga324-1"
set build_dir "build"

## Create project
create_project -force $design_name $build_dir -part $part

## Set project properties
set_property target_language Verilog [current_project]
set_property default_lib work [current_project]
set_property top servant_zvibe_arty_s7_50 [current_fileset]

## Path configuration
# Common RTL (shared across boards)
set common_rtl "../../../common/rtl"
# Board-specific RTL
set board_rtl "../rtl"
# SERV submodule paths (relative to build directory)
set serv_dir "../../../serv/rtl"
set serv_servile "../../../serv/servile"
set serv_servant "../../../serv/servant"
## Add RTL sources

# FPGA Top level (board-specific)
add_files $board_rtl/servant_zvibe_arty_s7_50.sv
add_files $board_rtl/servant_zvibe_arty_s7_50_clock_gen.sv

# Servant ZVibe SoC (common)
add_files $common_rtl/servant_zvibe.sv
add_files $common_rtl/servant_zvibe_mux.sv
add_files $common_rtl/servant_mem_mux.sv
add_files $common_rtl/servant_ram.sv
add_files $serv_servant/servant_timer.v

# UART (common)
add_files $common_rtl/uart/uart_wb.sv
add_files $common_rtl/uart/uart_tx.sv
add_files $common_rtl/uart/uart_rx.sv
add_files $common_rtl/uart/fifo_sync.sv

# GPIO Debug LEDs (common)
add_files $common_rtl/gpio_leds.sv

# QSPI Flash Controllers (common - cache + XIP read + write + mux)
add_files $common_rtl/qspi/qspi_cache_bram.sv
add_files $common_rtl/qspi/s25fl_xip.sv
add_files $common_rtl/qspi/s25fl_write.sv
add_files $common_rtl/qspi/qspi_mux.sv

# Multi-core wrapper (from SERV submodule, except servile_mux which is modified)
add_files $serv_servile/servile.v
add_files $common_rtl/servile_mux.sv
add_files $serv_servile/servile_arbiter.v

# SERV CPU Core (from submodule)
add_files $serv_dir/serv_top.v
add_files $serv_dir/serv_state.v
add_files $serv_dir/serv_decode.v
add_files $serv_dir/serv_ctrl.v
add_files $serv_dir/serv_alu.v
add_files $serv_dir/serv_rf_top.v
add_files $serv_dir/serv_rf_if.v
add_files $serv_dir/serv_rf_ram.v
add_files $serv_dir/serv_rf_ram_if.v
add_files $serv_dir/serv_mem_if.v
add_files $serv_dir/serv_csr.v
add_files $serv_dir/serv_immdec.v
add_files $serv_dir/serv_aligner.v
add_files $serv_dir/serv_bufreg.v
add_files $serv_dir/serv_bufreg2.v
add_files $serv_dir/serv_compdec.v

## Add constraints
add_files -fileset constrs_1 arty_s7_50.xdc

## Run synthesis
launch_runs synth_1
wait_on_run synth_1
open_run synth_1

## Generate utilization report
report_utilization -file $build_dir/utilization_synth.txt

## Run implementation
launch_runs impl_1 -to_step route_design
wait_on_run impl_1
open_run impl_1

## Generate post-implementation reports
report_utilization -file $build_dir/utilization_impl.txt
report_timing_summary -file $build_dir/timing_summary.txt

## Generate bitstream
launch_runs impl_1 -to_step write_bitstream
wait_on_run impl_1

## Copy bitstream to build directory
file copy -force $build_dir/$design_name.runs/impl_1/$design_name.bit $build_dir/$design_name.bit

puts "\n=========================================="
puts "Build Complete!"
puts "Bitstream: $build_dir/$design_name.bit"
puts "=========================================="
