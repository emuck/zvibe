# Copyright (c) 2025 Martin R. Raumann
# SPDX-License-Identifier: BSD-3-Clause
# Makefile for Servant ZVibe Flash Write Integration Testbench
# Uses Verilator for simulation
#
# Tests: s25fl_write.sv + qspi_mux.sv arbitration + s25fl_xip.sv read-back
# in a full SoC context (CPU executing XIP, writes flash, verifies via XIP).
#
# Usage:
#   make          — build firmware + testbench, run, emit PASS:/FAIL:
#   make clean    — remove build artifacts

VERILATOR = verilator
VERILATOR_FLAGS = --binary -Wall
VERILATOR_FLAGS += -Wno-WIDTHEXPAND -Wno-WIDTHTRUNC -Wno-UNUSEDSIGNAL
VERILATOR_FLAGS += -Wno-IMPLICITSTATIC -Wno-INITIALDLY -Wno-BLKSEQ
VERILATOR_FLAGS += -Wno-GENUNNAMED -Wno-PROCASSINIT
VERILATOR_FLAGS += -Wno-CASEINCOMPLETE -Wno-TIMESCALEMOD
VERILATOR_FLAGS += -Wno-UNOPTFLAT -Wno-MULTIDRIVEN -Wno-PINCONNECTEMPTY
VERILATOR_FLAGS += -Wno-SYNCASYNCNET
VERILATOR_FLAGS += --Wno-fatal

RTL_DIR = ../../../../common/rtl
QSPI_DIR = $(RTL_DIR)/qspi
MODEL_DIR = ../../../../common/tb/models
BUILD_DIR = build

SERV_DIR = ../../../../serv/rtl
SERV_SERVILE_DIR = ../../../../serv/servile
SERV_SERVANT_DIR = ../../../../serv/servant

RTL_SRCS = $(RTL_DIR)/servant_zvibe.sv \
           $(RTL_DIR)/servant_mem_mux.sv \
           $(RTL_DIR)/servant_zvibe_mux.sv \
           $(RTL_DIR)/servant_ram.sv \
           $(SERV_SERVANT_DIR)/servant_timer.v \
           $(RTL_DIR)/gpio_leds.sv \
           $(RTL_DIR)/uart/uart_wb.sv \
           $(RTL_DIR)/uart/uart_rx.sv \
           $(RTL_DIR)/uart/uart_tx.sv \
           $(RTL_DIR)/uart/fifo_sync.sv \
           $(SERV_SERVILE_DIR)/servile.v \
           $(RTL_DIR)/servile_mux.sv \
           $(SERV_SERVILE_DIR)/servile_arbiter.v \
           $(QSPI_DIR)/s25fl_xip.sv \
           $(QSPI_DIR)/s25fl_write.sv \
           $(QSPI_DIR)/qspi_mux.sv \
           $(MODEL_DIR)/s25fl_simple_rw.v \
           $(SERV_DIR)/serv_top.v \
           $(SERV_DIR)/serv_state.v \
           $(SERV_DIR)/serv_decode.v \
           $(SERV_DIR)/serv_ctrl.v \
           $(SERV_DIR)/serv_alu.v \
           $(SERV_DIR)/serv_rf_top.v \
           $(SERV_DIR)/serv_rf_if.v \
           $(SERV_DIR)/serv_rf_ram.v \
           $(SERV_DIR)/serv_rf_ram_if.v \
           $(SERV_DIR)/serv_mem_if.v \
           $(SERV_DIR)/serv_csr.v \
           $(SERV_DIR)/serv_immdec.v \
           $(SERV_DIR)/serv_aligner.v \
           $(SERV_DIR)/serv_bufreg.v \
           $(SERV_DIR)/serv_bufreg2.v \
           $(SERV_DIR)/serv_compdec.v

TB_SRC = servant_zvibe_flash_write_tb.sv
SW_DIR = ../../../../common/sw
TEST_FW_HEX = flash_write_xip_test.hex

.PHONY: all run build build-xip check-serv clean help

# Default: build firmware + testbench + run with PASS:/FAIL: output
all: run

# ---------------------------------------------------------------------------
# Firmware build
# ---------------------------------------------------------------------------
build-xip:
	@echo "Building $(TEST_FW_HEX) firmware..."
	$(MAKE) -C $(SW_DIR) $(TEST_FW_HEX)
	@mkdir -p $(BUILD_DIR)
	sed 's/@00000000/@00100000/' $(SW_DIR)/$(TEST_FW_HEX) > $(BUILD_DIR)/test_firmware.hex
	@echo "✓ Test firmware prepared (entry at flash phys 0x100100)"

# ---------------------------------------------------------------------------
# SERV submodule check
# ---------------------------------------------------------------------------
check-serv:
	@if [ ! -f $(SERV_DIR)/serv_top.v ]; then \
		echo "Initializing SERV submodule..."; \
		git submodule update --init target/riscv/serv_zvibe/serv; \
	fi

# ---------------------------------------------------------------------------
# Testbench build
# ---------------------------------------------------------------------------
build: check-serv $(BUILD_DIR)/Vservant_zvibe_flash_write_tb

$(BUILD_DIR)/Vservant_zvibe_flash_write_tb: $(TB_SRC) $(RTL_SRCS)
	@echo "Building Flash Write testbench..."
	@mkdir -p $(BUILD_DIR)
	$(VERILATOR) $(VERILATOR_FLAGS) \
		-Mdir $(BUILD_DIR) \
		--top-module servant_zvibe_flash_write_tb \
		$(RTL_SRCS) \
		$(TB_SRC)
	@echo "Build complete."

# ---------------------------------------------------------------------------
# Run: build firmware + testbench, run sim, emit PASS:/FAIL:
# ---------------------------------------------------------------------------
run: build-xip build
	@echo ""
	@echo "Running Flash Write Integration Test..."
	@cd $(BUILD_DIR) && ./Vservant_zvibe_flash_write_tb | tee /tmp/flash_write_out.txt; \
	if grep -q "FIRMWARE TEST PASSED" /tmp/flash_write_out.txt; then \
		echo "PASS: arty_soc_flash_write_tb"; \
	else \
		echo "FAIL: arty_soc_flash_write_tb"; \
		exit 1; \
	fi

# ---------------------------------------------------------------------------
clean:
	rm -rf $(BUILD_DIR) *.vcd

help:
	@echo "Flash Write Integration Testbench"
	@echo "  make       - build firmware + testbench + run (PASS:/FAIL: output)"
	@echo "  make clean - remove build artifacts"
