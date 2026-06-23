#!/bin/bash
# Copyright (c) 2025 Martin R. Raumann
# SPDX-License-Identifier: BSD-3-Clause
# Update Windows build in C:\tmp\zvibe_win after rebuilding

set -e

WINDOWS_DIR="/mnt/c/tmp/zvibe_win"
BUILD_DIR="build"

echo "🔄 Updating Windows ZVibe installation..."

# Check if Windows directory exists
if [ ! -d "$WINDOWS_DIR" ]; then
    echo "❌ Windows directory not found: $WINDOWS_DIR"
    echo "   Make sure you've copied the initial build to C:\\tmp\\zvibe_win"
    exit 1
fi

# Check if build directory exists
if [ ! -d "$BUILD_DIR" ]; then
    echo "❌ Build directory not found. Run 'make windows-all' first."
    exit 1
fi

# Update executables
if [ -f "$BUILD_DIR/win/zvibe_console.exe" ]; then
    # Try to copy, handle permission issues gracefully
    if cp "$BUILD_DIR/win/zvibe_console.exe" "$WINDOWS_DIR/win/" 2>/dev/null; then
        echo "✅ Updated zvibe_console.exe"
    else
        echo "⚠️  Could not update zvibe_console.exe (may be in use - close any running instances)"
    fi
else
    echo "⚠️  zvibe_console.exe not found - run 'make windows-console' first"
fi

if [ -f "$BUILD_DIR/win/zvibe_minimal.exe" ]; then
    if cp "$BUILD_DIR/win/zvibe_minimal.exe" "$WINDOWS_DIR/win/" 2>/dev/null; then
        echo "✅ Updated zvibe_minimal.exe"
    else
        echo "⚠️  Could not update zvibe_minimal.exe (may be in use - close any running instances)"
    fi
else
    echo "⚠️  zvibe_minimal.exe not found - run 'make windows-minimal' first"
fi

# Update shared library
if [ -f "$BUILD_DIR/win_lib/libzvibe.dll" ]; then
    cp "$BUILD_DIR/win_lib/libzvibe.dll" "$WINDOWS_DIR/win_lib/"
    echo "✅ Updated libzvibe.dll"
else
    echo "⚠️  libzvibe.dll not found - run 'make windows-shared' first"
fi

# Update import library
if [ -f "$BUILD_DIR/win_lib/libzvibe.dll.a" ]; then
    cp "$BUILD_DIR/win_lib/libzvibe.dll.a" "$WINDOWS_DIR/win_lib/"
    echo "✅ Updated libzvibe.dll.a"
fi

echo ""
echo "🎉 Windows build updated successfully!"
echo "   Location: C:\\tmp\\zvibe_win"
echo "   You can now test the updated executables on Windows"