# Copyright (c) 2025 Martin R. Raumann
# SPDX-License-Identifier: BSD-3-Clause
# Test Makefile for zVibe Z-machine interpreter
# Builds and runs unit tests and integration tests

# Compiler settings
CC = gcc
CFLAGS = -Wall -Wextra -O2 -std=c99
INCLUDES = -I../../../include

# Source directories
CORE_DIR = ../../../src/core
TEST_DIR = .
# Console build directory under test. Override to test another build, e.g. the
# V4-enabled one: make test-czech ZVIBE_BUILD=build_v4
ZVIBE_BUILD ?= build
BUILD_DIR = ../$(ZVIBE_BUILD)
export ZVIBE_BUILD
BIN_DIR = $(BUILD_DIR)/bin

# Test executables
TEST_TARGETS = test_split_memory test_screen test_config_v3 test_config_v4 test_config_stack1024 \
               test_save_restore test_save_restore_v4_stack1024 test_save_restore_san \
               test_objects_v3 test_objects_v4 test_dict_v3 test_dict_v4 test_opcodes_v3 test_opcodes_v4 test_save_restore_san

# Integration test scripts
INTEGRATION_TESTS = test_czech.sh

# Default target
all: $(TEST_TARGETS)

# Unit test targets
test_split_memory: test_split_memory.c $(CORE_DIR)/zvibe_memory.c
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $^

test_screen: test_screen.c $(CORE_DIR)/../screen/zvibe_screen.c
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $^

# Build configuration and restore-safety tests. They link the whole core and
# depend on every public header, so changing zvibe_config.h rebuilds them.
# Each config binary states what it must be (EXPECT_*), so a wrong -D fails the
# build instead of silently testing the wrong configuration.
# Test binaries also depend on a stamp of the compiler and flags, so changing
# CC/CFLAGS (not just a source or header) rebuilds them.
TEST_STAMP = .test_build_flags
$(TEST_STAMP): FORCE
	@echo '$(CC) $(CFLAGS) $(INCLUDES)' | cmp -s - $@ || echo '$(CC) $(CFLAGS) $(INCLUDES)' > $@
FORCE:

ALL_SRCS = $(wildcard $(CORE_DIR)/*.c) $(CORE_DIR)/../api/zvibe_api.c $(CORE_DIR)/../api/zvibe_api_file.c
HEADERS = $(wildcard ../../../include/*.h) test_story.h
DELTA_INC = -I../../same51/include
DELTA_SRC = ../../same51/src/save_delta.c

test_config_v3: test_config.c $(ALL_SRCS) $(HEADERS) $(TEST_STAMP)
	$(CC) $(CFLAGS) $(INCLUDES) -DEXPECT_V4=0 -DEXPECT_STACK_WORDS=512 -o $@ test_config.c $(ALL_SRCS)

test_config_v4: test_config.c $(ALL_SRCS) $(HEADERS) $(TEST_STAMP)
	$(CC) $(CFLAGS) $(INCLUDES) -DZVIBE_ENABLE_V4=1 -DEXPECT_V4=1 -DEXPECT_STACK_WORDS=512 -o $@ test_config.c $(ALL_SRCS)

test_config_stack1024: test_config.c $(ALL_SRCS) $(HEADERS) $(TEST_STAMP)
	$(CC) $(CFLAGS) $(INCLUDES) -DZVIBE_STACK_WORDS=1024 -DEXPECT_V4=0 -DEXPECT_STACK_WORDS=1024 -o $@ test_config.c $(ALL_SRCS)

test_save_restore: test_save_restore.c $(ALL_SRCS) $(DELTA_SRC) $(HEADERS) ../../same51/include/save_delta.h $(TEST_STAMP)
	$(CC) $(CFLAGS) $(INCLUDES) $(DELTA_INC) -o $@ test_save_restore.c $(ALL_SRCS) $(DELTA_SRC)

test_save_restore_v4_stack1024: test_save_restore.c $(ALL_SRCS) $(DELTA_SRC) $(HEADERS) ../../same51/include/save_delta.h $(TEST_STAMP)
	$(CC) $(CFLAGS) $(INCLUDES) $(DELTA_INC) -DZVIBE_ENABLE_V4=1 -DZVIBE_STACK_WORDS=1024 -o $@ test_save_restore.c $(ALL_SRCS) $(DELTA_SRC)

# Object table, properties, packed addresses, verify: V3-only build, and a
# dual build that runs the same V3 cases plus the V4 layouts.
test_objects_v3: test_objects.c $(ALL_SRCS) $(HEADERS) $(TEST_STAMP)
	$(CC) $(CFLAGS) $(INCLUDES) -DEXPECT_V4=0 -o $@ test_objects.c $(ALL_SRCS)

test_objects_v4: test_objects.c $(ALL_SRCS) $(HEADERS) $(TEST_STAMP)
	$(CC) $(CFLAGS) $(INCLUDES) -DZVIBE_ENABLE_V4=1 -DEXPECT_V4=1 -o $@ test_objects.c $(ALL_SRCS)

# Loading, header setup, instruction decoding, call forms, save/restore results.
test_opcodes_v3: test_opcodes.c $(ALL_SRCS) $(HEADERS) $(TEST_STAMP)
	$(CC) $(CFLAGS) $(INCLUDES) -DEXPECT_V4=0 -o $@ test_opcodes.c $(ALL_SRCS)

test_opcodes_v4: test_opcodes.c $(ALL_SRCS) $(HEADERS) $(TEST_STAMP)
	$(CC) $(CFLAGS) $(INCLUDES) -DZVIBE_ENABLE_V4=1 -DEXPECT_V4=1 -o $@ test_opcodes.c $(ALL_SRCS)

# Dictionary encoding, lookup and tokenizing. The test #includes zvibe_ops_text.c
# to reach its static functions, so that file is left out of the link.
DICT_SRCS = $(filter-out %zvibe_ops_text.c,$(ALL_SRCS))

test_dict_v3: test_dict.c $(ALL_SRCS) $(HEADERS) $(TEST_STAMP)
	$(CC) $(CFLAGS) $(INCLUDES) -DEXPECT_V4=0 -o $@ test_dict.c $(DICT_SRCS)

test_dict_v4: test_dict.c $(ALL_SRCS) $(HEADERS) $(TEST_STAMP)
	$(CC) $(CFLAGS) $(INCLUDES) -DZVIBE_ENABLE_V4=1 -DEXPECT_V4=1 -o $@ test_dict.c $(DICT_SRCS)

# The same test under AddressSanitizer and UBSan: exact-size inputs make an
# over-read in the codec or the raw restore a hard failure.
test_save_restore_san: test_save_restore.c $(ALL_SRCS) $(DELTA_SRC) $(HEADERS) ../../same51/include/save_delta.h $(TEST_STAMP)
	$(CC) $(CFLAGS) -g -fsanitize=address,undefined -fno-sanitize-recover=undefined $(INCLUDES) $(DELTA_INC) -o $@ test_save_restore.c $(ALL_SRCS) $(DELTA_SRC)

# The delta codec exists as two identical copies (SAM E51 and RISC-V); they
# must not drift apart.
test-delta-copies:
	@cmp ../../same51/src/save_delta.c ../../riscv/serv_zvibe/common/sw/save_delta.c
	@cmp ../../same51/include/save_delta.h ../../riscv/serv_zvibe/common/sw/save_delta.h
	@echo "save_delta copies identical"

# ZVIBE_VERIFY_SAVE_IDENTITY defaults: on for hosted builds and any V4 build,
# off for freestanding / minimal V3-only firmware (and overridable)
test-config-identity:
	@check() { \
	  want=$$1; shift; \
	  got=$$(printf '#include "zvibe_config.h"\nint v = ZVIBE_VERIFY_SAVE_IDENTITY;\n' | $(CC) -I../../../include "$$@" -E -x c - | tail -1 | sed 's/.*= \([01]\);/\1/'); \
	  [ "$$got" = "$$want" ] || { echo "FAIL: ZVIBE_VERIFY_SAVE_IDENTITY with [$$*] is $$got, expected $$want"; exit 1; }; \
	}; \
	check 1; \
	check 1 -DZVIBE_ENABLE_V4=1; \
	check 0 -DZVIBE_FREESTANDING=1; \
	check 0 -DZVIBE_MINIMAL_FEATURES; \
	check 1 -DZVIBE_ENABLE_V4=1 -DZVIBE_FREESTANDING=1; \
	check 1 -DZVIBE_ENABLE_V4=1 -DZVIBE_MINIMAL_FEATURES; \
	check 1 -DZVIBE_FREESTANDING=1 -DZVIBE_VERIFY_SAVE_IDENTITY=1; \
	check 0 -DZVIBE_VERIFY_SAVE_IDENTITY=0
	@echo "ZVIBE_VERIFY_SAVE_IDENTITY defaults OK"

# zvibe_config.h must reject anything but a literal 0 or 1 for ZVIBE_ENABLE_V4
test-config-defines:
	@for v in yes 2 -1 "" 0x1 true 01 1L "(1)" "1+0" "1&&0" "0||1"; do \
	  if $(CC) -fsyntax-only -x c -I../../../include "-DZVIBE_ENABLE_V4=$$v" ../../../include/zvibe_config.h 2>/dev/null; then \
	    echo "FAIL: ZVIBE_ENABLE_V4='$$v' was accepted"; exit 1; fi; \
	done
	@for v in 0 1; do \
	  $(CC) -fsyntax-only -x c -I../../../include -DZVIBE_ENABLE_V4=$$v ../../../include/zvibe_config.h || { echo "FAIL: ZVIBE_ENABLE_V4=$$v rejected"; exit 1; }; \
	done
	@echo "ZVIBE_ENABLE_V4 validation OK"

# Ensure zvibe binaries are built before running integration tests
ensure-binaries:
	@if [ "$(ZVIBE_BUILD)" != "build" ] && { [ ! -f "$(BIN_DIR)/zvibe_minimal" ] || [ ! -f "$(BIN_DIR)/zvibe_console" ]; }; then \
		echo "$(BIN_DIR) is missing binaries. Build it first, e.g.:"; \
		echo "  make -C .. BUILD_DIR=$(ZVIBE_BUILD) EXTRA_CFLAGS=-DZVIBE_ENABLE_V4=1"; \
		exit 1; \
	fi
	@if [ ! -f "$(BIN_DIR)/zvibe_minimal" ]; then \
		echo "Building zvibe_minimal..."; \
		cd .. && $(MAKE) BUILD_DIR=$(ZVIBE_BUILD) minimal; \
	fi
	@if [ ! -f "$(BIN_DIR)/zvibe_console" ]; then \
		echo "Building zvibe_console..."; \
		cd .. && $(MAKE) BUILD_DIR=$(ZVIBE_BUILD) console; \
	fi
	@$(MAKE) --no-print-directory verify-console-config

# The console binaries carry a marker of the configuration they were built
# with. Integration tests must run against the configuration they claim:
# V3-only (EXPECT_V4=0, the default) or V4-enabled (EXPECT_V4=1).
EXPECT_V4 ?= 0
EXPECT_STACK ?= 512
verify-console-config:
	@for b in zvibe_minimal zvibe_console; do \
	  strings -a "$(BIN_DIR)/$$b" | grep -q "zvibe-build:v4=$(EXPECT_V4):stack=$(EXPECT_STACK)$$" || { \
	    echo "FAIL: $(BIN_DIR)/$$b is not a ZVIBE_ENABLE_V4=$(EXPECT_V4), stack=$(EXPECT_STACK) build"; \
	    strings -a "$(BIN_DIR)/$$b" | grep "zvibe-build:" ; exit 1; }; \
	done
	@echo "console binaries in $(ZVIBE_BUILD)/ are ZVIBE_ENABLE_V4=$(EXPECT_V4), stack=$(EXPECT_STACK) builds"

# Run all tests (unit tests and integration tests)
test: test-unit test-integration
	@echo "=== All Tests Complete ==="

# Run unit tests
test-unit: $(TEST_TARGETS)
	@echo "=== Running Unit Tests ==="
	@echo
	@echo "Running split memory unit tests..."
	@./test_split_memory
	@echo
	@echo "Running V4 screen layer unit tests..."
	@./test_screen
	@echo
	@echo "Running build configuration tests (V3-only, then V3+V4)..."
	@./test_config_v3
	@./test_config_v4
	@./test_config_stack1024
	@$(MAKE) --no-print-directory test-config-defines
	@$(MAKE) --no-print-directory test-config-identity
	@echo
	@echo "Running restore safety tests (raw and delta saves)..."
	@./test_save_restore
	@./test_save_restore_v4_stack1024
	@./test_save_restore_san
	@echo
	@echo "Running object/property/address tests (V3-only, then V3+V4)..."
	@./test_objects_v3
	@./test_objects_v4
	@./test_dict_v3
	@./test_dict_v4
	@./test_opcodes_v3
	@./test_opcodes_v4
	@$(MAKE) --no-print-directory test-delta-copies
	@echo
	@echo "=== Unit Tests Complete ==="

# Run integration tests only
test-integration: ensure-binaries
	@echo "=== Running Integration Tests ==="
	@echo
	@echo "Running Czech Z3 functionality test..."
	@./test_czech.sh
	@echo
	@echo "Running Plundered Hearts integration test..."
	@./test_script.sh plunderedhearts
	@echo
	@echo "Running Seastalker integration test..."
	@./test_script.sh seastalker
	@echo
	@echo "Running Restaurant at the End of the Universe integration test..."
	@./test_script.sh restaurant
	@echo
	@echo "=== Integration Tests Complete ==="

# Run tests with verbose output (integration only)
test-verbose: test-integration
	@echo "=== Running zVibe Test Suite (Verbose) ==="

# Individual unit test targets
test-split-memory: test_split_memory
	@echo "Running split memory unit tests..."
	./test_split_memory

# Individual integration test targets
test-czech: ensure-binaries
	@echo "Running Czech Z3 functionality test..."
	./test_czech.sh

# V4 conformance gate: Czech built for V4, against a V4-enabled console build.
# Run with: make test-czech-v4 ZVIBE_BUILD=build_v4 EXPECT_V4=1
test-czech-v4: ensure-binaries
	@echo "Running Czech V4 conformance test..."
	./test_czech_v4.sh

test-plundered: ensure-binaries
	@echo "Running Plundered Hearts integration test..."
	./test_script.sh plunderedhearts

test-seastalker: ensure-binaries
	@echo "Running Seastalker integration test..."
	./test_script.sh seastalker

test-restaurant: ensure-binaries
	@echo "Running Restaurant at the End of the Universe integration test..."
	./test_script.sh restaurant

# Clean build artifacts
clean:
	rm -f $(TEST_TARGETS)
	rm -f core.*
	rm -f /tmp/zvibe_test_*
	rm -f czech_test_output.log
	rm -f test_output.log
	rm -f plunderedhearts_test_output.log
	rm -f seastalker_test_output.log
	rm -f restaurant_test_output.log
	rm -f czech-script.txt
	rm -f *.gcov *.gcda *.gcno
	rm -f *.profile gmon.out
	rm -f plunderedhearts.z3
	rm -f seastalker.z3

# Analysis targets
coverage: $(TEST_TARGETS)
	@echo "Running unit tests with coverage analysis..."
	$(CC) $(CFLAGS) $(INCLUDES) --coverage -o test_split_memory_cov test_split_memory.c $(CORE_DIR)/zvibe_memory.c
	./test_split_memory_cov
	gcov test_split_memory.c zvibe_memory.c

debug: 
	$(CC) $(CFLAGS) $(INCLUDES) -g -DDEBUG -o test_split_memory_debug test_split_memory.c $(CORE_DIR)/zvibe_memory.c

memcheck: $(TEST_TARGETS)
	@echo "Running unit tests with valgrind memory checking..."
	valgrind --leak-check=full --error-exitcode=1 ./test_split_memory

profile: $(TEST_TARGETS)
	@echo "Running unit tests with profiling..."
	$(CC) $(CFLAGS) $(INCLUDES) -pg -o test_split_memory_prof test_split_memory.c $(CORE_DIR)/zvibe_memory.c
	./test_split_memory_prof
	gprof test_split_memory_prof gmon.out > profile_report.txt

# Help target
help:
	@echo "zVibe Test Suite Build System"
	@echo "============================="
	@echo
	@echo "Main targets:"
	@echo "  all              - Build all unit tests"
	@echo "  test             - Run all unit and integration tests"
	@echo "  test-unit        - Run unit tests only"
	@echo "  test-integration - Run integration tests only"
	@echo "  test-verbose     - Run integration tests with verbose output"
	@echo "  clean            - Remove all build artifacts"
	@echo
	@echo "Individual tests:"
	@echo "  test-split-memory - Run split memory unit tests"
	@echo "  test-czech        - Run Czech Z3 functionality test"
	@echo "  test-plundered    - Run Plundered Hearts integration test"
	@echo "  test-seastalker   - Run Seastalker integration test"
	@echo "  test-restaurant   - Run Restaurant at the End of the Universe test"
	@echo
	@echo "Analysis targets:"
	@echo "  debug            - Build unit tests with debug symbols"
	@echo "  coverage         - Run unit tests with coverage analysis"
	@echo "  memcheck         - Run unit tests with valgrind memory checking"
	@echo "  profile          - Run unit tests with performance profiling"
	@echo "  help             - Show this help message"
	@echo
	@echo "Examples:"
	@echo "  make test                    # Run all unit and integration tests"
	@echo "  make test-unit               # Run unit tests only"
	@echo "  make test-split-memory       # Run split memory unit tests"
	@echo "  make test-czech              # Run Czech functionality test"
	@echo "  make test-plundered          # Run Plundered Hearts test"
	@echo "  make test-seastalker         # Run Seastalker test"
	@echo "  make test-restaurant         # Run Restaurant at the End of the Universe test"
	@echo "  make coverage                # Run unit tests with coverage analysis"

# Phony targets
.PHONY: test-config-identity test-czech-v4 verify-console-config test-delta-copies test-config-defines all test test-unit test-integration test-verbose clean test-split-memory test-czech test-plundered test-seastalker test-restaurant coverage debug memcheck profile help ensure-binaries
