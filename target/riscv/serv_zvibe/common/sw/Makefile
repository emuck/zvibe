# Copyright (c) 2025 Martin R. Raumann
# SPDX-License-Identifier: BSD-3-Clause
# Makefile for Servant ZVibe common firmware
#
# Primary use: build UART echo XIP binaries for simulation.
# Also provides Arty S7-50 multi-game firmware (quick build; prefer
# boards/arty_s7_50/sw/Makefile for production builds).
#
# MAX10 firmware: cd boards/max10_08_eval/sw && make firmware
# Arty firmware:  cd boards/arty_s7_50/sw   && make firmware

# Auto-detect RISC-V toolchain (riscv32 or riscv64)
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

# Compiler flags for RV32I (SERV target)
ARCH_FLAGS = -march=rv32i -mabi=ilp32
OPT_FLAGS  = -O1 -g
WARN_FLAGS = -Wall -Wextra -Wno-unused-parameter
CFLAGS = $(ARCH_FLAGS) $(OPT_FLAGS) $(WARN_FLAGS)

# Include paths
ZVIBE_ROOT = ../../../../..
INCLUDES = -I$(ZVIBE_ROOT)/include -I.

# Board-specific sw directories
ARTY_SW  = ../../boards/arty_s7_50/sw

# Per-platform compiler flags (Arty: adds board includes for flash driver config)
ARTY_CFLAGS  = $(CFLAGS) $(INCLUDES) -I. -I$(ARTY_SW)

# Linker flags for RAM-based builds
LDFLAGS = -nostdlib -nostartfiles -Tlink.ld -Wl,--gc-sections

.PHONY: all clean size disasm xip-echo sim-hex sim-hex-max10 sim-hex-arty flash-write-xip-test

all: uart_echo_xip.hex

#=============================================================================
# UART Echo XIP (primary simulation target, MAX10 default link address)
#=============================================================================

uart_echo_xip.elf: uart_echo_xip.c xip_start.S xip_link.ld
	$(CC) $(CFLAGS) -nostdlib -nostartfiles -Txip_link.ld \
		-Wl,--defsym=__flash_origin=0x80000100 -Wl,--gc-sections \
		-o $@ xip_start.S uart_echo_xip.c
	$(SIZE) $@

uart_echo_xip.bin: uart_echo_xip.elf
	$(OBJCOPY) -O binary $< $@

uart_echo_xip.hex: uart_echo_xip.elf
	$(OBJCOPY) -O verilog --verilog-data-width=1 \
		--change-addresses=-0x80000000 $< $@

# Generate .dat file for simulation (word-addressed Verilog hex)
# CRITICAL: --change-addresses=-0x80000000 maps virtual 0x80000000 → physical 0x00000000
uart_echo_xip_test.dat: uart_echo_xip.elf
	$(OBJCOPY) -O verilog --verilog-data-width=4 \
		--change-addresses=-0x80000000 $< $@
	@echo "✓ Generated $@ (word-addressed, physical addresses)"
	@head -3 $@

uart_echo_xip.dis: uart_echo_xip.elf
	$(OBJDUMP) -d $< > $@

xip-echo: uart_echo_xip.bin

#=============================================================================
# Board-specific sim hex files (same source, different link addresses)
#
# MAX10: firmware at XIP 0x80000100, entry at flash phys 0x100
# Arty:  firmware at XIP 0x80100100, entry at flash phys 0x100100
#=============================================================================

# MAX10 sim hex: byte-addressed, physical addresses (virtual 0x80000100 → phys 0x100)
uart_echo_xip_max10.elf: uart_echo_xip.c xip_start.S xip_link.ld
	$(CC) $(CFLAGS) -nostdlib -nostartfiles -Txip_link.ld \
		-Wl,--defsym=__flash_origin=0x80000100 -Wl,--gc-sections \
		-o $@ xip_start.S uart_echo_xip.c

uart_echo_xip_max10.hex: uart_echo_xip_max10.elf
	$(OBJCOPY) -O verilog --verilog-data-width=1 \
		--change-addresses=-0x80000000 $< $@
	@echo "✓ MAX10 sim hex: entry at @00000100"

# Arty sim hex: byte-addressed, absolute flash physical addresses
uart_echo_xip_arty.elf: uart_echo_xip.c xip_start.S xip_link.ld
	$(CC) $(CFLAGS) -nostdlib -nostartfiles -Txip_link.ld \
		-Wl,--defsym=__flash_origin=0x80100100 -Wl,--gc-sections \
		-o $@ xip_start.S uart_echo_xip.c

uart_echo_xip_arty.hex: uart_echo_xip_arty.elf
	$(OBJCOPY) -O verilog --verilog-data-width=1 \
		--change-addresses=-0x80000000 $< $@
	@echo "✓ Arty sim hex: entry at @00100100 (flash phys)"

