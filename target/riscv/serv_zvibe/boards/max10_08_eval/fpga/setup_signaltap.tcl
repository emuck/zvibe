#!/usr/bin/env quartus_stp -t
# Copyright (c) 2025 Martin R. Raumann
# SPDX-License-Identifier: BSD-3-Clause
# SignalTap Logic Analyzer Setup for MAX10 XIP Debugging

load_package stp

# Create SignalTap instance
set stp_file "output_files/servant_zvibe_max10_08_eval_xip.stp"
project_open servant_zvibe_max10_08_eval_xip

# Create new SignalTap instance if it doesn't exist
stp_create_instance auto_signaltap_0

# Clock setup - use sys_clk
stp_set_global_setting clock_name {sys_clk}
stp_set_global_setting sample_depth {2048}
stp_set_global_setting ram_type {AUTO}
stp_set_global_setting enable_type {combinatorial}

# Trigger setup - trigger on any reset release
stp_set_trigger_position {pre} {128}

# Add signals to probe
puts "Adding signals to SignalTap..."

# Reset signals
stp_add_probe {reset}
stp_add_probe {reset_n}
stp_add_probe {por_reset}
stp_add_probe {clk_locked}

# UFM interface signals
stp_add_probe {ufm_address[15:0]}
stp_add_probe {ufm_read}
stp_add_probe {ufm_readdata[31:0]}
stp_add_probe {ufm_readdatavalid}
stp_add_probe {ufm_waitrequest}

# Wishbone signals from CPU to UFM
stp_add_probe {wb_flash_cyc}
stp_add_probe {wb_flash_stb}
stp_add_probe {wb_flash_adr[31:0]}
stp_add_probe {wb_flash_rdt[31:0]}
stp_add_probe {wb_flash_ack}
stp_add_probe {wb_flash_stall}

# UART signals
stp_add_probe {uart_txd}
stp_add_probe {uart_rxd}

# GPIO LEDs
stp_add_probe {soc_gpio_led[2:0]}

# Save configuration
stp_save_instance auto_signaltap_0 $stp_file
project_close

puts "SignalTap configuration saved to $stp_file"
puts "Rebuild the project to include SignalTap."
