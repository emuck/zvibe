# Copyright (c) 2025 Martin R. Raumann
# SPDX-License-Identifier: BSD-3-Clause
# Makefile for MAX10 Board-Level XIP Testbench

# Paths
TB_DIR = .
BOARD_RTL = ../rtl
COMMON_RTL = ../../../common/rtl
UFM_RTL = $(COMMON_RTL)/ufm
SERV_DIR = ../../../serv
TB_MODELS = ../../../common/tb/models
IP_DIR = ../fpga/ip

# Verilator flags
VERILATOR = verilator
VFLAGS = --binary --trace --timing --build -Wall
VFLAGS += -Wno-WIDTHTRUNC -Wno-WIDTHEXPAND
VFLAGS += -Wno-PINCONNECTEMPTY -Wno-UNUSEDSIGNAL -Wno-TIMESCALEMOD
VFLAGS += -Wno-GENUNNAMED -Wno-BLKSEQ -Wno-PROCASSINIT -Wno-CASEINCOMPLETE
VFLAGS += -Wno-UNUSEDPARAM -Wno-UNDRIVEN -Wno-REDEFMACRO
VFLAGS += -Wno-INITIALDLY  # Allow initial delays for simulation
VFLAGS += -Wno-DEFPARAM    # altpll uses defparam (vendor primitive, synthesis-only)
VFLAGS += --top servant_zvibe_max10_08_eval_xip_tb
VFLAGS += +define+UFM_BEHAVIORAL_MODEL  # Use behavioral UFM with hardcoded Hello! test
VFLAGS += +define+SIMULATION            # Enable simulation-only ports in board wrapper

# Source files
SOURCES = servant_zvibe_max10_08_eval_xip_tb.sv

# Board top-level
SOURCES += $(BOARD_RTL)/servant_zvibe_max10_08_eval_xip.sv

# Common RTL
SOURCES += $(COMMON_RTL)/servant_zvibe.sv
SOURCES += $(COMMON_RTL)/servant_zvibe_mux.sv
SOURCES += $(COMMON_RTL)/servant_mem_mux.sv
SOURCES += $(COMMON_RTL)/servant_ram.sv
SOURCES += $(COMMON_RTL)/gpio_leds.sv
SOURCES += $(COMMON_RTL)/servile_mux.sv

# UART (SystemVerilog)
SOURCES += $(COMMON_RTL)/uart/uart_wb.sv
SOURCES += $(COMMON_RTL)/uart/uart_tx.sv
SOURCES += $(COMMON_RTL)/uart/uart_rx.sv
SOURCES += $(COMMON_RTL)/uart/fifo_sync.sv

# UFM Unified Controller (replaces max10_ufm_xip.v + wb_to_avalon_mm.v)
SOURCES += $(UFM_RTL)/max10_ufm_unified.sv

# QSPI Flash Mimic (for servant_zvibe.sv)
SOURCES += $(COMMON_RTL)/qspi/wb_ram_flash_mimic.sv

# IP Cores - Simulation Models (replace Intel IP cores with behavioral models)
SOURCES += $(TB_DIR)/ufm_sim.sv          # UFM simulation model
SOURCES += ../sim/altpll_model.sv         # ALTPLL simulation model

# SERV CPU
SOURCES += $(SERV_DIR)/servile/servile.v
SOURCES += $(SERV_DIR)/servile/servile_arbiter.v
SOURCES += $(SERV_DIR)/servant/servant_timer.v
SOURCES += $(SERV_DIR)/rtl/serv_top.v
SOURCES += $(SERV_DIR)/rtl/serv_state.v
SOURCES += $(SERV_DIR)/rtl/serv_decode.v
SOURCES += $(SERV_DIR)/rtl/serv_ctrl.v
SOURCES += $(SERV_DIR)/rtl/serv_alu.v
SOURCES += $(SERV_DIR)/rtl/serv_rf_top.v
SOURCES += $(SERV_DIR)/rtl/serv_rf_if.v
SOURCES += $(SERV_DIR)/rtl/serv_rf_ram.v
SOURCES += $(SERV_DIR)/rtl/serv_rf_ram_if.v
SOURCES += $(SERV_DIR)/rtl/serv_mem_if.v
SOURCES += $(SERV_DIR)/rtl/serv_csr.v
SOURCES += $(SERV_DIR)/rtl/serv_immdec.v
SOURCES += $(SERV_DIR)/rtl/serv_aligner.v
SOURCES += $(SERV_DIR)/rtl/serv_bufreg.v
SOURCES += $(SERV_DIR)/rtl/serv_bufreg2.v
SOURCES += $(SERV_DIR)/rtl/serv_compdec.v

# Include paths
INCLUDES = -I$(COMMON_RTL) -I$(UFM_RTL) -I$(SERV_DIR)/rtl

.PHONY: all clean run wave help

all: obj_dir/Vservant_zvibe_max10_08_eval_xip_tb

obj_dir/Vservant_zvibe_max10_08_eval_xip_tb: $(SOURCES)
	$(VERILATOR) $(VFLAGS) $(INCLUDES) $(SOURCES)

run: obj_dir/Vservant_zvibe_max10_08_eval_xip_tb
	@echo "========================================="
	@echo "Running Board-Level XIP Testbench (100MHz PLL)"
	@echo "========================================="
	./obj_dir/Vservant_zvibe_max10_08_eval_xip_tb +verilator+rand+reset+0


wave: servant_zvibe_max10_08_eval_xip_tb.vcd
	gtkwave servant_zvibe_max10_08_eval_xip_tb.vcd &

clean:
	rm -rf obj_dir servant_zvibe_max10_08_eval_xip_tb.vcd

help:
	@echo "MAX10 Board-Level XIP Testbench"
	@echo "================================"
	@echo ""
	@echo "Targets:"
	@echo "  all        - Build testbench"
	@echo "  run        - Run simulation (100MHz PLL, behavioral UFM Hello! test)"
	@echo "  wave       - View waveform with GTKWave"
	@echo "  clean      - Remove build artifacts"
	@echo ""
	@echo "This testbench simulates the complete top-level design:"
	@echo "  - External 50MHz clock input"
	@echo "  - PLL (50MHz → 100MHz) or Internal OSC (116MHz)"
	@echo "  - Power-on reset sequencing"
	@echo "  - Complete UFM integration path"
	@echo "  - Pin-level UART and GPIO interfaces"
	@echo ""
