#!/bin/bash
# Copyright (c) 2025 Martin R. Raumann
# SPDX-License-Identifier: BSD-3-Clause
# Create simple icons for ZVibe using ImageMagick (if available) or provide instructions

set -e

echo "🎨 Creating ZVibe icons..."

# Check if ImageMagick is available
if command -v convert >/dev/null 2>&1; then
    echo "✅ ImageMagick found - creating icons programmatically"

    # Create ZVibe console icon (blue Z on white background)
    convert -size 64x64 xc:white \
            -gravity center \
            -pointsize 48 \
            -fill blue \
            -annotate 0 "Z" \
            -border 2 \
            -bordercolor blue \
            zvibe_console.png

    # Create ZVibe minimal icon (gray Z on white background)
    convert -size 64x64 xc:white \
            -gravity center \
            -pointsize 48 \
            -fill gray \
            -annotate 0 "Z" \
            -border 2 \
            -bordercolor gray \
            zvibe_minimal.png

    # Create Z3 game file icon (green Z3 on white background)
    convert -size 64x64 xc:white \
            -gravity center \
            -pointsize 32 \
            -fill darkgreen \
            -annotate 0 "Z3" \
            -border 2 \
            -bordercolor darkgreen \
            z3_game.png

    # Convert to ICO format if possible
    if command -v png2ico >/dev/null 2>&1; then
        png2ico zvibe_console.ico zvibe_console.png
        png2ico zvibe_minimal.ico zvibe_minimal.png
        png2ico z3_game.ico z3_game.png
        echo "✅ Created .ico files"
    else
        echo "⚠️  png2ico not found - created .png files only"
        echo "   You can convert them to .ico files using online tools or GIMP"
    fi

    echo "✅ Icons created successfully!"

else
    echo "❌ ImageMagick not found"
    echo ""
    echo "To create icons, you have these options:"
    echo ""
    echo "1. Install ImageMagick:"
    echo "   sudo apt install imagemagick"
    echo "   ./create_icons.sh"
    echo ""
    echo "2. Create icons manually:"
    echo "   - zvibe_console.ico (64x64, blue theme)"
    echo "   - zvibe_minimal.ico (64x64, gray theme)"
    echo "   - z3_game.ico (64x64, green theme)"
    echo ""
    echo "3. Use online icon creators:"
    echo "   - https://www.favicon-generator.org/"
    echo "   - https://convertico.com/"
    echo ""
    echo "4. Use GIMP or other image editor to create .ico files"
fi