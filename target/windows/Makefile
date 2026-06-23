# Copyright (c) 2025 Martin R. Raumann
# SPDX-License-Identifier: BSD-3-Clause
# Windows Build System for ZVibe Z-Machine Interpreter
# Cross-compilation from WSL2 Ubuntu to Windows using MinGW-w64

# Compiler settings
WIN_CC = x86_64-w64-mingw32-gcc
WIN_RC = x86_64-w64-mingw32-windres
WIN_CFLAGS = -Wall -Wextra -O2 -I../../include -D_WIN32

# Directories
SRC_DIR = ../../src
CONSOLE_SRC_DIR = ../console
BUILD_DIR = build
OBJ_DIR = $(BUILD_DIR)/obj
BIN_DIR = $(BUILD_DIR)/bin
LIB_DIR = $(BUILD_DIR)/lib

# Windows installation directory (accessible from Windows)
WINDOWS_INSTALL_DIR = /mnt/c/tmp/zvibe_win

# Source files
CORE_SRCS = $(wildcard $(SRC_DIR)/core/*.c)
CONSOLE_SRC = $(CONSOLE_SRC_DIR)/zvibe_console.c
MINIMAL_SRC = $(CONSOLE_SRC_DIR)/zvibe_minimal.c

# Object files
CORE_OBJS = $(patsubst $(SRC_DIR)/core/%.c,$(OBJ_DIR)/%.o,$(CORE_SRCS))
API_OBJ = $(OBJ_DIR)/zvibe_api.o
API_FILE_OBJ = $(OBJ_DIR)/zvibe_api_file.o
API_MINIMAL_OBJ = $(OBJ_DIR)/zvibe_api_minimal.o
API_FILE_MINIMAL_OBJ = $(OBJ_DIR)/zvibe_api_file_minimal.o
CONSOLE_OBJ = $(OBJ_DIR)/zvibe_console.o
MINIMAL_OBJ = $(OBJ_DIR)/zvibe_minimal.o

# Shared library objects
CORE_SHARED_OBJS = $(patsubst $(SRC_DIR)/core/%.c,$(OBJ_DIR)/%_shared.o,$(CORE_SRCS))
API_SHARED_OBJ = $(OBJ_DIR)/zvibe_api_shared.o
API_FILE_SHARED_OBJ = $(OBJ_DIR)/zvibe_api_file_shared.o

# Resource files
CONSOLE_RES = $(OBJ_DIR)/zvibe_console_res.o
MINIMAL_RES = $(OBJ_DIR)/zvibe_minimal_res.o

# Target binaries
CONSOLE_TARGET = $(BIN_DIR)/zvibe_console.exe
MINIMAL_TARGET = $(BIN_DIR)/zvibe_minimal.exe
SHARED_LIB = $(LIB_DIR)/libzvibe.dll

# Default target
all: directories $(CONSOLE_TARGET) $(MINIMAL_TARGET) $(SHARED_LIB)

# Individual targets
console: directories $(CONSOLE_TARGET)
minimal: directories $(MINIMAL_TARGET)
shared: directories $(SHARED_LIB)

# Create build directories
directories:
	@mkdir -p $(OBJ_DIR)
	@mkdir -p $(BIN_DIR)
	@mkdir -p $(LIB_DIR)

# Console target
$(CONSOLE_TARGET): $(CORE_OBJS) $(API_OBJ) $(API_FILE_OBJ) $(CONSOLE_OBJ) $(CONSOLE_RES)
	$(WIN_CC) $(WIN_CFLAGS) $^ -o $@

# Minimal target
$(MINIMAL_TARGET): $(CORE_OBJS) $(API_MINIMAL_OBJ) $(API_FILE_MINIMAL_OBJ) $(MINIMAL_OBJ) $(MINIMAL_RES)
	$(WIN_CC) $(WIN_CFLAGS) $^ -o $@

# Shared library
$(SHARED_LIB): $(CORE_SHARED_OBJS) $(API_SHARED_OBJ) $(API_FILE_SHARED_OBJ)
	$(WIN_CC) -shared -Wl,--out-implib,$(LIB_DIR)/libzvibe.dll.a $^ -o $@

# Compile core source files
$(OBJ_DIR)/%.o: $(SRC_DIR)/core/%.c
	$(WIN_CC) $(WIN_CFLAGS) -c $< -o $@

# Compile API (full features)
$(API_OBJ): $(SRC_DIR)/api/zvibe_api.c
	$(WIN_CC) $(WIN_CFLAGS) -c $< -o $@

$(API_FILE_OBJ): $(SRC_DIR)/api/zvibe_api_file.c
	$(WIN_CC) $(WIN_CFLAGS) -c $< -o $@

# Compile API (for minimal target - same as full, file I/O still needed)
$(API_MINIMAL_OBJ): $(SRC_DIR)/api/zvibe_api.c
	$(WIN_CC) $(WIN_CFLAGS) -c $< -o $@

$(API_FILE_MINIMAL_OBJ): $(SRC_DIR)/api/zvibe_api_file.c
	$(WIN_CC) $(WIN_CFLAGS) -c $< -o $@

# Compile console application
$(CONSOLE_OBJ): $(CONSOLE_SRC)
	$(WIN_CC) $(WIN_CFLAGS) -c $< -o $@

# Compile minimal application
$(MINIMAL_OBJ): $(MINIMAL_SRC)
	$(WIN_CC) $(WIN_CFLAGS) -c $< -o $@

# Compile core source files for shared library
$(OBJ_DIR)/%_shared.o: $(SRC_DIR)/core/%.c
	$(WIN_CC) $(WIN_CFLAGS) -c $< -o $@

# Compile API for shared library
$(API_SHARED_OBJ): $(SRC_DIR)/api/zvibe_api.c
	$(WIN_CC) $(WIN_CFLAGS) -c $< -o $@

$(API_FILE_SHARED_OBJ): $(SRC_DIR)/api/zvibe_api_file.c
	$(WIN_CC) $(WIN_CFLAGS) -c $< -o $@

# Compile resource files
$(CONSOLE_RES): resources/zvibe_console.rc
	$(WIN_RC) $< -o $@

$(MINIMAL_RES): resources/zvibe_minimal.rc
	$(WIN_RC) $< -o $@

# Windows installation targets
install: all copy-install

copy-install:
	@echo "📦 Installing ZVibe to Windows..."
	@mkdir -p $(WINDOWS_INSTALL_DIR)/win
	@mkdir -p $(WINDOWS_INSTALL_DIR)/win_lib
	@cp $(BIN_DIR)/*.exe $(WINDOWS_INSTALL_DIR)/win/ || echo "⚠️  Could not copy executables (may be in use)"
	@cp $(LIB_DIR)/*.dll $(WINDOWS_INSTALL_DIR)/win_lib/
	@cp $(LIB_DIR)/*.a $(WINDOWS_INSTALL_DIR)/win_lib/ 2>/dev/null || true
	@cp icons/Z3.ico $(WINDOWS_INSTALL_DIR)/
	@echo "✅ Installation complete: C:\\tmp\\zvibe_win"

# Development workflow
deploy: all install copy-launchers

copy-launchers:
	@echo "📋 Copying Windows launchers and setup files..."
	@cp scripts/Create-Desktop-Shortcuts.ps1 $(WINDOWS_INSTALL_DIR)/ 2>/dev/null || echo "⚠️  Create-Desktop-Shortcuts.ps1 not found"
	@cp scripts/Launch-ZVibe.ps1 $(WINDOWS_INSTALL_DIR)/ 2>/dev/null || echo "⚠️  Launch-ZVibe.ps1 not found"
	@cp scripts/launch_zvibe.bat $(WINDOWS_INSTALL_DIR)/ 2>/dev/null || echo "⚠️  launch_zvibe.bat not found"
	@cp scripts/register_z3_files.reg $(WINDOWS_INSTALL_DIR)/ 2>/dev/null || echo "⚠️  register_z3_files.reg not found"
	@cp scripts/unregister_z3_files.reg $(WINDOWS_INSTALL_DIR)/ 2>/dev/null || echo "⚠️  unregister_z3_files.reg not found"
	@cp docs/WINDOWS_INSTALLATION.md $(WINDOWS_INSTALL_DIR)/ 2>/dev/null || echo "⚠️  WINDOWS_INSTALLATION.md not found"

# Development convenience
update: all
	@scripts/update_windows_build.sh

# Icon management
icons:
	@echo "🎨 Managing icons..."
	@echo "Current icons:"
	@ls -la icons/
	@echo ""
	@echo "To create placeholder icons: python3 scripts/create_placeholder_icons.py"
	@echo "To use custom icon: Replace icons/ZVibe.ico with your custom icon"

# Clean build
clean:
	rm -rf $(BUILD_DIR)

# Clean all (including Windows installation)
clean-all: clean
	rm -rf $(WINDOWS_INSTALL_DIR)

# Help
help:
	@echo "ZVibe Windows Build System"
	@echo ""
	@echo "Targets:"
	@echo "  all      - Build all Windows targets (console, minimal, shared)"
	@echo "  console  - Build console application only"
	@echo "  minimal  - Build minimal application only"
	@echo "  shared   - Build shared library only"
	@echo "  install  - Build and install to Windows"
	@echo "  deploy   - Build, install, and copy all Windows files"
	@echo "  update   - Quick update of existing Windows installation"
	@echo "  icons    - Show icon status and management info"
	@echo "  clean    - Remove build artifacts"
	@echo "  clean-all- Remove build artifacts and Windows installation"
	@echo ""
	@echo "Prerequisites:"
	@echo "  sudo apt install mingw-w64 gcc-mingw-w64"

.PHONY: all console minimal shared directories install copy-install deploy copy-launchers update icons clean clean-all help
