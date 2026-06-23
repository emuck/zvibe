#!/usr/bin/env quartus_cpf
# Copyright (c) 2025 Martin R. Raumann
# SPDX-License-Identifier: BSD-3-Clause
# Generate .pof file with UFM data for MAX10 08 Evaluation Board
# This combines the FPGA configuration (.sof) with User Flash Memory data

set project_name "servant_zvibe_max10_08_eval"
set sof_file "output_files/${project_name}.sof"
set ufm_hex_file "ufm_data.hex"
set pof_output "output_files/${project_name}_with_ufm.pof"
set map_file "output_files/${project_name}_with_ufm.map"

# Check that input files exist
if {![file exists $sof_file]} {
    post_message -type error "SOF file not found: $sof_file"
    exit 1
}

if {![file exists $ufm_hex_file]} {
    post_message -type warning "UFM hex file not found: $ufm_hex_file"
    post_message -type warning "Creating .pof without UFM data"
    post_message -type warning "Run 'make ufm-hex' first to generate UFM data"
}

post_message "Generating .pof file with UFM data..."
post_message "  SOF: $sof_file"
post_message "  UFM: $ufm_hex_file"
post_message "  POF: $pof_output"

# Use quartus_cpf to create the .pof file
# This is equivalent to File > Convert Programming Files in the GUI

set conversion_setup [list]

# Set output file type and name
lappend conversion_setup -c
lappend conversion_setup output_file=[list "$pof_output"]
lappend conversion_setup mode=internal_configuration

# Add SOF file for CFM (FPGA configuration)
lappend conversion_setup input_file=[list "$sof_file"]

# Add UFM data if it exists
if {[file exists $ufm_hex_file]} {
    lappend conversion_setup ufm_data=[list "$ufm_hex_file"]
}

# ICB settings
lappend conversion_setup icb_power_on_reset_scheme=fast_por_delay
lappend conversion_setup icb_user_ios_weak_pull_up=off
lappend conversion_setup icb_jtag_security=off
lappend conversion_setup icb_verify_protect=off

# Generate memory map file
lappend conversion_setup create_memory_map_file=on

post_message "Conversion setup: $conversion_setup"

# Execute conversion
if {[catch {eval quartus_cpf $conversion_setup} result]} {
    post_message -type error "Failed to generate .pof file: $result"
    exit 1
}

post_message "Successfully generated: $pof_output"
if {[file exists $map_file]} {
    post_message "Memory map: $map_file"
}

exit 0
