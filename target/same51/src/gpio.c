/* Copyright (c) 2025 Martin R. Raumann */
/* SPDX-License-Identifier: BSD-3-Clause */
/*******************************************************************************
  GPIO Driver - Consolidated replacement for plib_port.c and plib_eic.c
  
  Provides only the exact functionality needed by the zvibe application:
  - PORT_Initialize() - Hardware pin configuration 
  - EIC_Initialize() - External interrupt controller (button PA15)
  - EIC_CallbackRegister() - Button interrupt callback registration
  - EIC interrupt handler for EXTINT 15
*******************************************************************************/

#include "board_support.h"

/* EIC callback object for pin 15 (PA15 button) */
static volatile struct {
    void (*callback)(uintptr_t context);
    uintptr_t context;
} eicCallback15 = {NULL, 0};

void PORT_Initialize(void)
{
    /* GROUP 0 (Port A) - LED and Button pins */
    PORT_REGS->GROUP[0].PORT_DIR = 0x4000U;      /* PA14=output, others input */
    PORT_REGS->GROUP[0].PORT_OUT = 0xc000U;      /* PA14=1 (LED off), PA15=1 (pullup) */
    PORT_REGS->GROUP[0].PORT_PINCFG[14] = 0x0U;  /* PA14: GPIO mode */
    PORT_REGS->GROUP[0].PORT_PINCFG[15] = 0x5U;  /* PA15: input+pullup+interrupt */
    PORT_REGS->GROUP[0].PORT_PMUX[7] = 0x0U;     /* PA14/PA15: GPIO mode */

    /* GROUP 1 (Port B) - UART pins */
    PORT_REGS->GROUP[1].PORT_PINCFG[16] = 0x1U;  /* PB16: peripheral mode */
    PORT_REGS->GROUP[1].PORT_PINCFG[17] = 0x1U;  /* PB17: peripheral mode */
    PORT_REGS->GROUP[1].PORT_PMUX[8] = 0x22U;    /* PB16/17: function C (SERCOM5) */
}

void EIC_Initialize(void)
{
    /* Reset EIC module */
    EIC_REGS->EIC_CTRLA |= (uint8_t)EIC_CTRLA_SWRST_Msk;
    while((EIC_REGS->EIC_SYNCBUSY & EIC_SYNCBUSY_SWRST_Msk) == EIC_SYNCBUSY_SWRST_Msk) {
        /* Wait for reset sync */
    }

    /* Use ultra low power clock */
    EIC_REGS->EIC_CTRLA |= (uint8_t)EIC_CTRLA_CKSEL_Msk;

    /* Configure EXTINT channels 0-7: all disabled */
    EIC_REGS->EIC_CONFIG[0] = 0;
    
    /* Configure EXTINT channels 8-15: only EXTINT15 enabled for rising edge with filter */
    EIC_REGS->EIC_CONFIG[1] = EIC_CONFIG_SENSE7_RISE | EIC_CONFIG_FILTEN7_Msk;

    /* Enable debouncer for EXTINT15 */
    EIC_REGS->EIC_DEBOUNCEN = 0x8000U;

    /* Debouncer prescaler: no division */  
    EIC_REGS->EIC_DPRESCALER = EIC_DPRESCALER_PRESCALER0(0UL) | EIC_DPRESCALER_PRESCALER1(0UL);

    /* Enable EXTINT15 interrupt */
    EIC_REGS->EIC_INTENSET = 0x8000U;

    /* Enable EIC module */
    EIC_REGS->EIC_CTRLA |= (uint8_t)EIC_CTRLA_ENABLE_Msk;
    while((EIC_REGS->EIC_SYNCBUSY & EIC_SYNCBUSY_ENABLE_Msk) == EIC_SYNCBUSY_ENABLE_Msk) {
        /* Wait for enable sync */
    }
}

void EIC_CallbackRegister(EIC_PIN pin, EIC_CALLBACK callback, uintptr_t context)
{
    if (pin == EIC_PIN_15) {
        eicCallback15.callback = callback;
        eicCallback15.context = context;
    }
}

void __attribute__((used)) EIC_EXTINT_15_InterruptHandler(void)
{
    /* Clear interrupt flag */
    EIC_REGS->EIC_INTFLAG = (1UL << 15);
    
    /* Call registered callback */
    if (eicCallback15.callback != NULL) {
        eicCallback15.callback(eicCallback15.context);
    }
}