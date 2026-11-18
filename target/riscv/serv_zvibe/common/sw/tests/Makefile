# Copyright (c) 2025 Martin R. Raumann
# SPDX-License-Identifier: BSD-3-Clause
# Host tests for the RISC-V firmware's version 4 player (see test_riscv_v4.c).
#
#   make test            build and run
#   make test SAN=1      with AddressSanitizer and UBSan

CC     ?= cc
ROOT    = ../../../../../..
SRC     = $(ROOT)/src
SW      = ..
ARTY_SW = ../../../boards/arty_s7_50/sw
EMU     = $(ROOT)/target/console/tests

# Same feature profile as the firmware (sw/Makefile ZVIBE_XIP_FLAGS), V4 on, the story
# buffer big enough for the largest test story (v4bigsave: 21,250 bytes of dynamic memory, and the core wants 1 KB more).
CFLAGS = -Wall -Wextra -Wno-unused-parameter -O1 -g -std=gnu99 \
         -DZVIBE_ENABLE_V4=1 -DZVIBE_MEMORY_BUFFER_SIZE=22528 -DZV_RAMFUNC= \
         -DZVIBE_MINIMAL_FEATURES -DZVIBE_ENABLE_FILE_IO=0 -DZVIBE_ENABLE_STATUS_LINE=0 \
         -DZVIBE_ENABLE_SAVE_RESTORE=1
ifeq ($(SAN),1)
CFLAGS += -fsanitize=address,undefined -fno-omit-frame-pointer
endif
INCLUDES = -I$(ROOT)/include -iquote $(SW) -iquote $(ARTY_SW) -I$(EMU)

CORE = $(wildcard $(SRC)/core/*.c) $(SRC)/api/zvibe_api.c $(SRC)/screen/zvibe_screen.c
FW   = $(SW)/zvibe_riscv_v4.c $(SW)/flash_save.c $(SW)/save_delta.c

test_riscv_v4: test_riscv_v4.c $(CORE) $(FW) $(SW)/zvibe_riscv_v4.h $(EMU)/vt100_emu.h
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ test_riscv_v4.c $(CORE) $(FW)

.PHONY: test clean
test: test_riscv_v4
	./test_riscv_v4

clean:
	rm -rf test_riscv_v4 test_riscv_v4.dSYM
