# Copyright (c) 2025 Martin R. Raumann
# SPDX-License-Identifier: BSD-3-Clause
# zvibe_webterm — POSIX development server
#
# Usage:
#   make          Build the server binary
#   make run      Build and run on http://localhost:8080/
#   make clean    Remove build artifacts
#
# Override GAME_DIR to point at .z3 files:
#   make run GAME_DIR=/path/to/games

CC      ?= gcc
CFLAGS  ?= -O2 -Wall -Wextra -std=c99 -pedantic

ZVIBE_ROOT := ../../../..
WEBTERM    := ../..

INCLUDES := \
    -I$(ZVIBE_ROOT)/include \
    -I$(WEBTERM)

SRCS := \
    main.c \
    $(WEBTERM)/webterm_session.c \
    $(ZVIBE_ROOT)/src/api/zvibe_api.c \
    $(ZVIBE_ROOT)/src/core/zvibe_core.c \
    $(ZVIBE_ROOT)/src/core/zvibe_memory.c \
    $(ZVIBE_ROOT)/src/core/zvibe_object.c \
    $(ZVIBE_ROOT)/src/core/zvibe_ops_base.c \
    $(ZVIBE_ROOT)/src/core/zvibe_ops_math.c \
    $(ZVIBE_ROOT)/src/core/zvibe_ops_mem.c \
    $(ZVIBE_ROOT)/src/core/zvibe_ops_text.c

TARGET  := zvibe_webterm

PORT     ?= 8080
HTML     ?= www/zvibe.html
SAVE     ?= /tmp/zvibe.sav
GAME_DIR ?= $(ZVIBE_ROOT)/games/catalog

.PHONY: all run clean

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) $(INCLUDES) $(SRCS) -o $@

run: $(TARGET)
	./$(TARGET) -p $(PORT) -w $(HTML) -s $(SAVE) $(GAME_DIR)

clean:
	rm -f $(TARGET)
