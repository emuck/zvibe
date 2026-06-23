# Copyright (c) 2025 Martin R. Raumann
# SPDX-License-Identifier: BSD-3-Clause
# SAM E51 Curiosity Nano - SmartEEPROM Test Program (Consolidated)
# Using consolidated peripheral drivers for clean, production-ready SmartEEPROM implementation

# Project configuration
PROJECT_NAME = same51_smarteeprom_test
MCU_TARGET = ATSAME51J20A
EDBG_TARGET = same51

# OS detection and toolchain configuration
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
    # macOS
    XC32_PATH = /Applications/microchip/xc32/v4.60/bin
else ifeq ($(UNAME_S),Linux)
    # Linux
    XC32_PATH = /opt/microchip/xc32/v4.60/bin
else
    $(error Unsupported operating system: $(UNAME_S))
endif

# Toolchain
CC = $(XC32_PATH)/xc32-gcc
OBJCOPY = $(XC32_PATH)/xc32-objcopy
SIZE = $(XC32_PATH)/xc32-size

# Programmer  
PROGRAMMER = edbg
PROG_FLAGS = -b -t $(EDBG_TARGET) -c 1000 -p -v -f

# Compiler flags
CFLAGS = -g -x c -c -mprocessor=$(MCU_TARGET)
CFLAGS += -ffunction-sections -fdata-sections -O1 -fno-common
CFLAGS += -Werror -Wall -MP -MMD
CFLAGS += -I"../../include"
CFLAGS += -I"../../packs/ATSAME51J20A_DFP" -I"../../packs/CMSIS/" -I"../../packs/CMSIS/CMSIS/Core/Include"

# Linker flags
LDFLAGS = -mprocessor=$(MCU_TARGET) -mno-device-startup-code
LDFLAGS += -Wl,--defsym=__MPLAB_BUILD=1
LDFLAGS += -Wl,--defsym=_min_heap_size=512
LDFLAGS += -Wl,--gc-sections
LDFLAGS += -T ../../startup/ATSAME51J20A.ld

# Source files - SmartEEPROM test program using consolidated drivers
SOURCES = smarteeprom_test.c \
          ../../startup/startup_xc32.c \
          ../../src/clock.c \
          ../../src/gpio.c \
          ../../src/timer.c \
          ../../src/dma.c \
          ../../src/system.c \
          ../../src/flash.c \
          ../../src/uart.c

# Build directories
BUILD_DIR = build
OBJ_DIR = $(BUILD_DIR)/obj

# Object files (placed in build directory)
OBJECTS = $(patsubst %.c,$(OBJ_DIR)/%.o,$(SOURCES))

.PHONY: all clean program test platform_info

all: platform_info $(BUILD_DIR)/$(PROJECT_NAME).elf
	@echo ""
	@echo "=== SmartEEPROM Test Build Complete ==="
	$(SIZE) $(BUILD_DIR)/$(PROJECT_NAME).elf
	@echo ""

# Show platform information
platform_info:
	@echo "Building SmartEEPROM test on $(UNAME_S) - using XC32 at $(XC32_PATH)"

# Create build directories
$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)
	mkdir -p $(OBJ_DIR)
	mkdir -p $(OBJ_DIR)/../../startup
	mkdir -p $(OBJ_DIR)/../../src

# Compile rule for object files in build directory
$(OBJ_DIR)/%.o: %.c | $(BUILD_DIR)
	@echo "Compiling $<"
	$(CC) $(CFLAGS) -MF"$(@:.o=.d)" -o "$@" "$<"

# Link rule
$(BUILD_DIR)/$(PROJECT_NAME).elf: $(BUILD_DIR) $(OBJECTS)
	@echo "Linking $(PROJECT_NAME).elf"
	$(CC) $(LDFLAGS) -o "$@" $(OBJECTS)
	$(OBJCOPY) -O binary --remove-section=.config* "$@" "$(BUILD_DIR)/$(PROJECT_NAME).bin"
	$(SIZE) "$@"
	@echo ""
	@echo "Output files:"
	@echo "  ELF: $@"
	@echo "  BIN: $(BUILD_DIR)/$(PROJECT_NAME).bin"

# Program target - with chip erase first
program: $(BUILD_DIR)/$(PROJECT_NAME).elf
	@echo "Erasing entire flash memory..."
	$(PROGRAMMER) -b -t $(EDBG_TARGET) -c 1000 -e
	@echo "Programming SmartEEPROM test..."
	$(PROGRAMMER) $(PROG_FLAGS) "$(BUILD_DIR)/$(PROJECT_NAME).bin"

# Deploy target - same as program
deploy: program

# Test target - build and program
test: program
	@echo ""
	@echo "=== SmartEEPROM Test Program Deployed ==="
	@echo "Features:"
	@echo "  - Complete 16KB SmartEEPROM functionality test"
	@echo "  - Uses consolidated peripheral drivers"
	@echo "  - Automatic fuse configuration (SBLK=3, PSZ=7)"
	@echo "  - Full initialization sequence (RLOCK unlock, SEECFG setup)"
	@echo "  - 16KB write/read verification with progress tracking"
	@echo "  - Comprehensive bit pattern integrity testing"
	@echo ""
	@echo "Connect to UART at 115200 baud and press button to run test."
	@echo ""

# Clean rule
clean:
	@echo "Cleaning SmartEEPROM test build files..."
	rm -rf $(BUILD_DIR)
	@echo "Clean complete."

# Include dependency files
-include $(OBJECTS:.o=.d)