# Copyright (c) 2025 Martin R. Raumann
# SPDX-License-Identifier: BSD-3-Clause
# Makefile for Arty S7-50 firmware
#
# Builds ZVibe firmware and flash images for the Arty S7-50 platform.
# Sources drawn from:
#   ../../../common/sw/  — shared Z-machine + platform-agnostic firmware
#   ./                   — Arty-specific flash driver config
#
# Usage:
#   make firmware        — Build zvibe_riscv_multi_arty.elf/.bin
#   make flash-test      — Build arty_flash_test.bin (default: restaurant)
#   make flash-test GAME=hitchhiker
#   make check-layout    — Preview flash layout without building
#   make flash-test V4=1 STORY=/path/story.z4
#                        — Version 4 story: builds the V4 firmware, sized for the story
#   make ram-kb V4=1 STORY=...  — SoC RAM (KB) the bitstream must be built with for it
#   make ram-budget V4=1 STORY=... — The RAM budget behind that number
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
ZVIBE_XIP_FLAGS = -DZVIBE_MINIMAL_FEATURES -DZVIBE_FREESTANDING=1 -DZVIBE_ENABLE_FILE_IO=0 \
                  -DZVIBE_ENABLE_STATUS_LINE=0 -DZVIBE_ENABLE_SAVE_RESTORE=1 \
                  -DMAX_GAMES=32

# ---------------------------------------------------------------------------
# Game selection (flash-test GAME=... or STORY=<file>)
# ---------------------------------------------------------------------------
V4 ?= 0
GAME ?= restaurant
GAME_FILE_plunderedhearts = plunderedhearts
GAME_FILE_hitchhiker      = hitchhiker-r60-s861002
GAME_FILE_zork1           = zork1

GAME_BASE = $(or $(GAME_FILE_$(GAME)),$(GAME))
# V4=1 prefers a version 4 story of that name (czech.z4 over czech.z3); V3 builds look for .z3 only
GAME_EXTS = $(if $(filter 1,$(V4)),z4 z3,z3)
GAME_TESTS   = ../../../../../../tests/games
GAME_CATALOG = ../../../../../../games/catalog
GAME_PATH = $(or $(STORY),$(firstword \
	$(foreach e,$(GAME_EXTS),$(wildcard $(GAME_TESTS)/$(GAME_BASE).$(e))) \
	$(foreach e,$(GAME_EXTS),$(wildcard $(GAME_TESTS)/$(GAME)*.$(e))) \
	$(foreach e,$(GAME_EXTS),$(wildcard $(GAME_CATALOG)/$(GAME_BASE)*.$(e))) \
	$(foreach e,$(GAME_EXTS),$(wildcard $(GAME_CATALOG)/$(GAME)*.$(e)))))

# ---------------------------------------------------------------------------
# Version 4 stories (V4=1)
#
# V4=1 compiles the interpreter with ZVIBE_ENABLE_V4, adds the ANSI screen layer and the
# V4 player, and sizes the firmware for the story: the story's dynamic memory lives in
# RAM, with two save buffers, so a large story needs more than the 32 KB SoC RAM. The SoC
# RAM is a bitstream parameter (ARTY_RAM_KB, see fpga/Makefile): this Makefile works out
# the size, links for it, and the FPGA build uses the same number.
#   STORY=<file.z4>   the story (default: the GAME lookup above, which prefers .z4 when V4=1)
#   ARTY_RAM_KB=<n>   override the computed RAM size (a power of two)
# V4=0 (default) is the unchanged V3 firmware: 32 KB RAM.
# ---------------------------------------------------------------------------
ARTY_RAM_KB_V3 = 32
RAM_BUDGET = python3 $(COMMON_SW)/ram_budget.py

# The story the V4 build is sized for
V4_STORY := $(GAME_PATH)

ifeq ($(V4),1)
  ifndef ARTY_RAM_KB
    ARTY_RAM_KB := $(shell $(RAM_BUDGET) kb $(V4_STORY) 2>/dev/null)
  endif
  V4_DYN      := $(shell $(RAM_BUDGET) dyn $(V4_STORY) 2>/dev/null)
  ifeq ($(or $(ARTY_RAM_KB),$(V4_DYN)),)
    $(error Cannot size the V4 build for "$(V4_STORY)": it must be a version 4 story file, and not too large (see common/sw/ram_budget.py))
  endif
  ZVIBE_V4_FLAGS = -DZVIBE_ENABLE_V4=1 -DZVIBE_MEMORY_BUFFER_SIZE=$(V4_DYN)
  V4_OBJS     = src/screen/zvibe_screen_xip.o src/core/zvibe_ops_v4_xip.o zvibe_riscv_v4.o
