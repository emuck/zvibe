# Copyright (c) 2025 Martin R. Raumann
# SPDX-License-Identifier: BSD-3-Clause
# Makefile for MAX10 08 Eval Board firmware
#
# Builds ZVibe firmware and flash images for the MAX10 platform.
# Sources are drawn from:
#   ../../common/sw/     — shared Z-machine + platform-agnostic firmware
#   ./                   — MAX10-specific flash driver and HAL
#
# Usage:
#   make firmware        — Build zvibe_riscv_multi_max10.elf/.bin
#   make flash-test      — Build max10_flash_test.bin (always rebuilds flash image)
#   make flash-test GAME=hitchhiker  — Build with specific game (default: czech)
#   make flash           — Build max10_flash.bin (multi-game)
#   make check-layout    — Preview flash layout without building image
#   make sim-dat         — Generate altera_onchip_flash.dat for Questa simulation
#   make clean           — Remove all build artifacts
#   make help            — Show this help

# ---------------------------------------------------------------------------
# Toolchain (auto-detect riscv32 vs riscv64)
# ---------------------------------------------------------------------------
ifeq ($(RISCV_PREFIX),)
  ifeq ($(shell which riscv32-unknown-elf-gcc 2>/dev/null),)
    TOOLCHAIN_PREFIX = riscv64-unknown-elf-
  else
    TOOLCHAIN_PREFIX = riscv32-unknown-elf-
  endif
else
  TOOLCHAIN_PREFIX = $(RISCV_PREFIX)
endif

CC      = $(TOOLCHAIN_PREFIX)gcc
OBJCOPY = $(TOOLCHAIN_PREFIX)objcopy
OBJDUMP = $(TOOLCHAIN_PREFIX)objdump
SIZE    = $(TOOLCHAIN_PREFIX)size

# ---------------------------------------------------------------------------
# Paths
# ---------------------------------------------------------------------------
COMMON_SW  = ../../../common/sw
ZVIBE_ROOT = ../../../../../..

# ---------------------------------------------------------------------------
# Compiler flags
# ---------------------------------------------------------------------------
ARCH_FLAGS = -march=rv32i -mabi=ilp32
OPT_FLAGS  = -O1 -g
WARN_FLAGS = -Wall -Wextra -Wno-unused-parameter
CFLAGS     = $(ARCH_FLAGS) $(OPT_FLAGS) $(WARN_FLAGS)

INCLUDES = -I$(ZVIBE_ROOT)/include -I$(COMMON_SW) -I.

# Embedded firmware profile:
# no host file I/O, no status-line callbacks, save/restore enabled.
ZVIBE_XIP_FLAGS = -DZVIBE_MINIMAL_FEATURES -DZVIBE_ENABLE_FILE_IO=0 \
                  -DZVIBE_ENABLE_STATUS_LINE=0 -DZVIBE_ENABLE_SAVE_RESTORE=1 \
                  -DMAX_GAMES=32

# Combined flags for all MAX10 compilation units
MAX10_CFLAGS = $(CFLAGS) $(INCLUDES) $(ZVIBE_XIP_FLAGS)

# ---------------------------------------------------------------------------
# ZVibe interpreter objects (compiled from ZVIBE_ROOT with XIP flags)
# ---------------------------------------------------------------------------
ZVIBE_OBJ_XIP = \
	src/api/zvibe_api_xip.o \
	src/core/zvibe_core_xip.o \
	src/core/zvibe_memory_xip.o \
	src/core/zvibe_object_xip.o \
	src/core/zvibe_ops_base_xip.o \
	src/core/zvibe_ops_math_xip.o \
	src/core/zvibe_ops_mem_xip.o \
	src/core/zvibe_ops_text_xip.o

src/api/%_xip.o: $(ZVIBE_ROOT)/src/api/%.c
	@mkdir -p src/api
	$(CC) $(MAX10_CFLAGS) -c $< -o $@

src/core/%_xip.o: $(ZVIBE_ROOT)/src/core/%.c
	@mkdir -p src/core
	$(CC) $(MAX10_CFLAGS) -c $< -o $@

# ---------------------------------------------------------------------------
# Common firmware objects (shared source, MAX10 config)
# ---------------------------------------------------------------------------
menu_system.o: $(COMMON_SW)/menu_system.c $(COMMON_SW)/menu_system.h
	$(CC) $(MAX10_CFLAGS) -c $< -o $@

