# Copyright (c) 2025 Martin R. Raumann
# SPDX-License-Identifier: BSD-3-Clause
# Compiler settings
CC = gcc
CFLAGS = -Wall -Wextra -O2 -I../../include
SHARED_CFLAGS = $(CFLAGS) -fPIC
MINIMAL_API_CFLAGS = $(CFLAGS) -DZVIBE_ENABLE_SAVE_RESTORE=0 -DZVIBE_ENABLE_STATUS_LINE=0

# Platform-specific shared library settings
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Linux)
    SHARED_EXT = .so
    SHARED_FLAGS = -shared
endif
ifeq ($(UNAME_S),Darwin)
    SHARED_EXT = .dylib
    SHARED_FLAGS = -shared -undefined dynamic_lookup
endif
# Default fallback
ifndef SHARED_EXT
    SHARED_EXT = .so
    SHARED_FLAGS = -shared
endif

# Directories
SRC_DIR = ../../src
BUILD_DIR = build
OBJ_DIR = $(BUILD_DIR)/obj
BIN_DIR = $(BUILD_DIR)/bin
LIB_DIR = $(BUILD_DIR)/lib

# Source files
CORE_SRCS = $(wildcard $(SRC_DIR)/core/*.c)
CONSOLE_SRC = zvibe_console.c
MINIMAL_SRC = zvibe_minimal.c

# Object files for executables
CORE_OBJS = $(patsubst $(SRC_DIR)/core/%.c,$(OBJ_DIR)/%.o,$(CORE_SRCS))
API_OBJ = $(OBJ_DIR)/zvibe_api.o
API_FILE_OBJ = $(OBJ_DIR)/zvibe_api_file.o
API_MINIMAL_OBJ = $(OBJ_DIR)/zvibe_api_minimal.o
API_FILE_MINIMAL_OBJ = $(OBJ_DIR)/zvibe_api_file_minimal.o
CONSOLE_OBJ = $(OBJ_DIR)/zvibe_console.o
MINIMAL_OBJ = $(OBJ_DIR)/zvibe_minimal.o

# Object files for shared libraries (with -fPIC)
CORE_SHARED_OBJS = $(patsubst $(SRC_DIR)/core/%.c,$(OBJ_DIR)/%_shared.o,$(CORE_SRCS))
API_SHARED_OBJ = $(OBJ_DIR)/zvibe_api_shared.o
API_FILE_SHARED_OBJ = $(OBJ_DIR)/zvibe_api_file_shared.o

# Target binaries
CONSOLE_TARGET = $(BIN_DIR)/zvibe_console
MINIMAL_TARGET = $(BIN_DIR)/zvibe_minimal

# Shared library target
SHARED_LIB = $(LIB_DIR)/libzvibe$(SHARED_EXT)

# Default target
all: directories $(CONSOLE_TARGET) $(MINIMAL_TARGET) $(SHARED_LIB)

# Shared library target
shared: directories $(SHARED_LIB)

# Create necessary directories
directories:
	@mkdir -p $(OBJ_DIR)
	@mkdir -p $(BIN_DIR)
	@mkdir -p $(LIB_DIR)

# Console target
$(CONSOLE_TARGET): $(CORE_OBJS) $(API_OBJ) $(API_FILE_OBJ) $(CONSOLE_OBJ)
	$(CC) $(CFLAGS) $^ -o $@

# Minimal target
$(MINIMAL_TARGET): $(CORE_OBJS) $(API_MINIMAL_OBJ) $(API_FILE_MINIMAL_OBJ) $(MINIMAL_OBJ)
	$(CC) $(CFLAGS) $^ -o $@

# Compile core source files
$(OBJ_DIR)/%.o: $(SRC_DIR)/core/%.c
	$(CC) $(CFLAGS) -c $< -o $@

# Compile API (full features)
$(API_OBJ): $(SRC_DIR)/api/zvibe_api.c
	$(CC) $(CFLAGS) -c $< -o $@

$(API_FILE_OBJ): $(SRC_DIR)/api/zvibe_api_file.c
	$(CC) $(CFLAGS) -c $< -o $@

# Compile API for minimal target: keep host file loading, but strip
# save/restore and status-line support from the library code.
$(API_MINIMAL_OBJ): $(SRC_DIR)/api/zvibe_api.c
	$(CC) $(MINIMAL_API_CFLAGS) -c $< -o $@

$(API_FILE_MINIMAL_OBJ): $(SRC_DIR)/api/zvibe_api_file.c
	$(CC) $(MINIMAL_API_CFLAGS) -c $< -o $@

# Compile console
$(CONSOLE_OBJ): zvibe_console.c
	$(CC) $(CFLAGS) -c $< -o $@

# Compile minimal source file
$(MINIMAL_OBJ): zvibe_minimal.c
	$(CC) $(CFLAGS) -c $< -o $@

# Shared library target
$(SHARED_LIB): $(CORE_SHARED_OBJS) $(API_SHARED_OBJ) $(API_FILE_SHARED_OBJ)
	$(CC) $(SHARED_FLAGS) $^ -o $@

# Compile core source files for shared libraries (with -fPIC)
$(OBJ_DIR)/%_shared.o: $(SRC_DIR)/core/%.c
	$(CC) $(SHARED_CFLAGS) -c $< -o $@

# Compile API (full features) for shared library
$(API_SHARED_OBJ): $(SRC_DIR)/api/zvibe_api.c
	$(CC) $(SHARED_CFLAGS) -c $< -o $@

$(API_FILE_SHARED_OBJ): $(SRC_DIR)/api/zvibe_api_file.c
	$(CC) $(SHARED_CFLAGS) -c $< -o $@

# Clean build
clean:
	rm -rf $(BUILD_DIR)

# Clean build and documentation
clean-all: clean
	rm -rf ../../docs/html ../../docs/latex

# Documentation target
docs:
	@echo "Generating documentation with Doxygen..."
	@command -v doxygen >/dev/null || { echo "doxygen not found. Install doxygen first."; exit 1; }
	@command -v dot >/dev/null || { echo "dot not found. Install GraphViz first."; exit 1; }
	cd ../../docs && doxygen Doxyfile
	@echo "Documentation generated in ../../docs/html/index.html"

# Test status optimization
STATUS_TEST_TARGET = $(BIN_DIR)/test_status_optimization
STATUS_TEST_OBJ = $(OBJ_DIR)/test_status_optimization.o

$(STATUS_TEST_TARGET): $(CORE_OBJS) $(API_OBJ) $(API_FILE_OBJ) $(STATUS_TEST_OBJ)
	$(CC) $(CFLAGS) $^ -o $@

$(STATUS_TEST_OBJ): tests/test_status_optimization.c
	$(CC) $(CFLAGS) -c $< -o $@

# Convenience targets
minimal: directories $(MINIMAL_TARGET)
console: directories $(CONSOLE_TARGET)
test-status: directories $(STATUS_TEST_TARGET)

.PHONY: all clean clean-all directories minimal console docs shared test-status
