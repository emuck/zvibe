# Copyright (c) 2025 Martin R. Raumann
# SPDX-License-Identifier: BSD-3-Clause
# Makefile for Servant ZVibe FPGA Build (Arty S7-50)

DESIGN = servant_zvibe_arty_s7_50
BUILD_DIR = build
VIVADO = vivado
OPENFPGALOADER = openFPGALoader
BOARD = arty_s7_50

# Board-local software directory
SW_DIR = ../sw

# Game selection (matches key in games/registry.json)
GAME ?= restaurant

# Firmware + flash image (built by ../sw/Makefile)
# Override on command line for multi-game: make program-complete ARTY_FLASH=../sw/arty_flash_all.bin
ARTY_FLASH ?= $(SW_DIR)/arty_flash_test.bin

# RTL source files (using wildcard for proper dependency tracking)
BOARD_RTL      = $(wildcard ../rtl/*.v) $(wildcard ../rtl/*.sv)
COMMON_RTL_V   = $(wildcard ../../../common/rtl/*.v)
COMMON_RTL_SV  = $(wildcard ../../../common/rtl/*.sv)
QSPI_RTL       = $(wildcard ../../../common/rtl/qspi/*.v) $(wildcard ../../../common/rtl/qspi/*.sv)
SERV_RTL       = $(wildcard ../../../serv/rtl/*.v)

# All RTL dependencies
RTL_SOURCES = $(BOARD_RTL) $(COMMON_RTL_V) $(COMMON_RTL_SV) $(QSPI_RTL) $(SERV_RTL)

# Constraint files
CONSTRAINTS = $(wildcard *.xdc)

# All dependencies for bitstream build
BITSTREAM_DEPS = build.tcl $(RTL_SOURCES) $(CONSTRAINTS)

.PHONY: all build clean program program-complete program-flash program-sram \
        reports show-deps help

all: build

# Build bitstream
build: $(BUILD_DIR)/$(DESIGN).bit

$(BUILD_DIR)/$(DESIGN).bit: $(BITSTREAM_DEPS)
	@echo "=========================================="
	@echo "Starting Vivado Build..."
	@echo "=========================================="
	@echo "Dependencies changed:"
	@echo "  RTL files: $(words $(RTL_SOURCES)) files"
	@echo "  Constraints: $(words $(CONSTRAINTS)) files"
	@echo "=========================================="
	$(VIVADO) -mode batch -source build.tcl -notrace
	@echo ""
	@echo "=========================================="
	@echo "Build Complete!"
	@echo "Bitstream: $(BUILD_DIR)/$(DESIGN).bit"
	@echo "=========================================="

# One-command full programming: bitstream + user flash region
program-complete: $(BUILD_DIR)/$(DESIGN).bit
	@echo "=========================================="
	@echo "Programming Arty S7-50 (complete)..."
	@echo "=========================================="
	@echo "Building firmware and flash image (GAME=$(GAME))..."
	@$(MAKE) -C $(SW_DIR) flash-test GAME=$(GAME)
	@echo ""
	@echo "Step 1: Programming bitstream to flash offset 0x000000..."
	$(OPENFPGALOADER) -b $(BOARD) -f $(BUILD_DIR)/$(DESIGN).bit
	@echo ""
	@echo "Step 2: Programming ZVIF flash image to offset 0x100000..."
	$(OPENFPGALOADER) -b $(BOARD) -f --offset 0x100000 $(ARTY_FLASH)
	@echo ""
	@echo "Step 3: Reloading ZVibe bitstream into FPGA SRAM for clean reset..."
	@echo "(openFPGALoader -f leaves spiOverJtag in SRAM; this restores ZVibe)"
	$(OPENFPGALOADER) -b $(BOARD) $(BUILD_DIR)/$(DESIGN).bit
	@echo ""
	@echo "=========================================="
	@echo "Programming complete!"
	@echo "Board is running ZVibe."
	@echo "Connect to UART: picocom -b 115200 /dev/ttyUSB1  # or ttyUSB2 depending on other devices"
	@echo "=========================================="

# Program only the user flash region (fast update when only firmware/game changes)
# NOTE: openFPGALoader -f leaves spiOverJtag in FPGA SRAM after the write.
# Follow with 'make program-sram' to reload ZVibe, or power cycle to boot from flash.
program-flash: $(ARTY_FLASH)
	@echo "=========================================="
	@echo "Programming user flash region (offset 0x100000)..."
	@echo "Image: $(ARTY_FLASH)"
	@echo "=========================================="
	$(OPENFPGALOADER) -b $(BOARD) -f --offset 0x100000 $(ARTY_FLASH)
	@echo "Done. Run 'make program-sram' to boot immediately, or power cycle."

# Program FPGA SRAM only (temporary - lost on power cycle)
program-sram: $(BUILD_DIR)/$(DESIGN).bit
	@echo "Programming FPGA SRAM (temporary)..."
	$(OPENFPGALOADER) -b $(BOARD) $(BUILD_DIR)/$(DESIGN).bit

# Alias for backwards compatibility
program: program-sram

# Show reports
reports:
	@echo "\n=========================================="
	@echo "Utilization (Post-Synthesis)"
	@echo "=========================================="
	@cat $(BUILD_DIR)/utilization_synth.txt 2>/dev/null || echo "Report not found. Run 'make build' first."
	@echo "\n=========================================="
	@echo "Utilization (Post-Implementation)"
	@echo "=========================================="
	@cat $(BUILD_DIR)/utilization_impl.txt 2>/dev/null || echo "Report not found. Run 'make build' first."
	@echo "\n=========================================="
	@echo "Timing Summary"
	@echo "=========================================="
	@cat $(BUILD_DIR)/timing_summary.txt 2>/dev/null || echo "Report not found. Run 'make build' first."

# Show all dependencies (useful for debugging)
show-deps:
	@echo "=========================================="
	@echo "FPGA Build Dependencies"
	@echo "=========================================="
	@echo "Board RTL ($(words $(BOARD_RTL)) files):"
	@$(foreach f,$(BOARD_RTL),echo "  $(f)";)
	@echo ""
	@echo "Common RTL Verilog ($(words $(COMMON_RTL_V)) files):"
	@$(foreach f,$(COMMON_RTL_V),echo "  $(f)";)
	@echo ""
	@echo "Common RTL SystemVerilog ($(words $(COMMON_RTL_SV)) files):"
	@$(foreach f,$(COMMON_RTL_SV),echo "  $(f)";)
	@echo ""
	@echo "QSPI RTL ($(words $(QSPI_RTL)) files):"
	@$(foreach f,$(QSPI_RTL),echo "  $(f)";)
	@echo ""
	@echo "SERV CPU RTL ($(words $(SERV_RTL)) files):"
	@$(foreach f,$(SERV_RTL),echo "  $(f)";)
	@echo ""
	@echo "Constraints ($(words $(CONSTRAINTS)) files):"
	@$(foreach f,$(CONSTRAINTS),echo "  $(f)";)
	@echo ""
	@echo "Flash image: $(ARTY_FLASH)"
	@echo "=========================================="

# Clean build artifacts
clean:
	rm -rf $(BUILD_DIR)
	rm -f *.jou *.log *.str
	rm -rf .Xil

# Help
help:
	@echo "Servant ZVibe FPGA Build System (Arty S7-50)"
	@echo "============================================="
	@echo ""
	@echo "Targets:"
	@echo "  build              Build bitstream (default)"
	@echo "  program-complete   Program bitstream + ZVIF flash image (one command)"
	@echo "  program-flash      Program user flash region only (fast firmware update)"
	@echo "  program-sram       Program FPGA SRAM only (temporary)"
	@echo "  reports            Show timing and utilization reports"
	@echo "  show-deps          Show all RTL and constraint dependencies"
	@echo "  clean              Remove build artifacts"
	@echo "  help               Show this help"
	@echo ""
	@echo "Firmware (build separately in ../sw/):"
	@echo "  cd ../sw && make flash-test            # restaurant (default, ships with repo)"
	@echo "  cd ../sw && make flash-test GAME=zork1 # Different game"
	@echo "  cd ../sw && make check-layout          # Preview flash layout"
	@echo ""
	@echo "Target FPGA: Arty S7-50 (XC7S50-CSGA324-1)"
	@echo "Flash:       S25FL128S/S25FL127S (16MB QSPI)"
	@echo ""
	@echo "Flash Layout (ZVIF-based, offsets from QSPI physical 0):"
	@echo "  0x000000  FPGA Bitstream (up to ~4MB)"
	@echo "  0x100000  ZVIF metadata header (256 bytes)"
	@echo "  0x100100  Firmware entry point (~34KB)"
	@echo "  varies    ZVGM game TOC"
	@echo "  varies    Game data"
	@echo "  varies    Save slots (64KB each)"
	@echo ""
	@echo "Exact offsets are computed by ../sw/build_flash.py and"
	@echo "stored in the ZVIF header read by firmware at boot."