game_registry_riscv.o: $(COMMON_SW)/game_registry_riscv.c $(COMMON_SW)/game_registry_riscv.h
	$(CC) $(MAX10_CFLAGS) -c $< -o $@

flash_metadata.o: $(COMMON_SW)/flash_metadata.c $(COMMON_SW)/flash_metadata.h
	$(CC) $(MAX10_CFLAGS) -c $< -o $@

flash_save.o: $(COMMON_SW)/flash_save.c $(COMMON_SW)/flash_save.h flash_save_config.h
	$(CC) $(MAX10_CFLAGS) -c $< -o $@

save_delta.o: $(COMMON_SW)/save_delta.c $(COMMON_SW)/save_delta.h
	$(CC) $(MAX10_CFLAGS) -c $< -o $@

# ---------------------------------------------------------------------------
# MAX10-specific objects (board sw directory)
# ---------------------------------------------------------------------------
flash_driver_max10_ufm.o: flash_driver_max10_ufm.c flash_driver_max10_ufm.h
	$(CC) $(MAX10_CFLAGS) -c $< -o $@

flash_hal_max10.o: flash_hal_max10.c flash_hal_max10.h flash_driver_max10_ufm.h
	$(CC) $(MAX10_CFLAGS) -c $< -o $@

# ---------------------------------------------------------------------------
# Firmware ELF — main build target
# ---------------------------------------------------------------------------
MAX10_OBJS = menu_system.o game_registry_riscv.o flash_metadata.o \
             flash_save.o flash_hal_max10.o flash_driver_max10_ufm.o save_delta.o

zvibe_riscv_multi_max10.elf: $(COMMON_SW)/zvibe_riscv_multi.c \
                              $(COMMON_SW)/zvibe_stubs.c \
                              $(COMMON_SW)/xip_start.S \
                              $(COMMON_SW)/xip_link.ld \
                              $(MAX10_OBJS) $(ZVIBE_OBJ_XIP)
	$(CC) $(MAX10_CFLAGS) \
		-nostdlib -nostartfiles -T$(COMMON_SW)/xip_link.ld \
		-Wl,--defsym=__flash_origin=0x80000100 -Wl,--gc-sections \
		-o $@ $(COMMON_SW)/xip_start.S $(COMMON_SW)/zvibe_riscv_multi.c \
		$(MAX10_OBJS) $(COMMON_SW)/zvibe_stubs.c $(ZVIBE_OBJ_XIP) -lgcc
	$(SIZE) $@

zvibe_riscv_multi_max10.bin: zvibe_riscv_multi_max10.elf
	$(OBJCOPY) -O binary $< $@

zvibe_riscv_multi_max10.dis: zvibe_riscv_multi_max10.elf
	$(OBJDUMP) -d $< > $@

# ---------------------------------------------------------------------------
# Flash image targets (invokes build_flash.py)
# ---------------------------------------------------------------------------
BUILD_FLASH = python3 ../build_flash.py

# Default game for test build (restaurant.z3 ships with repo)
GAME ?= restaurant

# Map short game IDs to filenames
GAME_FILE_plunderedhearts = plunderedhearts
GAME_FILE_hitchhiker      = hitchhiker-r60-s861002
GAME_FILE_zork1           = zork1

# Find the game file (check tests/games/ first, then games/catalog/)
GAME_BASE   = $(or $(GAME_FILE_$(GAME)),$(GAME))
GAME_PATH   = $(firstword \
	$(wildcard ../../../../../../tests/games/$(GAME_BASE).z3) \
	$(wildcard ../../../../../../tests/games/$(GAME)*.z3) \
	$(wildcard ../../../../../../games/catalog/$(GAME_BASE)*.z3) \
	$(wildcard ../../../../../../games/catalog/$(GAME)*.z3))