else
  ARTY_RAM_KB = $(ARTY_RAM_KB_V3)
  V4_DYN      =
  ZVIBE_V4_FLAGS =
  V4_OBJS     =
endif
RAM_BYTES := $(shell echo $$(( $(ARTY_RAM_KB) * 1024 )))

# Objects are not interchangeable between modes: switching V4, the story's size or the RAM
# size starts from a clean tree.
BUILD_MODE := v4=$(V4) dyn=$(V4_DYN) ram=$(ARTY_RAM_KB)
ifneq ($(shell cat .build_mode 2>/dev/null),$(BUILD_MODE))
  $(shell rm -rf *.o src *.elf *.bin *.dis *.map; echo '$(BUILD_MODE)' > .build_mode)
endif

# Combined flags for all Arty compilation units
ARTY_CFLAGS = $(CFLAGS) $(INCLUDES) $(ZVIBE_XIP_FLAGS) $(ZVIBE_V4_FLAGS)

# ---------------------------------------------------------------------------
# ZVibe interpreter objects (XIP variants)
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
	$(CC) $(ARTY_CFLAGS) -c $< -o $@

src/core/%_xip.o: $(ZVIBE_ROOT)/src/core/%.c
	@mkdir -p src/core
	$(CC) $(ARTY_CFLAGS) -c $< -o $@

src/screen/%_xip.o: $(ZVIBE_ROOT)/src/screen/%.c
	@mkdir -p src/screen
	$(CC) $(ARTY_CFLAGS) -c $< -o $@

zvibe_riscv_v4.o: $(COMMON_SW)/zvibe_riscv_v4.c $(COMMON_SW)/zvibe_riscv_v4.h flash_save_config.h
	$(CC) $(ARTY_CFLAGS) -c $< -o $@

# ---------------------------------------------------------------------------
# Common firmware objects (shared source, Arty config)
# ---------------------------------------------------------------------------
menu_system.o: $(COMMON_SW)/menu_system.c $(COMMON_SW)/menu_system.h
	$(CC) $(ARTY_CFLAGS) -c $< -o $@

game_registry_riscv.o: $(COMMON_SW)/game_registry_riscv.c $(COMMON_SW)/game_registry_riscv.h
	$(CC) $(ARTY_CFLAGS) -c $< -o $@

flash_metadata.o: $(COMMON_SW)/flash_metadata.c $(COMMON_SW)/flash_metadata.h flash_save_config.h
	$(CC) $(ARTY_CFLAGS) -c $< -o $@

flash_save.o: $(COMMON_SW)/flash_save.c $(COMMON_SW)/flash_save.h flash_save_config.h
	$(CC) $(ARTY_CFLAGS) -c $< -o $@

flash_driver.o: flash_driver.c flash_save_config.h
	$(CC) $(ARTY_CFLAGS) -c $< -o $@

save_delta.o: $(COMMON_SW)/save_delta.c $(COMMON_SW)/save_delta.h
	$(CC) $(ARTY_CFLAGS) -c $< -o $@

# ---------------------------------------------------------------------------
# Firmware ELF
# ---------------------------------------------------------------------------
ARTY_OBJS = menu_system.o game_registry_riscv.o flash_metadata.o \
            flash_save.o flash_driver.o save_delta.o

# Firmware entry: 0x80100100 (0x80100000 = ZVIF header, +0x100 = firmware)
zvibe_riscv_multi_arty.elf: $(COMMON_SW)/zvibe_riscv_multi.c \
                              $(COMMON_SW)/zvibe_stubs.c \
                              $(COMMON_SW)/xip_start.S \
                              $(COMMON_SW)/xip_link.ld \
                              $(ARTY_OBJS) $(V4_OBJS) $(ZVIBE_OBJ_XIP)
	$(CC) $(ARTY_CFLAGS) \
		-nostdlib -nostartfiles -T$(COMMON_SW)/xip_link.ld \
		-Wl,--defsym=__flash_origin=0x80100100 -Wl,--defsym=__ram_size=$(RAM_BYTES) \
		-Wl,--gc-sections \
		-o $@ $(COMMON_SW)/xip_start.S $(COMMON_SW)/zvibe_riscv_multi.c \
		$(ARTY_OBJS) $(V4_OBJS) $(COMMON_SW)/zvibe_stubs.c $(ZVIBE_OBJ_XIP) -lgcc
	$(SIZE) $@
	@echo "SoC RAM this firmware is linked for: $(ARTY_RAM_KB) KB (the bitstream must be built with ARTY_RAM_KB=$(ARTY_RAM_KB))"

