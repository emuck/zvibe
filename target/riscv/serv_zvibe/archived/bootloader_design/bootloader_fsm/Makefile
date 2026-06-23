# Copyright (c) 2025 Martin R. Raumann
# SPDX-License-Identifier: BSD-3-Clause
################################################################################
# Makefile for MAX10 Bootloader FSM Testbench
#
# Tests the bootloader FSM that copies boot stub from UFM to RAM.
################################################################################

# Testbench files
TB_SRC = max10_bootloader_fsm_tb.sv
RTL_SRC = ../../rtl/max10_bootloader_fsm.v
UFM_MODEL = ../models/max10_ufm_model.v
RAM_MODEL = ../models/max10_ram_model.v

# Verilator options
VERILATOR = verilator
VERILATOR_FLAGS = \
	-Wall \
	-Wno-UNOPTFLAT \
	-Wno-WIDTH \
	-Wno-IMPLICIT \
	-Wno-BLKSEQ \
	-Wno-PROCASSINIT \
	-Wno-UNUSEDSIGNAL \
	-Wno-CASEINCOMPLETE \
	--cc \
	--exe \
	--main \
	--build \
	--trace \
	--timing \
	--top-module max10_bootloader_fsm_tb

# Build directory
BUILD_DIR = build_bootloader_fsm

# Default target
all: run

# Build testbench
build: $(BUILD_DIR)/Vmax10_bootloader_fsm_tb

$(BUILD_DIR)/Vmax10_bootloader_fsm_tb: $(TB_SRC) $(RTL_SRC) $(UFM_MODEL) $(RAM_MODEL)
	@echo "=========================================="
	@echo "Building Bootloader FSM Testbench"
	@echo "=========================================="
	@mkdir -p $(BUILD_DIR)
	$(VERILATOR) $(VERILATOR_FLAGS) \
		$(TB_SRC) \
		$(RTL_SRC) \
		$(UFM_MODEL) \
		$(RAM_MODEL) \
		--Mdir $(BUILD_DIR)
	@echo "Build complete!"

# Run testbench
run: build
	@echo ""
	@echo "=========================================="
	@echo "Running Bootloader FSM Testbench"
	@echo "=========================================="
	@$(BUILD_DIR)/Vmax10_bootloader_fsm_tb

# Clean
clean:
	rm -rf $(BUILD_DIR)
	rm -f *.vcd *.fst

# Help
help:
	@echo "Available targets:"
	@echo "  build  - Build testbench"
	@echo "  run    - Build and run testbench"
	@echo "  clean  - Clean build artifacts"
	@echo "  help   - Show this help"

.PHONY: all build run clean help
