#!/usr/bin/env python3
# Copyright (c) 2025 Martin R. Raumann
# SPDX-License-Identifier: BSD-3-Clause
"""
Convert binary file to Intel Hex format for Quartus UFM programming.

Intel Hex Format:
:LLAAAATT[DD...]CC

LL = byte count (hex)
AAAA = address (hex)
TT = record type (00=data, 01=EOF)
DD = data bytes
CC = checksum (2's complement of sum of all bytes)
"""

import sys
import argparse
from pathlib import Path


def bin_to_intel_hex(bin_file, hex_file, bytes_per_line=16, start_address=0):
    """Convert binary file to Intel Hex format with Extended Linear Address support."""

    # Read binary data
    raw_data = Path(bin_file).read_bytes()

    # Prepend padding bytes (0xFF) if start_address > 0
    if start_address > 0:
        data = bytes([0xFF] * start_address) + raw_data
        print(f"Converting {len(raw_data)} bytes from {bin_file} at offset 0x{start_address:X}...")
    else:
        data = raw_data
        print(f"Converting {len(data)} bytes from {bin_file}...")

    with open(hex_file, 'w') as f:
        current_segment = -1  # Track upper 16 bits of address

        for i in range(0, len(data), bytes_per_line):
            addr = i
            chunk = data[i:i+bytes_per_line]
            byte_count = len(chunk)

            # Check if we need an Extended Linear Address Record
            # This sets the upper 16 bits of the 32-bit address
            segment = (addr >> 16) & 0xFFFF
            if segment != current_segment:
                # Write Extended Linear Address Record (type 04)
                # Format: :02 0000 04 SSSS CC
                # Where SSSS is the upper 16 bits of the address
                ext_checksum = 0x02 + 0x00 + 0x00 + 0x04 + (segment >> 8) + (segment & 0xFF)
                ext_checksum = (0x100 - (ext_checksum & 0xFF)) & 0xFF
                f.write(f':02000004{segment:04X}{ext_checksum:02X}\n')
                current_segment = segment

            # Write data record with lower 16 bits of address
            lower_addr = addr & 0xFFFF

            # Calculate checksum
            checksum = byte_count + (lower_addr >> 8) + (lower_addr & 0xFF) + 0x00 + sum(chunk)
            checksum = (0x100 - (checksum & 0xFF)) & 0xFF

            # Write data record (type 00)
            f.write(f':{byte_count:02X}{lower_addr:04X}00')
            f.write(''.join(f'{b:02X}' for b in chunk))
            f.write(f'{checksum:02X}\n')

        # Write EOF record (type 01)
        f.write(':00000001FF\n')

    print(f"Created: {hex_file}")
    hex_size = Path(hex_file).stat().st_size
    print(f"  Binary: {len(data)} bytes")
    print(f"  Hex:    {hex_size} bytes")
    print(f"  Extended Linear Address Records: {current_segment + 1}")


def main():
    parser = argparse.ArgumentParser(
        description='Convert binary file to Intel Hex format for Quartus UFM programming'
    )
    parser.add_argument('input', help='Input binary file')
    parser.add_argument('output', help='Output Intel Hex file')
    parser.add_argument('--bytes-per-line', type=int, default=16,
                       help='Bytes per line (default: 16)')
    parser.add_argument('--start-address', default='0',
                       help='Pad with 0xFF from address 0 up to this offset before data (hex, e.g. 0x1000)')

    args = parser.parse_args()

    if not Path(args.input).exists():
        print(f"ERROR: Input file not found: {args.input}", file=sys.stderr)
        sys.exit(1)

    start_address = int(args.start_address, 0)

    try:
        bin_to_intel_hex(args.input, args.output, args.bytes_per_line, start_address)
    except Exception as e:
        print(f"ERROR: {e}", file=sys.stderr)
        sys.exit(1)


if __name__ == '__main__':
    main()