zvibe_riscv_multi_arty.bin: zvibe_riscv_multi_arty.elf
	$(OBJCOPY) -O binary $< $@

zvibe_riscv_multi_arty.dis: zvibe_riscv_multi_arty.elf
	$(OBJDUMP) -d $< > $@

# ---------------------------------------------------------------------------
# Flash image (invokes build_flash.py)
# ---------------------------------------------------------------------------
BUILD_FLASH = python3 ../build_flash.py

arty_flash_test.bin: zvibe_riscv_multi_arty.bin
	@if [ -z "$(GAME_PATH)" ]; then \
		echo "ERROR: Game file not found for GAME=$(GAME)"; \
		echo "  Searched tests/games/ and games/catalog/"; \
		echo "  Run: python3 ../../../../../../games/get_games.py fetch"; \
		exit 1; \
	fi
	@echo "Building Arty flash image: GAME=$(GAME)"
	@echo "  Firmware:  zvibe_riscv_multi_arty.bin"
	@echo "  Game file: $(GAME_PATH)"
	$(BUILD_FLASH) \
		--firmware zvibe_riscv_multi_arty.bin \
		--game "$(GAME_PATH)" \
		--game-id "$(GAME)" \
		--game-name "$(GAME)" \
		--output arty_flash_test.bin

# Multi-game flash image (all riscv-tagged games from registry)
arty_flash_all.bin: zvibe_riscv_multi_arty.bin
	@echo "Building Arty multi-game flash image (all catalog games)"
	$(BUILD_FLASH) \
		--firmware zvibe_riscv_multi_arty.bin \
		--games-from-registry \
		--output arty_flash_all.bin

# ---------------------------------------------------------------------------
# Top-level phony targets
# ---------------------------------------------------------------------------
.PHONY: all firmware flash-test all-games check-layout clean help ram-kb ram-budget

all: firmware

firmware: zvibe_riscv_multi_arty.bin

# flash-test is .PHONY so it always re-runs the flash builder, even if the
# output file exists.  This ensures GAME= changes take effect without needing
# make clean.  The firmware binary is still incrementally compiled.
flash-test: zvibe_riscv_multi_arty.bin
	@if [ -z "$(GAME_PATH)" ]; then \
		echo "ERROR: Game file not found for GAME=$(GAME)"; \
		echo "  Searched tests/games/ and games/catalog/"; \
		echo "  Run: python3 ../../../../../../games/get_games.py fetch"; \
		exit 1; \
	fi
	@echo "Building Arty flash image: GAME=$(GAME)"
	@echo "  Firmware:  zvibe_riscv_multi_arty.bin"
	@echo "  Game file: $(GAME_PATH)"
	$(BUILD_FLASH) \
		--firmware zvibe_riscv_multi_arty.bin \
		--game "$(GAME_PATH)" \
		--game-id "$(GAME)" \
		--game-name "$(GAME)" \
		--output arty_flash_test.bin

all-games: arty_flash_all.bin

check-layout: zvibe_riscv_multi_arty.bin
	$(BUILD_FLASH) \
		--firmware zvibe_riscv_multi_arty.bin \
		--game "$(GAME_PATH)" \
		--game-id "$(GAME)" \
		--game-name "$(GAME)" \
		--layout-only

# SoC RAM (KB) the bitstream must be built with for this firmware: 32 for V3
ram-kb:
	@echo $(ARTY_RAM_KB)

ram-budget:
ifeq ($(V4),1)
	@$(RAM_BUDGET) show $(V4_STORY)
else
	@echo "V3 firmware: $(ARTY_RAM_KB) KB SoC RAM"
endif

clean:
	rm -f .build_mode
	rm -f *.o *.elf *.bin *.dis *.hex *.map
	rm -f flash_layout.h arty_layout.json arty_flash_test.bin
	rm -rf src/

help:
	@echo "Arty S7-50 Firmware Build"
	@echo "========================="
	@echo ""
	@echo "Targets:"
	@echo "  firmware       Build zvibe_riscv_multi_arty.elf/.bin"
	@echo "  flash-test     Build arty_flash_test.bin (default: restaurant, ships with repo)"
	@echo "  flash-test GAME=hitchhiker  Build with Hitchhiker's Guide"
	@echo "  all-games      Build arty_flash_all.bin with all catalog games"
	@echo "  check-layout   Preview flash layout (no image written)"
	@echo "  clean          Remove all build artifacts"
	@echo ""
	@echo "After building, program with:"
	@echo "  cd ../fpga && make program-complete"
	@echo "  (fpga/Makefile picks up arty_flash_test.bin from ../sw/)"
