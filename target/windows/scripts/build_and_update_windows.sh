#!/bin/bash
# Copyright (c) 2025 Martin R. Raumann
# SPDX-License-Identifier: BSD-3-Clause
# Build Windows executables and update the Windows installation

set -e

echo "🔨 Building Windows executables..."

# Build all Windows targets
make windows-all

echo ""
echo "📦 Build completed successfully!"

# Update the Windows installation
echo ""
./update_windows_build.sh

echo ""
echo "🚀 Ready to test on Windows!"
echo "   Run: C:\\tmp\\zvibe_win\\win\\zvibe_console.exe czech.z3"