sim-hex-max10: uart_echo_xip_max10.hex
sim-hex-arty: uart_echo_xip_arty.hex
sim-hex: uart_echo_xip_max10.hex uart_echo_xip_arty.hex

#=============================================================================
# Unified UART Echo (single source, RAM or XIP builds)
#=============================================================================

uart_echo_ram.elf: uart_echo.c crt0_minimal.S link.ld
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ crt0_minimal.S uart_echo.c
	$(SIZE) $@

uart_echo_ram.hex: uart_echo_ram.elf
	$(OBJCOPY) -O verilog --verilog-data-width=4 $< $@

uart_echo_unified_xip.elf: uart_echo.c xip_start.S xip_link.ld
	$(CC) $(CFLAGS) -DXIP_MODE -nostdlib -nostartfiles -Txip_link.ld \
		-Wl,--defsym=__flash_origin=0x80001000 -Wl,--gc-sections \
		-o $@ xip_start.S uart_echo.c
	$(SIZE) $@

uart_echo_unified_xip.bin: uart_echo_unified_xip.elf
	$(OBJCOPY) -O binary $< $@

uart_echo_unified_xip.hex: uart_echo_unified_xip.elf
	$(OBJCOPY) -O verilog --verilog-data-width=1 $< $@

#=============================================================================
# Flash Write XIP Test (Arty QSPI write/read test)
#=============================================================================

flash_write_xip_test.elf: $(ARTY_SW)/flash_write_xip_test.c xip_start.S xip_link.ld
	$(CC) $(CFLAGS) -nostdlib -nostartfiles -Txip_link.ld \
		-Wl,--defsym=__flash_origin=0x80100100 -Wl,--gc-sections \
		-o $@ xip_start.S $(ARTY_SW)/flash_write_xip_test.c
	$(SIZE) $@

flash_write_xip_test.bin: flash_write_xip_test.elf
	$(OBJCOPY) -O binary $< $@

flash_write_xip_test.hex: flash_write_xip_test.elf
	$(OBJCOPY) -O verilog --verilog-data-width=1 \
		--change-addresses=-0x80000000 $< $@
	@echo "✓ Flash write XIP test hex: entry at @00100100 (flash phys)"

flash_write_xip_test.dis: flash_write_xip_test.elf
	$(OBJDUMP) -d $< > $@

flash-write-xip-test: flash_write_xip_test.bin
	@echo "Flash write XIP test built: flash_write_xip_test.bin"
	@echo "  Program with: cd ../boards/arty_s7_50/fpga && make program-flash-test"

#=============================================================================
# ZVibe XIP flags and object pattern rules
#=============================================================================

# Embedded firmware profile:
# no host file I/O, no status-line callbacks, save/restore enabled.
ZVIBE_XIP_FLAGS = -DZVIBE_MINIMAL_FEATURES -DZVIBE_ENABLE_FILE_IO=0 \
                  -DZVIBE_ENABLE_STATUS_LINE=0 -DZVIBE_ENABLE_SAVE_RESTORE=1 \
                  -DMAX_GAMES=32

src/api/zvibe_api_xip.o: $(ZVIBE_ROOT)/src/api/zvibe_api.c
	@mkdir -p src/api
	$(CC) $(CFLAGS) $(INCLUDES) $(ZVIBE_XIP_FLAGS) -c $< -o $@

src/core/zvibe_core_xip.o: $(ZVIBE_ROOT)/src/core/zvibe_core.c
	@mkdir -p src/core
	$(CC) $(CFLAGS) $(INCLUDES) $(ZVIBE_XIP_FLAGS) -c $< -o $@

src/core/zvibe_memory_xip.o: $(ZVIBE_ROOT)/src/core/zvibe_memory.c
	@mkdir -p src/core
	$(CC) $(CFLAGS) $(INCLUDES) $(ZVIBE_XIP_FLAGS) -c $< -o $@

src/core/zvibe_object_xip.o: $(ZVIBE_ROOT)/src/core/zvibe_object.c
	@mkdir -p src/core
	$(CC) $(CFLAGS) $(INCLUDES) $(ZVIBE_XIP_FLAGS) -c $< -o $@

src/core/zvibe_ops_base_xip.o: $(ZVIBE_ROOT)/src/core/zvibe_ops_base.c
	@mkdir -p src/core
	$(CC) $(CFLAGS) $(INCLUDES) $(ZVIBE_XIP_FLAGS) -c $< -o $@

src/core/zvibe_ops_math_xip.o: $(ZVIBE_ROOT)/src/core/zvibe_ops_math.c
	@mkdir -p src/core
	$(CC) $(CFLAGS) $(INCLUDES) $(ZVIBE_XIP_FLAGS) -c $< -o $@

