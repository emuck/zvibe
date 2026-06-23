# Copyright (c) 2025 Martin R. Raumann
# SPDX-License-Identifier: BSD-3-Clause
# MAX10 UFM Programming Script
# Programs both CFM (configuration) and UFM (user flash) from POF file

set pof_file "plunderedhearts.pof"
set device_name "10M08SAE144"
set cable_name "USB-BlasterII"

# Open device
set device_index 1

puts "Programming MAX10 with UFM data..."
puts "POF file: $pof_file"

# Program CFM and UFM
# For MAX10, both CFM and UFM are programmed from the same POF file
# The key is using the correct operation that includes UFM

catch {
    quartus_pgm -c $cable_name -m jtag \
        -o "p;$pof_file@$device_index"
} result

puts "Result: $result"
