/* Copyright (c) 2025 Martin R. Raumann */
/* SPDX-License-Identifier: BSD-3-Clause */
/* Freestanding stdio.h for RISC-V embedded builds without newlib */
#ifndef ZVIBE_STDIO_H
#define ZVIBE_STDIO_H

#include <stddef.h>
#include <stdarg.h>

typedef void FILE;
#define EOF (-1)

int snprintf(char *str, size_t size, const char *format, ...);
int vfprintf(void *stream, const char *format, va_list ap);
int fputs(const char *s, void *stream);
int fputc(int c, void *stream);

int    fseek(FILE *stream, long offset, int whence);
long   ftell(FILE *stream);
int    fclose(FILE *stream);
size_t fread(void *ptr, size_t size, size_t nmemb, FILE *stream);
size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream);

#endif
