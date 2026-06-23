# Copyright (c) 2025 Martin R. Raumann
# SPDX-License-Identifier: BSD-3-Clause
# Makefile for Mux Unit Tests
# Tests servant_mem_mux and servant_zvibe_mux

VERILATOR = verilator
VERILATOR_FLAGS = --binary --trace --assert -Wall
VERILATOR_FLAGS += -Wno-WIDTHEXPAND -Wno-WIDTHTRUNC -Wno-UNUSEDSIGNAL
VERILATOR_FLAGS += -Wno-IMPLICITSTATIC -Wno-INITIALDLY -Wno-PROCASSINIT
VERILATOR_FLAGS += -Wno-CASEINCOMPLETE -Wno-MULTIDRIVEN -Wno-UNUSEDPARAM -Wno-BLKSEQ
VERILATOR_FLAGS += -Wno-TIMESCALEMOD -Wno-PINCONNECTEMPTY

BUILD_DIR = build_mux
RTL_DIR = ../../../rtl

# Source files
TB_MEM_MUX = servant_mem_mux_tb.sv
TB_ZVIBE_MUX = servant_zvibe_mux_tb.sv

RTL_MEM_MUX = $(RTL_DIR)/servant_mem_mux.sv
RTL_ZVIBE_MUX = $(RTL_DIR)/servant_zvibe_mux.sv

.PHONY: all clean test-mem-mux test-zvibe-mux test help

# Default target - run both tests
all: test

# Run all tests
test: test-mem-mux test-zvibe-mux
	@echo ""
	@echo "=========================================="
	@echo "ALL MUX TESTS COMPLETE"
	@echo "=========================================="

# Build and run mem_mux test
test-mem-mux: build-mem-mux
	@echo ""
	@echo "=========================================="
	@echo "Running servant_mem_mux Tests"
	@echo "=========================================="
	cd $(BUILD_DIR) && ./Vservant_mem_mux_tb
	@echo ""

build-mem-mux: $(BUILD_DIR)/Vservant_mem_mux_tb

$(BUILD_DIR)/Vservant_mem_mux_tb: $(TB_MEM_MUX) $(RTL_MEM_MUX)
	@echo "=========================================="
	@echo "Building servant_mem_mux Test (Verilator)"
	@echo "=========================================="
	@mkdir -p $(BUILD_DIR)
	$(VERILATOR) $(VERILATOR_FLAGS) \
		-Mdir $(BUILD_DIR) \
		--top-module servant_mem_mux_tb \
		--prefix Vservant_mem_mux_tb \
		$(RTL_MEM_MUX) \
		$(TB_MEM_MUX)
	@echo "Build complete!"

# Build and run zvibe_mux test
test-zvibe-mux: build-zvibe-mux
	@echo ""
	@echo "=========================================="
	@echo "Running servant_zvibe_mux Tests"
	@echo "=========================================="
	cd $(BUILD_DIR) && ./Vservant_zvibe_mux_tb
	@echo ""

build-zvibe-mux: $(BUILD_DIR)/Vservant_zvibe_mux_tb

$(BUILD_DIR)/Vservant_zvibe_mux_tb: $(TB_ZVIBE_MUX) $(RTL_ZVIBE_MUX)
	@echo "=========================================="
	@echo "Building servant_zvibe_mux Test (Verilator)"
	@echo "=========================================="
	@mkdir -p $(BUILD_DIR)
	$(VERILATOR) $(VERILATOR_FLAGS) \
		-Mdir $(BUILD_DIR) \
		--top-module servant_zvibe_mux_tb \
		--prefix Vservant_zvibe_mux_tb \
		$(RTL_ZVIBE_MUX) \
		$(TB_ZVIBE_MUX)
	@echo "Build complete!"

# Clean build artifacts
clean:
	rm -rf $(BUILD_DIR) *.vcd

# Help
help:
	@echo "Mux Unit Tests"
	@echo "=============="
	@echo ""
	@echo "Targets:"
	@echo "  make all           - Run all mux tests (default)"
	@echo "  make test          - Run all mux tests"
	@echo "  make test-mem-mux  - Run servant_mem_mux tests only"
	@echo "  make test-zvibe-mux - Run servant_zvibe_mux tests only"
	@echo "  make clean         - Remove build artifacts"
	@echo "  make help          - Show this help"
	@echo ""
	@echo "Tests:"
	@echo "  servant_mem_mux    - RAM/Flash address decode + pipelined ack"
	@echo "  servant_zvibe_mux  - Peripheral decode + case statement + pipelined ack"
