#!/bin/bash
###############################################################################
# hex_to_ufm_dat.sh
#
# Converts RISC-V firmware .hex files to MAX10 UFM .dat format
#
# Usage: hex_to_ufm_dat.sh input.hex output.dat
#
# UFM .dat format: Each line contains one word address and one 32-bit data value:
#   @AAAAAAAA DDDDDDDD
#   @AAAAAAAA DDDDDDDD
#   ...
#
# Conversion steps:
# 1. Input .hex must be word-addressed (objcopy --verilog-data-width=4)
# 2. Converts full 32-bit addresses (@20000040) to UFM-relative (@00000040)
# 3. Splits multi-word lines into individual address/data pairs
# 4. UFM supports 17-bit word addressing (0x00000 - 0x1FFFF)
#
# The firmware is linked at byte address 0x80000100, which equals:
#   - Word address: 0x20000040 (full 32-bit address space)
#   - UFM word address: 0x00000040 (UFM-relative, bits [16:0])
#
# Copyright (c) 2025 Martin R. Raumann
# SPDX-License-Identifier: BSD-3-Clause
###############################################################################

set -e

if [ $# -ne 2 ]; then
    echo "Usage: $0 input.hex output.dat"
    echo ""
    echo "Converts RISC-V firmware .hex (word-addressed) to UFM .dat format"
    echo ""
    echo "Example:"
    echo "  # First generate word-addressed .hex from .elf:"
    echo "  riscv64-unknown-elf-objcopy -O verilog --verilog-data-width=4 firmware.elf firmware.hex"
    echo ""
    echo "  # Then convert to UFM .dat:"
    echo "  $0 firmware.hex ufm_firmware.dat"
    exit 1
fi

INPUT_HEX="$1"
OUTPUT_DAT="$2"

if [ ! -f "$INPUT_HEX" ]; then
    echo "Error: Input file '$INPUT_HEX' not found"
    exit 1
fi

# Check if input looks like word-addressed format
if ! head -1 "$INPUT_HEX" | grep -q "^@[0-9A-Fa-f]\{8\}"; then
    echo "Warning: Input file may not be word-addressed format"
    echo "Expected format: @AAAAAAAA followed by 32-bit hex words"
    echo ""
    echo "To generate correct format:"
    echo "  riscv64-unknown-elf-objcopy -O verilog --verilog-data-width=4 firmware.elf firmware.hex"
fi

# Convert hex to dat format
# Input format:  @20000040
#                00008137 00010113 ...
# Output format: @00000040 00008137
#                @00000041 00010113
#                ...

awk '
BEGIN {
    addr = 0;
}

/^@/ {
    # Parse address line, convert from full 32-bit to UFM-relative (strip upper bits)
    addr_str = substr($0, 2);  # Remove @ prefix
    addr = strtonum("0x" addr_str);
    addr = and(addr, 0x1FFFF);  # Keep only lower 17 bits (UFM address space)
    next;
}

{
    # Data line - split into individual words
    for (i = 1; i <= NF; i++) {
        printf "@%08X %s\n", addr, toupper($i);
        addr++;
    }
}
' "$INPUT_HEX" > "$OUTPUT_DAT"

echo "Converted $INPUT_HEX -> $OUTPUT_DAT"

# Show first few lines for verification
echo ""
echo "First 10 lines of output:"
head -10 "$OUTPUT_DAT"

# Show size
LINES=$(wc -l < "$OUTPUT_DAT")
echo ""
echo "Total lines: $LINES"
echo "UFM capacity: 43904 words (172KB)"

if [ $LINES -gt 43904 ]; then
    echo "WARNING: Firmware exceeds UFM capacity!"
fi
