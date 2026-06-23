/* Copyright (c) 2025 Martin R. Raumann */
/* SPDX-License-Identifier: BSD-3-Clause */
/*******************************************************************************
  Clock Driver - Consolidated CLOCK Functions
  
  This file replaces peripheral/clock/plib_clock.c with simplified direct
  register programming while maintaining identical clock behavior.
  
  Target: SAM E51 Curiosity Nano Board
  Clock Configuration:
  - DPLL0: 120MHz (1MHz reference * 120 multiplier)
  - GCLK0: 120MHz CPU clock (SRC=DPLL0, DIV=1)
  - GCLK1: 60MHz peripheral clock (SRC=DPLL0, DIV=2) 
  - GCLK2: 1MHz clock (SRC=DFLL48M, DIV=48)
  - EIC uses GCLK1 (60MHz / 2^N for debouncing)
  - SERCOM5 uses GCLK1 (60MHz for 115200 baud UART)
  - Bridge clocks enabled for all peripherals
*******************************************************************************/

#include "board_support.h"

/*******************************************************************************
  Internal Clock Initialization Functions
*******************************************************************************/

static void OSCCTRL_Initialize(void)
{
    /* OSCCTRL not configured - using default settings */
}

static void OSC32KCTRL_Initialize(void)
{
    /* Configure 32KHz oscillator for RTC */
    /* RTCSEL=0: Use 32KHz Ultra Low Power Internal Oscillator */
    OSC32KCTRL_REGS->OSC32KCTRL_RTCCTRL = OSC32KCTRL_RTCCTRL_RTCSEL(0U);
}

static void DFLL_Initialize(void)
{
    /* DFLL48M not configured - using default 48MHz */
}

static void FDPLL0_Initialize(void)
{
    /* Enable GCLK2 (1MHz) as reference for DPLL0 */
    GCLK_REGS->GCLK_PCHCTRL[1] = GCLK_PCHCTRL_GEN(0x2U) | GCLK_PCHCTRL_CHEN_Msk;
    while ((GCLK_REGS->GCLK_PCHCTRL[1] & GCLK_PCHCTRL_CHEN_Msk) != GCLK_PCHCTRL_CHEN_Msk)
    {
        /* Wait for synchronization */
    }

    /****************** DPLL0 Initialization  *********************************/
    
    /* Configure DPLL Control B:
     * - FILTER=0: Bandwidth filter (default)
     * - LTIME=0: Lock time (default)  
     * - REFCLK=0: Reference clock selection (GCLK)
     */
    OSCCTRL_REGS->DPLL[0].OSCCTRL_DPLLCTRLB = OSCCTRL_DPLLCTRLB_FILTER(0U) | 
                                             OSCCTRL_DPLLCTRLB_LTIME(0x0U) | 
                                             OSCCTRL_DPLLCTRLB_REFCLK(0U);

    /* Configure DPLL Ratio:
     * - LDRFRAC=0: No fractional multiplier
     * - LDR=119: Integer multiplier (1MHz * 120 = 120MHz)
     */
    OSCCTRL_REGS->DPLL[0].OSCCTRL_DPLLRATIO = OSCCTRL_DPLLRATIO_LDRFRAC(0U) | 
                                             OSCCTRL_DPLLRATIO_LDR(119U);

    while((OSCCTRL_REGS->DPLL[0].OSCCTRL_DPLLSYNCBUSY & OSCCTRL_DPLLSYNCBUSY_DPLLRATIO_Msk) == OSCCTRL_DPLLSYNCBUSY_DPLLRATIO_Msk)
    {
        /* Waiting for the synchronization */
    }

    /* Enable DPLL */
    OSCCTRL_REGS->DPLL[0].OSCCTRL_DPLLCTRLA = OSCCTRL_DPLLCTRLA_ENABLE_Msk;

    while((OSCCTRL_REGS->DPLL[0].OSCCTRL_DPLLSYNCBUSY & OSCCTRL_DPLLSYNCBUSY_ENABLE_Msk) == OSCCTRL_DPLLSYNCBUSY_ENABLE_Msk)
    {
        /* Waiting for the DPLL enable synchronization */
    }

    /* Wait for DPLL to lock and be ready */
    while((OSCCTRL_REGS->DPLL[0].OSCCTRL_DPLLSTATUS & (OSCCTRL_DPLLSTATUS_LOCK_Msk | OSCCTRL_DPLLSTATUS_CLKRDY_Msk)) !=
                (OSCCTRL_DPLLSTATUS_LOCK_Msk | OSCCTRL_DPLLSTATUS_CLKRDY_Msk))
    {
        /* Waiting for the Ready state */
    }
}

