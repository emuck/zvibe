/* Copyright (c) 2025 Martin R. Raumann */
/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * GPIO Debug LEDs
 * Memory-mapped at 0x40000020
 * Bits 0-2 control LD3, LD4, LD5
 */

#ifndef GPIO_H
#define GPIO_H

#define GPIO_BASE 0x40000020

// LED bits
#define LED3 (1 << 0)  // Boot stub running
#define LED4 (1 << 1)  // XIP code running
#define LED5 (1 << 2)  // Additional debug

// Helper macros
#define GPIO_WRITE(val) (*(volatile unsigned int *)GPIO_BASE = (val))
#define GPIO_READ() (*(volatile unsigned int *)GPIO_BASE)
#define GPIO_SET(bits) GPIO_WRITE(GPIO_READ() | (bits))
#define GPIO_CLR(bits) GPIO_WRITE(GPIO_READ() & ~(bits))

#endif
