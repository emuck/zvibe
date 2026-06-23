/* Copyright (c) 2025 Martin R. Raumann */
/* SPDX-License-Identifier: BSD-3-Clause */
/* Freestanding string.h for RISC-V embedded builds without newlib */
#ifndef ZVIBE_STRING_H
#define ZVIBE_STRING_H

#include <stddef.h>

void  *memset(void *s, int c, size_t n);
void  *memcpy(void *dest, const void *src, size_t n);
int    memcmp(const void *s1, const void *s2, size_t n);
size_t strlen(const char *s);
char  *strchr(const char *s, int c);
int    strcmp(const char *s1, const char *s2);
char  *strcpy(char *dest, const char *src);
char  *strncpy(char *dest, const char *src, size_t n);

#endif