max10_flash_test.bin flash_layout.h max10_layout.json: zvibe_riscv_multi_max10.bin
	@if [ -z "$(GAME_PATH)" ]; then \
		echo "ERROR: Game file not found for GAME=$(GAME)"; \
		echo "  Searched tests/games/ and games/catalog/"; \
		echo "  Run: python3 ../../../../../../games/get_games.py fetch"; \
		exit 1; \
	fi
	@echo "Building MAX10 flash image: GAME=$(GAME)"
	@echo "  Firmware:  zvibe_riscv_multi_max10.bin"
	@echo "  Game file: $(GAME_PATH)"
	$(BUILD_FLASH) \
		--firmware zvibe_riscv_multi_max10.bin \
		--game "$(GAME_PATH)" \
		--game-id "$(GAME)" \
		--game-name "$(GAME)" \
		--output max10_flash_test.bin

max10_flash.bin: zvibe_riscv_multi_max10.bin
	@echo "Building MAX10 multi-game flash image"
	$(BUILD_FLASH) \
		--firmware zvibe_riscv_multi_max10.bin \
		--output max10_flash.bin

# ---------------------------------------------------------------------------
# Simulation: generate .dat for Questa vendor UFM simulation
# ---------------------------------------------------------------------------
# Generate from the COMPLETE flash image (includes ZVIF metadata at offset 0,
# firmware at offset 0x100, and game data). This is required for the firmware
# to find valid metadata at XIP address 0x80000000 during simulation.
#
# --input-target=binary: treat binary as raw bytes at address 0
# --reverse-bytes=4: swap byte order within each 32-bit word (LE→BE) so the
#   vendor UFM simulation model (which expects big-endian words) decodes
#   RISC-V instructions correctly.
altera_onchip_flash.dat: max10_flash_test.bin
	$(OBJCOPY) -I binary -O verilog --verilog-data-width=4 --reverse-bytes=4 \
		$< $@
	@echo "Generated $@ ($$(wc -l < $@) lines)"

# ---------------------------------------------------------------------------
# Top-level phony targets
# ---------------------------------------------------------------------------
.PHONY: all firmware flash-test flash check-layout sim-dat clean help

all: firmware

firmware: zvibe_riscv_multi_max10.bin

# flash-test is .PHONY so it always re-runs the flash builder, even if the
# output file exists.  This ensures GAME= changes take effect without needing
# make clean.  The firmware binary is still incrementally compiled.
flash-test: zvibe_riscv_multi_max10.bin
	@if [ -z "$(GAME_PATH)" ]; then \
		echo "ERROR: Game file not found for GAME=$(GAME)"; \
		echo "  Searched tests/games/ and games/catalog/"; \
		echo "  Run: python3 ../../../../../../games/get_games.py fetch"; \
		exit 1; \
	fi
	@echo "Building MAX10 flash image: GAME=$(GAME)"
	@echo "  Firmware:  zvibe_riscv_multi_max10.bin"
	@echo "  Game file: $(GAME_PATH)"
	$(BUILD_FLASH) \
		--firmware zvibe_riscv_multi_max10.bin \
		--game "$(GAME_PATH)" \
		--game-id "$(GAME)" \
		--game-name "$(GAME)" \
		--output max10_flash_test.bin

flash: max10_flash.bin

check-layout: zvibe_riscv_multi_max10.bin
	$(BUILD_FLASH) \
		--firmware zvibe_riscv_multi_max10.bin \
		--game "$(GAME_PATH)" \
		--game-id "$(GAME)" \
		--game-name "$(GAME)" \
		--layout-only

sim-dat: altera_onchip_flash.dat

clean:
	rm -f *.o *.elf *.bin *.dis *.hex *.map
	rm -f flash_layout.h flash_layout.ld max10_layout.json
	rm -f max10_flash_test.bin max10_flash.bin altera_onchip_flash.dat
	rm -rf src/

help:
	@echo "MAX10 Firmware Build"
	@echo "===================="
	@echo ""
	@echo "Targets:"
	@echo "  firmware       Build zvibe_riscv_multi_max10.elf/.bin"
	@echo "  flash-test           Build max10_flash_test.bin (always rebuilds; default GAME=czech)"
	@echo "  flash-test GAME=...  Build with specific game"
	@echo "  flash          Build max10_flash.bin (multi-game)"
	@echo "  check-layout   Preview flash layout (no image written)"
	@echo "  sim-dat        Generate altera_onchip_flash.dat for Questa simulation"
	@echo "  clean          Remove all build artifacts"
	@echo ""
	@echo "After building, program with:"
	@echo "  cd ../fpga && make program-complete"
	@echo "  (fpga/Makefile picks up max10_flash_test.bin from ../sw/)"
