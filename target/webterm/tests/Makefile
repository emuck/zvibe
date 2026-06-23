# Copyright (c) 2025 Martin R. Raumann
# SPDX-License-Identifier: BSD-3-Clause
# Test Makefile for webterm target
#
# Usage:
#   make test           # build and run all tests
#   make test-unit      # unit tests only
#   make test-czech     # Czech conformance only (via unit test binary)
#   make clean

CC      = gcc
CFLAGS  = -Wall -Wextra -O2 -std=c99
INCLUDES = -I../../../include -I..

ROOT_DIR  = ../../..
CORE_DIR  = $(ROOT_DIR)/src/core
API_DIR   = $(ROOT_DIR)/src/api
GAMES_DIR = $(ROOT_DIR)/games/catalog

BUILD_DIR = build
BIN_DIR   = $(BUILD_DIR)/bin
OBJ_DIR   = $(BUILD_DIR)/obj

# zvibe core sources (all .c files)
CORE_SRCS = $(wildcard $(CORE_DIR)/*.c) $(API_DIR)/zvibe_api.c

# webterm session sources
SESSION_SRC = ../webterm_session.c

# test sources
TEST_SRCS = test_session.c mock_platform.c

TEST_BIN = $(BIN_DIR)/test_session

.PHONY: all test test-unit test-czech clean help

all: $(TEST_BIN)

directories:
	@mkdir -p $(BIN_DIR) $(OBJ_DIR)

$(TEST_BIN): directories $(CORE_SRCS) $(SESSION_SRC) $(TEST_SRCS)
	$(CC) $(CFLAGS) $(INCLUDES) \
		$(CORE_SRCS) $(SESSION_SRC) $(TEST_SRCS) \
		-o $@

test: $(TEST_BIN)
	@echo "=== Running webterm session tests ==="
	@$(TEST_BIN) $(GAMES_DIR)
	@echo "=== Tests complete ==="

test-unit: test

test-czech: $(TEST_BIN)
	@echo "=== Running Czech conformance via webterm session ==="
	@$(TEST_BIN) $(GAMES_DIR) 2>&1 | grep -E "czech|PASS|FAIL|passed|failed"

clean:
	rm -rf $(BUILD_DIR)

help:
	@echo "webterm test targets:"
	@echo "  test        - build and run all tests"
	@echo "  test-unit   - same as test"
	@echo "  test-czech  - run and filter for czech results"
	@echo "  clean       - remove build artifacts"
