#!/usr/bin/env python3
# Copyright (c) 2025 Martin R. Raumann
# SPDX-License-Identifier: BSD-3-Clause
"""
Convert a binary file to a C byte array.

Usage: bin2c.py input_file output_file array_name
"""

import sys

if len(sys.argv) != 4:
    print(f"Usage: {sys.argv[0]} input_file output_file array_name")
    sys.exit(1)

input_file = sys.argv[1]
output_file = sys.argv[2]
array_name = sys.argv[3]

try:
    with open(input_file, 'rb') as f:
        data = f.read()

    with open(output_file, 'w') as f:
        f.write(f"// Generated from {input_file}\n")
        f.write(f"// Size: {len(data)} bytes\n\n")
        f.write(f"const unsigned char {array_name}[] = {{\n")

        # Write 12 bytes per line
        for i in range(0, len(data), 12):
            chunk = data[i:i+12]
            hex_values = ', '.join(f'0x{b:02x}' for b in chunk)
            f.write(f"    {hex_values},\n")

        f.write(f"}};\n\n")
        f.write(f"const unsigned int {array_name}_size = {len(data)};\n")

    print(f"Generated {output_file}: {len(data)} bytes")

except FileNotFoundError as e:
    print(f"Error: {e}")
    sys.exit(1)
except Exception as e:
    print(f"Error: {e}")
    sys.exit(1)
