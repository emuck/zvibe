# Copyright (c) 2025 Martin R. Raumann
# SPDX-License-Identifier: BSD-3-Clause
##
## Non-project Vivado build for Servant ZVibe on Arty S7-50 (`make build-nonproject`)
##
## The same sources, constraints and part as build.tcl, built with the plain
## synth_design / place_design / route_design / write_bitstream flow. build.tcl's project flow
## ends synthesis by writing a hardware-definition file; on some host setups Vivado 2025.1
## crashes there ("Abnormal program termination (7)" in libxv_hdf.so) after a successful
## synthesis. This flow never writes that file. The bitstream is the same design.
##
## The SoC RAM size comes from the ARTY_RAM_KB environment variable (default 32), as in build.tcl.
##

set design_name "servant_zvibe_arty_s7_50"
set part "xc7s50csga324-1"
set build_dir "build"

set ram_kb 32
if {[info exists ::env(ARTY_RAM_KB)]} { set ram_kb $::env(ARTY_RAM_KB) }
puts "SoC RAM: ${ram_kb} KB"

file mkdir $build_dir

## Path variables, as build.tcl defines them
set common_rtl "../../../common/rtl"
set board_rtl "../rtl"
set serv_dir "../../../serv/rtl"
set serv_servile "../../../serv/servile"
set serv_servant "../../../serv/servant"

## Read the sources build.tcl adds, so the two flows cannot drift apart
set fh [open build.tcl r]
set lines [split [read $fh] "\n"]
close $fh
set nsrc 0
foreach line $lines {
    if {[regexp {^add_files\s+(\S+)\s*$} $line -> arg]} {
        set path [subst -nocommands $arg]
        if {[string match *.sv $path]} {
            read_verilog -sv $path
        } else {
            read_verilog $path
        }
        incr nsrc
    }
}
puts "Read $nsrc source files"
read_xdc arty_s7_50.xdc

## Synthesis
synth_design -top $design_name -part $part -generic RAM_KB=$ram_kb
report_utilization -file $build_dir/utilization_synth.txt

## Implementation
opt_design
place_design
route_design
report_utilization -file $build_dir/utilization_impl.txt
report_timing_summary -file $build_dir/timing_summary.txt

## Bitstream
write_bitstream -force $build_dir/$design_name.bit

puts "\n=========================================="
puts "Build Complete (non-project flow)!"
puts "Bitstream: $build_dir/$design_name.bit"
puts "=========================================="
