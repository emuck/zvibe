/* Copyright (c) 2025 Martin R. Raumann */
/* SPDX-License-Identifier: BSD-3-Clause */
/*******************************************************************************
  GPIO Header - Consolidated replacement for plib_port.h and plib_eic.h
  
  Provides LED0 macros and EIC (External Interrupt Controller) functions.
  - PA14: LED pin (active low on SAM E51 Curiosity Nano)  
  - PA15: Button pin with external interrupt (EIC pin 15)
*******************************************************************************/

#ifndef GPIO_H
#define GPIO_H

#include "same51_minimal.h"
#include <stdint.h>

/* LED0 macros for PA14 (active low LED) */
#define LED0_Set()               (PORT_REGS->GROUP[0].PORT_OUTSET = ((uint32_t)1U << 14U))
#define LED0_Clear()             (PORT_REGS->GROUP[0].PORT_OUTCLR = ((uint32_t)1U << 14U))
#define LED0_Toggle()            (PORT_REGS->GROUP[0].PORT_OUTTGL = ((uint32_t)1U << 14U))
#define LED0_Get()               (((PORT_REGS->GROUP[0].PORT_IN >> 14U)) & 0x01U)

/* EIC Pin definitions */
#define EIC_PIN_15               (15U)
typedef uint16_t EIC_PIN;

/* EIC Callback function type */
typedef void (*EIC_CALLBACK)(uintptr_t context);

/* Function declarations */
void PORT_Initialize(void);
void EIC_Initialize(void);
void EIC_CallbackRegister(EIC_PIN pin, EIC_CALLBACK callback, uintptr_t context);

#endif /* GPIO_H */