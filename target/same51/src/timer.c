/* Copyright (c) 2025 Martin R. Raumann */
/* SPDX-License-Identifier: BSD-3-Clause */
/*******************************************************************************
  Timer Driver - Consolidated RTC Functions
  
  This file replaces peripheral/rtc/plib_rtc_timer.c with simplified direct
  register programming while maintaining identical timer behavior.
  
  Target: SAM E51 Curiosity Nano Board
  Configuration:
  - RTC in MODE0 (32-bit timer mode)
  - Prescaler = 1 (1KHz clock from 32KHz source)
  - Compare 0 used for 50ms periodic interrupts
  - Auto-clear on match enabled
  
  Functions Used:
  - RTC_Initialize(): Configure RTC as 32-bit timer
  - RTC_Timer32Start(): Enable RTC counting
  - RTC_Timer32CounterSet(): Reset counter value
  - RTC_Timer32Compare0Set(): Set compare value for interrupts
  - RTC_Timer32CallbackRegister(): Register interrupt callback
  - RTC_InterruptHandler(): Interrupt service routine
*******************************************************************************/

#include "board_support.h"

volatile static RTC_OBJECT rtcObj;

/*******************************************************************************
  Function: RTC_Initialize
  
  Summary: Initializes RTC as 32-bit timer with exact same configuration as original
  
  Description: 
    Configures RTC in MODE0 (32-bit timer) with 1KHz clock and auto-clear on match.
    Sets up Compare 0 for periodic interrupts and enables interrupt.
*******************************************************************************/
void RTC_Initialize(void)
{
    /* Software reset RTC */
    RTC_REGS->MODE0.RTC_CTRLA = RTC_MODE0_CTRLA_SWRST_Msk;

    while((RTC_REGS->MODE0.RTC_SYNCBUSY & RTC_MODE0_SYNCBUSY_SWRST_Msk) == RTC_MODE0_SYNCBUSY_SWRST_Msk)
    {
        /* Wait for Synchronization after Software Reset */
    }

    /* Configure RTC:
     * - MODE0: 32-bit timer mode
     * - PRESCALER=0x1: Divide by 2 (32KHz -> 16KHz)  
     * - COUNTSYNC: Count synchronization enabled
     * - MATCHCLR: Auto-clear counter on match
     */
    RTC_REGS->MODE0.RTC_CTRLA = (uint16_t)(RTC_MODE0_CTRLA_MODE(0UL) | 
                                          RTC_MODE0_CTRLA_PRESCALER(0x1UL) | 
                                          RTC_MODE0_CTRLA_COUNTSYNC_Msk |
                                          RTC_MODE0_CTRLA_MATCHCLR_Msk);
    
    while((RTC_REGS->MODE0.RTC_SYNCBUSY & RTC_MODE0_SYNCBUSY_COUNTSYNC_Msk) == RTC_MODE0_SYNCBUSY_COUNTSYNC_Msk)
    {
       /* Wait for Synchronization */
    }

    /* Set Compare 0 to 0x200 (512 decimal) 
     * With 16KHz clock: 512 ticks = 32ms period */
    RTC_REGS->MODE0.RTC_COMP[0] = 0x200U;

    while((RTC_REGS->MODE0.RTC_SYNCBUSY & RTC_MODE0_SYNCBUSY_COMP0_Msk) == RTC_MODE0_SYNCBUSY_COMP0_Msk)
    {
        /* Wait for Synchronization after writing Compare Value */
    }

    /* Set Compare 1 to 0 (unused) */
    RTC_REGS->MODE0.RTC_COMP[1] = 0x0U;

    while((RTC_REGS->MODE0.RTC_SYNCBUSY & RTC_MODE0_SYNCBUSY_COMP1_Msk) == RTC_MODE0_SYNCBUSY_COMP1_Msk)
    {
        /* Wait for Synchronization after writing Compare Value */
    }

    /* Enable Compare 0 interrupt (bit 8 = CMP0) */
    RTC_REGS->MODE0.RTC_INTENSET = 0x100U;
}

/*******************************************************************************
  Timer Control Functions
*******************************************************************************/

void RTC_Timer32Start(void)
{
    RTC_REGS->MODE0.RTC_CTRLA |= RTC_MODE0_CTRLA_ENABLE_Msk;

    while((RTC_REGS->MODE0.RTC_SYNCBUSY & RTC_MODE0_SYNCBUSY_ENABLE_Msk) == RTC_MODE0_SYNCBUSY_ENABLE_Msk)
    {
        /* Wait for synchronization after Enabling RTC */
    }
}

void RTC_Timer32CounterSet(uint32_t count)
{
    RTC_REGS->MODE0.RTC_COUNT = count;

    while((RTC_REGS->MODE0.RTC_SYNCBUSY & RTC_MODE0_SYNCBUSY_COUNT_Msk) == RTC_MODE0_SYNCBUSY_COUNT_Msk)
    {
        /* Wait for Synchronization after writing value to Count Register */
    }
}

void RTC_Timer32Compare0Set(uint32_t compareValue)
{
    RTC_REGS->MODE0.RTC_COMP[0] = compareValue;

    while((RTC_REGS->MODE0.RTC_SYNCBUSY & RTC_MODE0_SYNCBUSY_COMP0_Msk) == RTC_MODE0_SYNCBUSY_COMP0_Msk)
    {
        /* Wait for Synchronization after writing Compare Value */
    }
}

/*******************************************************************************
  Callback and Interrupt Functions
*******************************************************************************/

void RTC_Timer32CallbackRegister(RTC_TIMER32_CALLBACK callback, uintptr_t context)
{
    rtcObj.timer32BitCallback = callback;
    rtcObj.context            = context;
}

void __attribute__((used)) RTC_InterruptHandler(void)
{
    rtcObj.timer32intCause = (RTC_TIMER32_INT_MASK) RTC_REGS->MODE0.RTC_INTFLAG;
    RTC_REGS->MODE0.RTC_INTFLAG = (uint16_t)RTC_MODE0_INTFLAG_Msk;
    (void)RTC_REGS->MODE0.RTC_INTFLAG;

    /* Invoke registered Callback function */
    if(rtcObj.timer32BitCallback != NULL)
    {
        RTC_TIMER32_INT_MASK timer32intCause = rtcObj.timer32intCause;
        uintptr_t context = rtcObj.context;
        rtcObj.timer32BitCallback(timer32intCause, context);
    }
}