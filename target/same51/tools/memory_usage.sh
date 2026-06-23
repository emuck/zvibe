#!/bin/bash
# Copyright (c) 2025 Martin R. Raumann
# SPDX-License-Identifier: BSD-3-Clause
# SAM E51J20A Memory Usage Reporter
# Flash: 1MB (1,048,576 bytes)
# RAM:   256KB (262,144 bytes)

if [ $# -ne 1 ]; then
    echo "Usage: $0 <elf-file>"
    exit 1
fi

ELF_FILE="$1"

# Auto-detect latest XC32 version just like the Makefile does
UNAME_S=$(uname -s)
if [ "$UNAME_S" = "Darwin" ]; then
    # macOS
    XC32_BASE="/Applications/microchip/xc32"
elif [ "$UNAME_S" = "Linux" ]; then
    # Linux
    XC32_BASE="/opt/microchip/xc32"
else
    echo "Error: Unsupported operating system: $UNAME_S"
    exit 1
fi

# Find latest installed version
XC32_VERSION=$(ls -d "$XC32_BASE"/v* 2>/dev/null | sort -V | tail -1 | xargs basename)
if [ -z "$XC32_VERSION" ]; then
    echo "Error: XC32 compiler not found in $XC32_BASE"
    exit 1
fi
XC32_PATH="$XC32_BASE/$XC32_VERSION/bin"

SIZE_CMD="${XC32_PATH}/xc32-size"

# SAM E51J20A memory limits
FLASH_SIZE=1048576  # 1MB
RAM_SIZE=262144     # 256KB

echo "========================================="
echo "SAM E51J20A Memory Usage Report"
echo "========================================="

# Check if XC32 toolchain is available
if [ ! -f "$SIZE_CMD" ]; then
    echo "⚠️  XC32 toolchain not found at: $SIZE_CMD"
    echo "   Install XC32 to get detailed memory usage reporting"
    echo "   Build completed successfully without memory analysis"
    echo "========================================="
    exit 0
fi

# Get memory usage from size command
SIZE_OUTPUT=$($SIZE_CMD "$ELF_FILE" 2>/dev/null)
if [ $? -ne 0 ]; then
    echo "Error: Could not get size information from $ELF_FILE"
    echo "Make sure the file is a valid ELF created by xc32-gcc"
    echo "========================================="
    exit 1
fi

# Parse size output (skip header line)
SIZES=$(echo "$SIZE_OUTPUT" | tail -n 1)
TEXT=$(echo $SIZES | awk '{print $1}')
DATA=$(echo $SIZES | awk '{print $2}')
BSS=$(echo $SIZES | awk '{print $3}')

# Calculate memory usage
FLASH_USED=$((TEXT + DATA))
RAM_USED=$((DATA + BSS))

# Calculate percentages
FLASH_PERCENT=$(( (FLASH_USED * 100) / FLASH_SIZE ))
RAM_PERCENT=$(( (RAM_USED * 100) / RAM_SIZE ))

# Format numbers with commas for readability
format_number() {
    printf "%'d" "$1" 2>/dev/null || printf "%d" "$1"
}

# Display results
echo "Flash Memory (Program + Initialized Data):"
echo "  Used:  $(format_number $FLASH_USED) bytes (${FLASH_PERCENT}%)"
echo "  Free:  $(format_number $((FLASH_SIZE - FLASH_USED))) bytes"
echo "  Total: $(format_number $FLASH_SIZE) bytes (1MB)"

echo ""
echo "RAM Memory (Data + BSS):"
echo "  Used:  $(format_number $RAM_USED) bytes (${RAM_PERCENT}%)"
echo "  Free:  $(format_number $((RAM_SIZE - RAM_USED))) bytes"  
echo "  Total: $(format_number $RAM_SIZE) bytes (256KB)"

echo ""
echo "Memory Breakdown:"
echo "  .text (code):           $(format_number $TEXT) bytes"
echo "  .data (initialized):    $(format_number $DATA) bytes"
echo "  .bss (uninitialized):   $(format_number $BSS) bytes"

echo ""
# Warning messages for high usage
if [ $FLASH_PERCENT -gt 90 ]; then
    echo "⚠️  WARNING: Flash usage is very high (${FLASH_PERCENT}%)"
elif [ $FLASH_PERCENT -gt 80 ]; then
    echo "⚠️  CAUTION: Flash usage is high (${FLASH_PERCENT}%)"
else
    echo "✅ Flash usage is acceptable (${FLASH_PERCENT}%)"
fi

if [ $RAM_PERCENT -gt 90 ]; then
    echo "⚠️  WARNING: RAM usage is very high (${RAM_PERCENT}%)"
elif [ $RAM_PERCENT -gt 80 ]; then
    echo "⚠️  CAUTION: RAM usage is high (${RAM_PERCENT}%)"
else
    echo "✅ RAM usage is acceptable (${RAM_PERCENT}%)"
fi

echo "========================================="