static void GCLK0_Initialize(void)
{
    /* Configure CPU clock division = 1 (no division) */
    MCLK_REGS->MCLK_CPUDIV = MCLK_CPUDIV_DIV(0x01U);

    while((MCLK_REGS->MCLK_INTFLAG & MCLK_INTFLAG_CKRDY_Msk) != MCLK_INTFLAG_CKRDY_Msk)
    {
        /* Wait for the Main Clock to be Ready */
    }
    
    /* Configure GCLK0 (Main/CPU Clock):
     * - DIV=1: No division 
     * - SRC=7: DPLL0 (120MHz)
     * - GENEN: Generator enabled
     * Result: 120MHz CPU clock
     */
    GCLK_REGS->GCLK_GENCTRL[0] = GCLK_GENCTRL_DIV(1U) | 
                                GCLK_GENCTRL_SRC(7U) | 
                                GCLK_GENCTRL_GENEN_Msk;

    while((GCLK_REGS->GCLK_SYNCBUSY & GCLK_SYNCBUSY_GENCTRL_GCLK0) == GCLK_SYNCBUSY_GENCTRL_GCLK0)
    {
        /* wait for the Generator 0 synchronization */
    }
}

static void GCLK1_Initialize(void)
{
    /* Configure GCLK1 (Peripheral Clock):
     * - DIV=2: Divide by 2
     * - SRC=7: DPLL0 (120MHz)  
     * - GENEN: Generator enabled
     * Result: 60MHz peripheral clock (for UART, EIC, etc.)
     */
    GCLK_REGS->GCLK_GENCTRL[1] = GCLK_GENCTRL_DIV(2U) | 
                                GCLK_GENCTRL_SRC(7U) | 
                                GCLK_GENCTRL_GENEN_Msk;

    while((GCLK_REGS->GCLK_SYNCBUSY & GCLK_SYNCBUSY_GENCTRL_GCLK1) == GCLK_SYNCBUSY_GENCTRL_GCLK1)
    {
        /* wait for the Generator 1 synchronization */
    }
}

static void GCLK2_Initialize(void)
{
    /* Configure GCLK2 (Reference Clock):
     * - DIV=48: Divide by 48
     * - SRC=6: DFLL48M (48MHz)
     * - GENEN: Generator enabled  
     * Result: 1MHz reference clock (for DPLL0)
     */
    GCLK_REGS->GCLK_GENCTRL[2] = GCLK_GENCTRL_DIV(48U) | 
                                GCLK_GENCTRL_SRC(6U) | 
                                GCLK_GENCTRL_GENEN_Msk;

    while((GCLK_REGS->GCLK_SYNCBUSY & GCLK_SYNCBUSY_GENCTRL_GCLK2) == GCLK_SYNCBUSY_GENCTRL_GCLK2)
    {
        /* wait for the Generator 2 synchronization */
    }
}

/*******************************************************************************
  Function: CLOCK_Initialize
  
  Summary: Initializes complete clock system with exact same configuration as original
  
  Description: 
    Sets up the entire clock tree:
    1. Initialize oscillators (32KHz, DFLL48M)
    2. Configure GCLK2 (1MHz reference)
    3. Configure DPLL0 (120MHz PLL)
    4. Configure GCLK0 (120MHz CPU)
    5. Configure GCLK1 (60MHz peripherals)
    6. Route clocks to peripherals
    7. Enable bridge clocks
    
    Critical timing: Must follow exact sequence for PLL lock!
*******************************************************************************/
void CLOCK_Initialize(void)
{
    /* Initialize oscillators in correct order */
    OSCCTRL_Initialize();           /* Main oscillator setup */
    OSC32KCTRL_Initialize();        /* 32KHz oscillator for RTC */
    
    DFLL_Initialize();              /* 48MHz internal oscillator */
    GCLK2_Initialize();             /* 1MHz reference from DFLL48M */
    FDPLL0_Initialize();            /* 120MHz PLL from 1MHz reference */
    GCLK0_Initialize();             /* 120MHz CPU clock from DPLL0 */
    GCLK1_Initialize();             /* 60MHz peripheral clock from DPLL0 */

    /********************** Peripheral Clock Routing **********************/
    
    /* Route GCLK1 (60MHz) to EIC (External Interrupt Controller) */
    GCLK_REGS->GCLK_PCHCTRL[4] = GCLK_PCHCTRL_GEN(0x1U) | GCLK_PCHCTRL_CHEN_Msk;

    while ((GCLK_REGS->GCLK_PCHCTRL[4] & GCLK_PCHCTRL_CHEN_Msk) != GCLK_PCHCTRL_CHEN_Msk)
    {
        /* Wait for synchronization */
    }
    
    /* Route GCLK1 (60MHz) to SERCOM5_CORE (UART) */
    GCLK_REGS->GCLK_PCHCTRL[35] = GCLK_PCHCTRL_GEN(0x1U) | GCLK_PCHCTRL_CHEN_Msk;

    while ((GCLK_REGS->GCLK_PCHCTRL[35] & GCLK_PCHCTRL_CHEN_Msk) != GCLK_PCHCTRL_CHEN_Msk)
    {
        /* Wait for synchronization */
    }

    /********************** Bridge Clock Configuration **********************/
    
    /* Configure AHB Bridge Clocks - enable all peripherals */
    MCLK_REGS->MCLK_AHBMASK = 0xffffffU;

    /* Configure APBA Bridge Clocks - enable all APBA peripherals */
    MCLK_REGS->MCLK_APBAMASK = 0x7ffU;

    /* Configure APBD Bridge Clocks - enable SERCOM5 */
    MCLK_REGS->MCLK_APBDMASK = 0x2U;
}