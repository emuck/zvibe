# Copyright (c) 2025 Martin R. Raumann
# SPDX-License-Identifier: BSD-3-Clause
# ZVibe Repository-Level Makefile
#
# Convenience targets for common tasks across all targets

.PHONY: help validate test ci-source clean clean-all docs sim-tier1 sim-questa sim-xsim sim-all

help:
	@echo "ZVibe Repository Makefile"
	@echo ""
	@echo "Validation:"
	@echo "  make validate       - Console build + unit tests (matches CI)"
	@echo "  make ci-source      - Full source-change gate (skips missing toolchains)"
	@echo "  make sim-tier1      - Verilator RTL tests (requires verilator)"
	@echo ""
	@echo "Simulation:"
	@echo "  make sim-tier1      - Verilator unit tests (verilator required)"
	@echo "  make sim-questa     - Questa vendor-model tests (vsim required)"
	@echo "  make sim-xsim       - Vivado xsim tests (vivado required)"
	@echo "  make sim-all        - All simulation tiers"
	@echo ""
	@echo "Documentation:"
	@echo "  make docs           - Build Doxygen documentation"
	@echo ""
	@echo "Cleanup:"
	@echo "  make clean-all      - Clean all targets"
	@echo ""
	@echo "Individual targets:"
	@echo "  Console:  cd target/console && make"
	@echo "  Windows:  cd target/windows && make"
	@echo "  SAM E51:  cd games && ./build_games.py same51 && cd ../target/same51 && make"
	@echo "  RISC-V:   cd games && ./build_games.py arty_s7_50 && cd ../target/riscv/serv_zvibe/common/sw && make"
	@echo ""
	@echo "See docs/DEVELOPMENT.md for complete development guide"

# Validation: console build + unit tests + czech conformance suite (host subset).
# CI also runs ./scripts/ci-source-check.sh which adds integration and cross-build checks.
# RTL simulation requires verilator — run 'make sim-tier1' separately.
validate:
	@echo "Building console target..."
	@cd target/console && make clean && make
	@echo "Running console unit tests..."
	@cd target/console/tests && make test-unit
	@echo "Running czech conformance suite..."
	@cd target/console/tests && make test-czech

# Source-change validation: host tests always, embedded builds when toolchains exist.
ci-source:
	@./scripts/ci-source-check.sh

# Documentation
docs:
	@echo "Building Doxygen documentation..."
	@cd docs && doxygen Doxyfile
	@echo "Documentation built: docs/html/index.html"

SERV_ZVIBE = target/riscv/serv_zvibe

# Simulation — Tier 1 (Verilator unit tests, no external tools required)
sim-tier1:
	@./$(SERV_ZVIBE)/run_regression.sh --tier1

# Simulation — Tier 2 Questa (vsim must be on PATH, UFM IP must be generated)
sim-questa:
	@./$(SERV_ZVIBE)/run_regression.sh --tier2-questa

# Simulation — Tier 2 xsim (vivado must be on PATH)
sim-xsim:
	@./$(SERV_ZVIBE)/run_regression.sh --tier2-xsim

# Simulation — all tiers
sim-all:
	@./$(SERV_ZVIBE)/run_regression.sh --all

test: validate

clean: clean-all

# Clean all targets
clean-all:
	@echo "Cleaning all targets..."
	@cd target/console && make clean || true
	@cd target/windows && make clean || true
	@cd target/same51 && make clean || true
	@cd target/riscv/serv_zvibe/common/sw && make clean || true
	@cd target/riscv/serv_zvibe/boards/arty_s7_50/fpga && make clean || true
	@rm -rf docs/html docs/latex
	@echo "All targets cleaned"
