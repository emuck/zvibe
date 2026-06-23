/* Copyright (c) 2025 Martin R. Raumann */
/* SPDX-License-Identifier: BSD-3-Clause */
/*******************************************************************************
  System Header - Consolidated replacement for plib_nvic.h and plib_evsys.h
  
  Provides system-level function declarations.
*******************************************************************************/

#ifndef SYSTEM_H
#define SYSTEM_H

/* Function declarations */
void NVIC_Initialize(void);
void EVSYS_Initialize(void);

#endif /* SYSTEM_H */