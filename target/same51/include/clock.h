/* Copyright (c) 2025 Martin R. Raumann */
/* SPDX-License-Identifier: BSD-3-Clause */
/*******************************************************************************
  Clock Header - Consolidated replacement for plib_clock.h
  
  Provides system clock initialization function for SAM E51.
  - DPLL0: 120MHz main system clock
  - GCLK0: 120MHz CPU clock  
  - GCLK1: 60MHz peripheral clock
  - GCLK2: 1MHz reference clock
*******************************************************************************/

#ifndef CLOCK_H
#define CLOCK_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* Function declarations */
void CLOCK_Initialize(void);

#endif /* CLOCK_H */