src/core/zvibe_ops_mem_xip.o: $(ZVIBE_ROOT)/src/core/zvibe_ops_mem.c
	@mkdir -p src/core
	$(CC) $(CFLAGS) $(INCLUDES) $(ZVIBE_XIP_FLAGS) -c $< -o $@

src/core/zvibe_ops_text_xip.o: $(ZVIBE_ROOT)/src/core/zvibe_ops_text.c
	@mkdir -p src/core
	$(CC) $(CFLAGS) $(INCLUDES) $(ZVIBE_XIP_FLAGS) -c $< -o $@

ZVIBE_OBJ_XIP = \
	src/api/zvibe_api_xip.o \
	src/core/zvibe_core_xip.o \
	src/core/zvibe_memory_xip.o \
	src/core/zvibe_object_xip.o \
	src/core/zvibe_ops_base_xip.o \
	src/core/zvibe_ops_math_xip.o \
	src/core/zvibe_ops_mem_xip.o \
	src/core/zvibe_ops_text_xip.o

#=============================================================================
# Multi-Game ZVibe Firmware (Arty S7-50)
#
# NOTE: Prefer boards/arty_s7_50/sw/Makefile for production Arty builds.
#       These rules are kept here for quick builds from common/sw.
#=============================================================================

menu_system.o: menu_system.c menu_system.h
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

game_registry_riscv.o: game_registry_riscv.c game_registry_riscv.h menu_system.h flash_metadata.h
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

flash_metadata.o: flash_metadata.c flash_metadata.h $(ARTY_SW)/flash_save_config.h
	$(CC) $(ARTY_CFLAGS) -c $< -o $@

flash_save_arty.o: flash_save.c flash_save.h $(ARTY_SW)/flash_save_config.h
	$(CC) $(ARTY_CFLAGS) -c $< -o $@

flash_driver.o: $(ARTY_SW)/flash_driver.c
	$(CC) $(ARTY_CFLAGS) -c $< -o $@

save_delta.o: save_delta.c save_delta.h
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

zvibe_riscv_multi.elf: zvibe_riscv_multi.c menu_system.o game_registry_riscv.o \
                       flash_metadata.o flash_save_arty.o flash_driver.o save_delta.o \
                       zvibe_stubs.c xip_start.S xip_link.ld $(ZVIBE_OBJ_XIP)
	$(CC) $(ARTY_CFLAGS) $(ZVIBE_XIP_FLAGS) \
		-nostdlib -nostartfiles -Txip_link.ld \
		-Wl,--defsym=__flash_origin=0x80100100 -Wl,--gc-sections \
		-o $@ xip_start.S zvibe_riscv_multi.c menu_system.o game_registry_riscv.o \
		flash_metadata.o flash_save_arty.o flash_driver.o save_delta.o \
		zvibe_stubs.c $(ZVIBE_OBJ_XIP) -lgcc
	$(SIZE) $@

zvibe_riscv_multi.bin: zvibe_riscv_multi.elf
	$(OBJCOPY) -O binary $< $@

zvibe_riscv_multi.hex: zvibe_riscv_multi.elf
	$(OBJCOPY) -O verilog --verilog-data-width=1 --change-addresses=-0x80100100 $< $@

zvibe_riscv_multi.dis: zvibe_riscv_multi.elf
	$(OBJDUMP) -d $< > $@

#=============================================================================
# Utility targets
#=============================================================================

size: uart_echo_xip.elf
	$(SIZE) -A $<

disasm: uart_echo_xip.dis
	@echo "Disassembly written to uart_echo_xip.dis"

clean:
	rm -f *.elf *.hex *.bin *.dis *.o *.dat
	rm -rf src/

help:
	@echo "Servant ZVibe Common Firmware"
	@echo "=============================="
	@echo ""
	@echo "Simulation targets (primary use of this Makefile):"
	@echo "  all              Build uart_echo_xip.hex (MAX10 echo, default)"
	@echo "  sim-hex-max10    Build uart_echo_xip_max10.hex (MAX10 sim)"
	@echo "  sim-hex-arty     Build uart_echo_xip_arty.hex (Arty sim)"
	@echo "  sim-hex          Build both board sim hex files"
	@echo ""
	@echo "Arty flash write test:"
	@echo "  flash-write-xip-test  Build flash_write_xip_test.bin"
	@echo ""
	@echo "Board firmware builds (preferred):"
	@echo "  cd boards/max10_08_eval/sw && make firmware"
	@echo "  cd boards/arty_s7_50/sw    && make firmware"
	@echo ""
	@echo "Utility:"
	@echo "  size     Show uart_echo_xip memory usage"
	@echo "  disasm   Generate uart_echo_xip disassembly"
	@echo "  clean    Remove build artifacts"
