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
SESSION_SRC = ../webterm_session.c ../cartographer.c

# test sources
TEST_SRCS = test_session.c mock_platform.c

TEST_BIN = $(BIN_DIR)/test_session

.PHONY: all test test-unit test-czech test-v4 test-screen test-server clean help

all: $(TEST_BIN)

directories:
	@mkdir -p $(BIN_DIR) $(OBJ_DIR)

$(TEST_BIN): directories $(CORE_SRCS) $(SESSION_SRC) $(TEST_SRCS)
	$(CC) $(CFLAGS) $(INCLUDES) \
		$(CORE_SRCS) $(SESSION_SRC) $(TEST_SRCS) \
		-o $@

# The same suite with version 4 built in, plus the V4 session tests (read_char
# as a key message, saves over 24 KB, no cartographer events for V4 stories).
V4_BIN = $(BIN_DIR)/test_session_v4

SCREEN_SRC = $(ROOT_DIR)/src/screen/zvibe_screen.c

$(V4_BIN): directories $(CORE_SRCS) $(SCREEN_SRC) $(SESSION_SRC) $(TEST_SRCS)
	$(CC) $(CFLAGS) -DZVIBE_ENABLE_V4=1 -DZVIBE_MEMORY_BUFFER_SIZE=73728 $(INCLUDES) \
		$(CORE_SRCS) $(SCREEN_SRC) $(SESSION_SRC) $(TEST_SRCS) \
		-o $@

test-v4: $(V4_BIN)
	@echo "=== Running webterm session tests (V4 build) ==="
	@$(V4_BIN) $(GAMES_DIR)

# The V4 screen path: the session's screen frames drawn by the console tests'
# VT100 emulator against the screen layer driven directly, and zterm.js (the
# browser's emulator) against the same C emulator (needs node; skipped without).
SCREEN_STORY = $(ROOT_DIR)/target/console/tests/data/v4screen.z4
DUMP_BIN  = $(BIN_DIR)/ansi_dump
SCREEN_BIN = $(BIN_DIR)/test_screen_session
V4_DEFS   = -DZVIBE_ENABLE_V4=1 -DZVIBE_MEMORY_BUFFER_SIZE=73728

$(DUMP_BIN): directories ansi_dump.c $(CORE_SRCS) $(SCREEN_SRC) $(API_DIR)/zvibe_api_file.c
	$(CC) $(CFLAGS) $(V4_DEFS) $(INCLUDES) ansi_dump.c $(CORE_SRCS) $(SCREEN_SRC) $(API_DIR)/zvibe_api_file.c -o $@

$(SCREEN_BIN): directories test_screen_session.c mock_platform.c $(CORE_SRCS) $(SCREEN_SRC) $(SESSION_SRC)
	$(CC) $(CFLAGS) $(V4_DEFS) $(INCLUDES) test_screen_session.c mock_platform.c $(CORE_SRCS) $(SCREEN_SRC) $(SESSION_SRC) -o $@

test-screen: $(DUMP_BIN) $(SCREEN_BIN)
	@echo "=== Running webterm V4 screen tests ==="
	@$(DUMP_BIN) story $(SCREEN_STORY) $(BUILD_DIR)/ref
	@$(SCREEN_BIN) $(BUILD_DIR)/ref.grid $(SCREEN_STORY)
	@if command -v node >/dev/null 2>&1; then node test_zterm.js $(DUMP_BIN) $(SCREEN_STORY); \
	 else echo "(node not found: skipping the zterm.js check)"; fi

# The POSIX server's game list, run under AddressSanitizer (needs python3)
SERVER_SRCS = ../platform/posix/main.c ../webterm_session.c ../cartographer.c $(CORE_SRCS) $(SCREEN_SRC)
SERVER_SAN  = $(BIN_DIR)/zvibe_webterm_san

$(SERVER_SAN): directories $(SERVER_SRCS) ../platform/posix/main.c
	$(CC) -g -O1 -fsanitize=address,undefined $(V4_DEFS) $(INCLUDES) $(SERVER_SRCS) -o $@

test-server: $(SERVER_SAN)
	@echo "=== Running the POSIX server game-list test (ASan) ==="
	@python3 test_server_games.py $(SERVER_SAN)

test: $(TEST_BIN) $(V4_BIN)
	@echo "=== Running webterm session tests ==="
	@$(TEST_BIN) $(GAMES_DIR)
	@$(MAKE) --no-print-directory test-v4
	@$(MAKE) --no-print-directory test-screen
	@$(MAKE) --no-print-directory test-server
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
