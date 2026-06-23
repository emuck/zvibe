#!/usr/bin/env python3
# Copyright (c) 2025 Martin R. Raumann
# SPDX-License-Identifier: BSD-3-Clause
"""
Convert Intel HEX to flat binary for edbg programming.

Keeps only the main flash address range (below the User Row at 0x00804000),
producing a clean binary equivalent to what xc32-objcopy would generate on
Linux -- without the 512 MB BSS expansion seen with xc32-objcopy v5.0+ on macOS.
"""

import sys
from pathlib import Path

FLASH_BASE  = 0x00000000
FLASH_LIMIT = 0x00800000   # below NVM User Row at 0x00804000


def hex_to_bin(hex_path: Path, bin_path: Path) -> int:
    lines = hex_path.read_text().splitlines()
    data: dict[int, int] = {}
    current_base = 0

    for line in lines:
        if not line.startswith(':'):
            continue
        rec_type = int(line[7:9], 16)
        if rec_type == 4:                          # Extended Linear Address
            current_base = int(line[9:13], 16) << 16
        elif rec_type == 0:                        # Data
            addr = current_base + int(line[3:7], 16)
            if FLASH_BASE <= addr < FLASH_LIMIT:
                n = int(line[1:3], 16)
                for i in range(n):
                    data[addr + i] = int(line[9 + i*2: 11 + i*2], 16)

    if not data:
        print(f"Error: no flash data found in {hex_path.name}", file=sys.stderr)
        sys.exit(1)

    size = max(data.keys()) + 1
    binary = bytearray(size)
    for addr, byte in data.items():
        binary[addr] = byte

    bin_path.write_bytes(binary)
    return size


if __name__ == '__main__':
    if len(sys.argv) != 3:
        print(f"Usage: {sys.argv[0]} input.hex output.bin", file=sys.stderr)
        sys.exit(1)

    size = hex_to_bin(Path(sys.argv[1]), Path(sys.argv[2]))
    print(f"Wrote {size} bytes ({size / 1024:.1f} KB) to {sys.argv[2]}")
