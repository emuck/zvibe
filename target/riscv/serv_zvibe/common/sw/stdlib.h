/* Copyright (c) 2025 Martin R. Raumann */
/* SPDX-License-Identifier: BSD-3-Clause */
/* Freestanding stdlib.h for RISC-V embedded builds without newlib */
#ifndef ZVIBE_STDLIB_H
#define ZVIBE_STDLIB_H

#include <stddef.h>

#define NULL ((void *)0)
#define EXIT_FAILURE 1
#define EXIT_SUCCESS 0

void *malloc(size_t size);
void  free(void *ptr);

#endif
