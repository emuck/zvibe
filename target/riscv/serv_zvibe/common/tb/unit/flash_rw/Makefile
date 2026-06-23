# Copyright (c) 2025 Martin R. Raumann
# SPDX-License-Identifier: BSD-3-Clause
# Makefile for S25FL R/W Model Testing (Verilator)

# Source files
TB_SRC = s25fl_rw_tb.sv
DUT_SRC = ../../models/s25fl_simple_rw.v

# Build directory
BUILD_DIR = build_flash_rw

# Verilator options
VERILATOR = verilator
VFLAGS = --binary --trace --top-module s25fl_rw_tb
VFLAGS += -Wno-fatal -Wno-WIDTH -Wno-UNOPTFLAT
VFLAGS += --timing  # Enable timing for #delay statements

# Default target
.PHONY: all
all: run

# Build testbench
.PHONY: build
build: $(BUILD_DIR)/Vs25fl_rw_tb

$(BUILD_DIR)/Vs25fl_rw_tb: $(TB_SRC) $(DUT_SRC)
	@echo "Building testbench with Verilator..."
	$(VERILATOR) $(VFLAGS) -Mdir $(BUILD_DIR) $(TB_SRC) $(DUT_SRC)
	@echo "Build complete!"

# Run simulation
.PHONY: run
run: build
	@echo ""
	@echo "Running flash R/W model tests..."
	@echo ""
	@./$(BUILD_DIR)/Vs25fl_rw_tb | tee /tmp/s25fl_rw_out.txt; \
	if grep -q "ALL TESTS PASSED" /tmp/s25fl_rw_out.txt; then \
		echo "PASS: s25fl_flash_rw_tb"; \
	else \
		echo "FAIL: s25fl_flash_rw_tb"; \
		exit 1; \
	fi

# View waveform
.PHONY: wave
wave:
	@if [ -f s25fl_rw_tb.vcd ]; then \
		gtkwave s25fl_rw_tb.vcd &; \
	else \
		echo "No waveform file found. Run 'make run' first."; \
	fi

# Clean build artifacts
.PHONY: clean
clean:
	rm -rf $(BUILD_DIR)
	rm -f s25fl_rw_tb.vcd

# Show help
.PHONY: help
help:
	@echo "Available targets:"
	@echo "  make build   - Build testbench"
	@echo "  make run     - Build and run simulation (default)"
	@echo "  make wave    - View waveform with GTKWave"
	@echo "  make clean   - Remove build artifacts"
	@echo "  make help    - Show this help message"
