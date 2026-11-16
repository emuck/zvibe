# Copyright (c) 2025 Martin R. Raumann
# SPDX-License-Identifier: BSD-3-Clause
# Host tests of the SAM E51 save store (save_store.c) against a model of the SmartEEPROM.
#   make test            build and run
#   make test SAN=1      with AddressSanitizer and UBSan
CC ?= cc
CFLAGS = -Wall -Wextra -O1 -g -std=gnu99
ifeq ($(SAN),1)
CFLAGS += -fsanitize=address,undefined -fno-omit-frame-pointer
endif
INC = -I../../include -I.

test_save_store: test_save_store.c mock_smarteeprom.c mock_smarteeprom.h ../../src/save_store.c ../../include/save_store.h ../../include/smarteeprom.h
	$(CC) $(CFLAGS) $(INC) -o $@ test_save_store.c mock_smarteeprom.c ../../src/save_store.c

.PHONY: test clean
test: test_save_store
	./test_save_store

clean:
	rm -rf test_save_store test_save_store.dSYM
