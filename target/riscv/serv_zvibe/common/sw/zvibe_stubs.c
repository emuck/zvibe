/* Copyright (c) 2025 Martin R. Raumann */
/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * zvibe_stubs.c
 *
 * Minimal C library stubs for ZVibe embedded build
 * Provides essential functions without needing full newlib
 */

#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>

//=============================================================================
// Debug UART macros (for tracing stub function calls)
//=============================================================================
#define UART_STATUS (*(volatile uint32_t *)0x40000008)
#define UART_TX_DATA (*(volatile uint32_t *)0x40000000)
#define TX_READY (1 << 0)

//=============================================================================
// String functions
//=============================================================================

void *memset(void *s, int c, size_t n) {
    unsigned char *p = s;
    while (n--) *p++ = (unsigned char)c;
    return s;
}

void *memcpy(void *dest, const void *src, size_t n) {
    unsigned char *d = dest;
    const unsigned char *s = src;
    while (n--) *d++ = *s++;
    return dest;
}

int memcmp(const void *s1, const void *s2, size_t n) {
    const unsigned char *p1 = s1, *p2 = s2;
    while (n--) {
        if (*p1 != *p2) return *p1 - *p2;
        p1++; p2++;
    }
    return 0;
}

size_t strlen(const char *s) {
    size_t len = 0;
    while (s[len]) len++;
    return len;
}

char *strchr(const char *s, int c) {
    while (*s) {
        if (*s == (char)c) return (char *)s;
        s++;
    }
    return (c == 0) ? (char *)s : NULL;
}

int strcmp(const char *s1, const char *s2) {
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(unsigned char *)s1 - *(unsigned char *)s2;
}

char *strcpy(char *dest, const char *src) {
    char *d = dest;
    while ((*d++ = *src++));
    return dest;
}

char *strncpy(char *dest, const char *src, size_t n) {
    size_t i;
    for (i = 0; i < n && src[i]; i++)
        dest[i] = src[i];
    for (; i < n; i++)
        dest[i] = '\0';
    return dest;
}

int snprintf(char *str, size_t size, const char *format, ...) {
    /* Minimal snprintf for integer formatting (used by status line) */
    if (!str || size == 0) return 0;

    va_list args;
    va_start(args, format);

    size_t pos = 0;
    const char *p = format;

    while (*p && pos < size - 1) {
        if (*p == '%') {
            p++;
            if (*p == 'd') {
                /* Format integer */
                int val = va_arg(args, int);
                char buf[12];  /* Enough for 32-bit int */
                int len = 0;
                int is_neg = 0;

                if (val < 0) {
                    is_neg = 1;
                    val = -val;
                }

                /* Convert to string (reverse order) */
                do {
                    buf[len++] = '0' + (val % 10);
                    val /= 10;
                } while (val > 0);

                /* Add negative sign */
                if (is_neg && pos < size - 1) str[pos++] = '-';

                /* Copy digits in correct order */
                while (len > 0 && pos < size - 1) {
                    str[pos++] = buf[--len];
                }
            } else if (*p == 's') {
                /* Format string */
                const char *s = va_arg(args, const char *);
                if (s) {
                    while (*s && pos < size - 1) {
                        str[pos++] = *s++;
                    }
                }
            } else if (*p == '0' && *(p+1) >= '0' && *(p+1) <= '9') {
                /* Handle %02d format for zero-padding */
                p++;  /* Skip '0' */
                int width = *p - '0';
                p++;  /* Skip width digit */
                if (*p == 'd') {
                    int val = va_arg(args, int);
                    char buf[12];
                    int len = 0;

                    do {
                        buf[len++] = '0' + (val % 10);
                        val /= 10;
                    } while (val > 0);

                    /* Pad with zeros */
                    while (len < width && pos < size - 1) {
                        str[pos++] = '0';
                        width--;
                    }

                    /* Copy digits */
                    while (len > 0 && pos < size - 1) {
                        str[pos++] = buf[--len];
                    }
                }
            }
            p++;
        } else {
            str[pos++] = *p++;
        }
    }

    str[pos] = '\0';
    va_end(args);
    return pos;
}

//=============================================================================
// Memory allocation (should NOT be called in embedded mode!)
//=============================================================================

void *malloc(size_t size) {
    /* Should never be called - ZVibe uses static buffers in ZVIBE_MINIMAL_FEATURES mode */
    (void)size;
    return NULL;  /* Return NULL to trigger error in caller */
}

void free(void *ptr) {
    /* Should never be called */
    (void)ptr;
}

//=============================================================================
// Time functions
//=============================================================================

long time(long *tloc) {
    /* Return a dummy time value - not critical for Z-machine */
    static long dummy_time = 1234567890;
    if (tloc) *tloc = dummy_time;
    return dummy_time++;
}

//=============================================================================
// I/O functions (stubs - should not be called in embedded mode)
//=============================================================================

/* These are only used by error_callback and debug functions */
/* They should be disabled in ZVIBE_MINIMAL_FEATURES builds */

struct _reent;  /* Forward declaration for newlib compatibility */

/* File I/O stubs - should not be called in embedded builds */
typedef struct FILE FILE;

FILE *fopen(const char *filename, const char *mode) {
    (void)filename; (void)mode;
    return NULL;
}

int fseek(FILE *stream, long offset, int whence) {
    (void)stream; (void)offset; (void)whence;
    return -1;
}

long ftell(FILE *stream) {
    (void)stream;
    return -1;
}

int fclose(FILE *stream) {
    (void)stream;
    return -1;
}

size_t fread(void *ptr, size_t size, size_t nmemb, FILE *stream) {
    (void)ptr; (void)size; (void)nmemb; (void)stream;
    return 0;
}

size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream) {
    (void)ptr; (void)size; (void)nmemb; (void)stream;
    return 0;
}

int fputs(const char *s, void *stream) {
    (void)s; (void)stream;
    return 0;
}

int fputc(int c, void *stream) {
    (void)c; (void)stream;
    return c;
}

int vfprintf(void *stream, const char *format, va_list ap) {
    (void)stream; (void)format; (void)ap;
    return 0;
}

int printf(const char *format, ...) {
    (void)format;
    return 0;
}

int vprintf(const char *format, va_list ap) {
    (void)format; (void)ap;
    return 0;
}

int puts(const char *s) {
    (void)s;
    return 0;
}

int putchar(int c) {
    (void)c;
    return c;
}

void exit(int status) {
    (void)status;
    /* Loop forever on exit */
    while (1);
}

/* Newlib _impure_ptr stub */
void *_impure_ptr = NULL;
