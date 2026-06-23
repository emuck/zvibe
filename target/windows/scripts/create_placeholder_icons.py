#!/usr/bin/env python3
# Copyright (c) 2025 Martin R. Raumann
# SPDX-License-Identifier: BSD-3-Clause
"""
Create simple placeholder .ico files for ZVibe
This creates minimal valid ICO files that can be replaced with better icons later
"""

import struct
import os

def create_simple_ico(filename, color_rgb=(0, 100, 200)):
    """Create a simple 32x32 ICO file with a solid color and white border"""

    # ICO header (6 bytes)
    ico_header = struct.pack('<HHH', 0, 1, 1)  # Reserved=0, Type=1 (ICO), Count=1

    # ICO directory entry (16 bytes)
    width = height = 32
    colors = 0  # 0 means 256+ colors
    reserved = 0
    planes = 1
    bits_per_pixel = 24
    image_size = width * height * 3 + 40  # 24-bit RGB + DIB header
    image_offset = 22  # 6 (ICO header) + 16 (directory entry)

    ico_dir_entry = struct.pack('<BBBBHHLL',
                                width, height, colors, reserved,
                                planes, bits_per_pixel, image_size, image_offset)

    # DIB header (40 bytes) - BITMAPINFOHEADER
    dib_header = struct.pack('<LLLHHLLLLLL',
                            40,  # header size
                            width, height * 2,  # width, height*2 for ICO
                            1,   # planes
                            bits_per_pixel,  # bits per pixel
                            0,   # compression (none)
                            0,   # image size (can be 0 for uncompressed)
                            0, 0, 0, 0)  # other fields

    # Create 32x32 image data (bottom-up, BGR format)
    r, g, b = color_rgb
    image_data = bytearray()

    for y in range(height):
        for x in range(width):
            # Create a simple border effect
            if x < 2 or x >= width-2 or y < 2 or y >= height-2:
                # White border
                image_data.extend([255, 255, 255])  # BGR
            else:
                # Colored interior
                image_data.extend([b, g, r])  # BGR

        # Pad to 4-byte boundary
        while len(image_data) % 4 != 0:
            image_data.append(0)

    # AND mask (1 bit per pixel, all transparent)
    and_mask = b'\x00' * ((width * height + 7) // 8)

    # Write ICO file
    with open(filename, 'wb') as f:
        f.write(ico_header)
        f.write(ico_dir_entry)
        f.write(dib_header)
        f.write(image_data)
        f.write(and_mask)

def main():
    print("[create] Creating placeholder ZVibe icons...")

    # Create icons with different colors
    create_simple_ico('zvibe_console.ico', (0, 100, 200))   # Blue
    create_simple_ico('zvibe_minimal.ico', (100, 100, 100)) # Gray
    create_simple_ico('z3_game.ico', (0, 150, 50))          # Green

    print("[ok] Created placeholder icons:")
    print("   - zvibe_console.ico (blue)")
    print("   - zvibe_minimal.ico (gray)")
    print("   - z3_game.ico (green)")
    print("")
    print("[note] These are simple placeholder icons.")
    print("   Replace with professional icons using:")
    print("   - GIMP, Photoshop, or other image editor")
    print("   - Online icon generators")
    print("   - Icon fonts (Font Awesome, etc.)")

if __name__ == '__main__':
    main()