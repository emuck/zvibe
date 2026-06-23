/* Copyright (c) 2025 Martin R. Raumann */
/* SPDX-License-Identifier: BSD-3-Clause */
/*******************************************************************************
  System Driver - Consolidated System Functions
  
  This file replaces:
  - peripheral/nvic/plib_nvic.c (interrupt controller configuration)
  - peripheral/evsys/plib_evsys.c (event system - unused, empty function)
  
  With simplified direct register programming while maintaining identical behavior.
  
  Target: SAM E51 Curiosity Nano Board
  Interrupts Configured:
  - RTC_IRQn: Priority 7 (RTC timer interrupts)
  - EIC_EXTINT_15_IRQn: Priority 7 (Button PA15 external interrupt)  
  - DMAC_0_IRQn: Priority 7 (DMA channel 0 for UART)
  - System fault handlers enabled (Usage, Bus, MemManage faults)
  
  Event System: Not used in this application (EVSYS_Initialize is empty)
*******************************************************************************/

#include "board_support.h"

/*******************************************************************************
  Function: NVIC_Initialize
  
  Summary: Initializes interrupt controller with exact same configuration as original
  
  Description: 
    Configures interrupt priorities and enables required interrupts with identical
    register programming to the original Microchip PLIB implementation.
*******************************************************************************/
void NVIC_Initialize(void)
{
    /* Priority 0 to 7 and no sub-priority. 0 is the highest priority */
    NVIC_SetPriorityGrouping(0x00);

    /* Enable NVIC Controller */
    __DMB();
    __enable_irq();

    /* Enable the interrupt sources and configure the priorities as configured
     * from within the "Interrupt Manager" of MHC. */
    
    // RTC Timer interrupt - priority 7 (lowest)
    NVIC_SetPriority(RTC_IRQn, 7);
    NVIC_EnableIRQ(RTC_IRQn);
    
    // External interrupt 15 (Button PA15) - priority 7
    NVIC_SetPriority(EIC_EXTINT_15_IRQn, 7);
    NVIC_EnableIRQ(EIC_EXTINT_15_IRQn);
    
    // DMA Channel 0 (UART transmit) - priority 7
    NVIC_SetPriority(DMAC_0_IRQn, 7);
    NVIC_EnableIRQ(DMAC_0_IRQn);

    /* Enable Usage fault - traps undefined instructions, divide by zero, etc */
    SCB->SHCSR |= (SCB_SHCSR_USGFAULTENA_Msk);
    /* Trap divide by zero */
    SCB->CCR   |= SCB_CCR_DIV_0_TRP_Msk;

    /* Enable Bus fault - traps invalid memory accesses */
    SCB->SHCSR |= (SCB_SHCSR_BUSFAULTENA_Msk);

    /* Enable memory management fault - traps MPU violations */
    SCB->SHCSR |= (SCB_SHCSR_MEMFAULTENA_Msk);
}


/*******************************************************************************
  Function: EVSYS_Initialize
  
  Summary: Event system initialization (unused in this application)
  
  Description: Empty function - no event channels are configured or used.
*******************************************************************************/
void EVSYS_Initialize(void)
{
    /* Event system not used in this application - empty function */
}

/*******************************************************************************
  Function: SYS_Initialize

  Summary: Top-level board and peripheral initialization.

  Description:
    Called from main() before any application code runs. Order matches the
    original MCC Harmony SYS_Initialize sequence.
*******************************************************************************/
void SYS_Initialize(void *data)
{
    (void)data;
    NVMCTRL_Initialize();
    PORT_Initialize();
    CLOCK_Initialize();
    EVSYS_Initialize();
    DMAC_Initialize();
    SERCOM5_USART_Initialize();
    EIC_Initialize();
    RTC_Initialize();
    NVIC_Initialize();
}

/*******************************************************************************
  NVIC_SystemReset is provided by CMSIS core_cm4.h
*******************************************************************************/