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
BUILD_DIR = ../build
BIN_DIR = $(BUILD_DIR)/bin

# Test executables
TEST_TARGETS = test_split_memory

# Integration test scripts
INTEGRATION_TESTS = test_czech.sh

# Default target
all: $(TEST_TARGETS)

# Unit test targets
test_split_memory: test_split_memory.c $(CORE_DIR)/zvibe_memory.c
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $^

# Ensure zvibe binaries are built before running integration tests
ensure-binaries:
	@if [ ! -f "$(BIN_DIR)/zvibe_minimal" ]; then \
		echo "Building zvibe_minimal..."; \
		cd .. && $(MAKE) minimal; \
	fi
	@if [ ! -f "$(BIN_DIR)/zvibe_console" ]; then \
		echo "Building zvibe_console..."; \
		cd .. && $(MAKE) console; \
	fi

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
.PHONY: all test test-unit test-integration test-verbose clean test-split-memory test-czech test-plundered test-seastalker test-restaurant coverage debug memcheck profile help ensure-binaries
