#!/bin/bash
# Copyright (c) 2025 Martin R. Raumann
# SPDX-License-Identifier: BSD-3-Clause
# Generate UFM IP Core with Simulation and Synthesis Files
# This script generates the UFM IP from ufm.qsys for both FPGA builds and simulation

set -e  # Exit on error

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

# Paths
QSYS_FILE="ufm.qsys"
OUTPUT_DIR="ip/ufm"
# Try to find qsys-generate automatically if not set
if [ -z "$QSYS_GENERATE" ]; then
    # Try common locations
    for path in \
        "/mnt/kingston/Altera/quartus/sopc_builder/bin/qsys-generate" \
        "/mnt/kingston/altera_lite/25.1std/quartus/sopc_builder/bin/qsys-generate" \
        "$(which qsys-generate 2>/dev/null)"; do
        if [ -x "$path" ]; then
            QSYS_GENERATE="$path"
            break
        fi
    done
fi
QPF_FILE="servant_zvibe_max10_08_eval_xip.qpf"
REV="servant_zvibe_max10_08_eval_xip"

# Device configuration
FAMILY="MAX 10"
PART="10M08SAE144C8G"

echo "=========================================="
echo "UFM IP Core Generation Script"
echo "=========================================="
echo "Qsys file: $QSYS_FILE"
echo "Output:    $OUTPUT_DIR"
echo "Device:    $PART ($FAMILY)"
echo ""

# Check if qsys-generate exists
if [ ! -x "$QSYS_GENERATE" ]; then
    echo "ERROR: qsys-generate not found at: $QSYS_GENERATE"
    echo ""
    echo "Please set QSYS_GENERATE environment variable to the correct path, or update this script."
    echo "Example: export QSYS_GENERATE=/path/to/quartus/sopc_builder/bin/qsys-generate"
    exit 1
fi

# Check if ufm.qsys exists
if [ ! -f "$QSYS_FILE" ]; then
    echo "ERROR: $QSYS_FILE not found"
    echo "This script must be run from the fpga/ directory containing ufm.qsys"
    exit 1
fi

# Create output directory if it doesn't exist
mkdir -p "$OUTPUT_DIR"

echo "Step 1/3: Generating simulation models..."
echo "----------------------------------------"
$QSYS_GENERATE "$SCRIPT_DIR/$QSYS_FILE" \
    --simulation=VERILOG \
    --allow-mixed-language-simulation \
    --output-directory="$SCRIPT_DIR/$OUTPUT_DIR"

if [ $? -ne 0 ]; then
    echo "ERROR: Simulation generation failed"
    exit 1
fi
echo "✓ Simulation models generated"
echo ""

echo "Step 2/3: Generating synthesis files..."
echo "----------------------------------------"
$QSYS_GENERATE "$SCRIPT_DIR/$QSYS_FILE" \
    --synthesis=VERILOG \
    --output-directory="$SCRIPT_DIR/$OUTPUT_DIR"

if [ $? -ne 0 ]; then
    echo "ERROR: Synthesis generation failed"
    exit 1
fi
echo "✓ Synthesis files generated"
echo ""

echo "Step 3/3: Generating block symbol file..."
echo "----------------------------------------"
$QSYS_GENERATE "$SCRIPT_DIR/$QSYS_FILE" \
    --block-symbol-file \
    --output-directory="$SCRIPT_DIR/$OUTPUT_DIR"

if [ $? -ne 0 ]; then
    echo "ERROR: Block symbol generation failed"
    exit 1
fi
echo "✓ Block symbol file generated"
echo ""

echo "=========================================="
echo "UFM IP Generation Complete!"
echo "=========================================="
echo ""
echo "Generated files:"
echo "  $OUTPUT_DIR/simulation/submodules/  - Verilog simulation models"
echo "  $OUTPUT_DIR/synthesis/submodules/   - Synthesis Verilog files"
echo "  $OUTPUT_DIR/synthesis/ufm.qip       - Quartus IP file (for build.tcl)"
echo "  $OUTPUT_DIR/ufm.bsf                 - Block symbol file"
echo ""
echo "Simulation models are ready for Questa/ModelSim."
echo "Synthesis files are ready for FPGA builds."
echo ""
echo "NOTE: Generated files are excluded from git (.gitignore)"
echo "      Run this script after cloning the repository."
