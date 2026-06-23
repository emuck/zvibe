/* Copyright (c) 2025 Martin R. Raumann */
/* SPDX-License-Identifier: BSD-3-Clause */
/*******************************************************************************
  UART Driver - Consolidated SERCOM5 USART Functions
  
  This file replaces peripheral/sercom/usart/plib_sercom5_usart.c with simplified
  direct register programming while maintaining identical UART behavior.
  
  Target: SAM E51 Curiosity Nano Board
  UART Configuration:
  - SERCOM5 at 115200 baud (8N1)
  - RX: PB16 (SERCOM5/PAD[0])
  - TX: PB17 (SERCOM5/PAD[1])
  - Clock: GCLK1 (60MHz)
  - Interrupt-driven receive with callback support
*******************************************************************************/

#include "board_support.h"

/*******************************************************************************
  Local Data
*******************************************************************************/

/* SERCOM5 USART baud value for 115200 Hz baud rate */
#define SERCOM5_USART_INT_BAUD_VALUE            (63522UL)

static SERCOM5_USART_RING_BUFFER_CALLBACK SERCOM5_USART_ReadCallback = NULL;
static uintptr_t SERCOM5_USART_ReadContext;

/*******************************************************************************
  Internal Functions
*******************************************************************************/

void static SERCOM5_USART_ErrorClear(void)
{
    uint8_t  u8dummyData = 0U;
    USART_ERROR errorStatus = (USART_ERROR) (SERCOM5_REGS->USART_INT.SERCOM_STATUS & (uint16_t)(SERCOM_USART_INT_STATUS_PERR_Msk | SERCOM_USART_INT_STATUS_FERR_Msk | SERCOM_USART_INT_STATUS_BUFOVF_Msk ));

    if(errorStatus != USART_ERROR_NONE)
    {
        /* Clear error flag */
        SERCOM5_REGS->USART_INT.SERCOM_INTFLAG = (uint8_t)SERCOM_USART_INT_INTFLAG_ERROR_Msk;
        /* Clear all errors */
        SERCOM5_REGS->USART_INT.SERCOM_STATUS = (uint16_t)(SERCOM_USART_INT_STATUS_PERR_Msk | SERCOM_USART_INT_STATUS_FERR_Msk | SERCOM_USART_INT_STATUS_BUFOVF_Msk);

        /* Flush existing error bytes from the RX FIFO */
        while((SERCOM5_REGS->USART_INT.SERCOM_INTFLAG & (uint8_t)SERCOM_USART_INT_INTFLAG_RXC_Msk) == (uint8_t)SERCOM_USART_INT_INTFLAG_RXC_Msk)
        {
            u8dummyData = (uint8_t)SERCOM5_REGS->USART_INT.SERCOM_DATA;
        }
    }

    /* Ignore the warning */
    (void)u8dummyData;
}

/*******************************************************************************
  Function: SERCOM5_USART_Initialize
  
  Summary: Initializes SERCOM5 as UART with exact same configuration as original
  
  Description: 
    Configures SERCOM5 for 115200 baud UART operation:
    - Internal clock mode from GCLK1 (60MHz)
    - 8-bit data, 1 stop bit, no parity
    - RX on PAD[0] (PB16), TX on PAD[1] (PB17)
    - Interrupt-driven receive enabled
    - DMA-compatible for transmit
    
    Critical: Exact register programming sequence preserved!
*******************************************************************************/
void SERCOM5_USART_Initialize(void)
{
    /*
     * Configures USART Clock Mode
     * Configures TXPO and RXPO
     * Configures Data Order
     * Configures Standby Mode
     * Configures Sampling rate
     * Configures IBON
     */

    SERCOM5_REGS->USART_INT.SERCOM_CTRLA = SERCOM_USART_INT_CTRLA_MODE_USART_INT_CLK | SERCOM_USART_INT_CTRLA_RXPO(0x1UL) | SERCOM_USART_INT_CTRLA_TXPO(0x0UL) | SERCOM_USART_INT_CTRLA_DORD_Msk | SERCOM_USART_INT_CTRLA_IBON_Msk | SERCOM_USART_INT_CTRLA_FORM(0x0UL) | SERCOM_USART_INT_CTRLA_SAMPR(0UL) ;

    /* Configure Baud Rate */
    SERCOM5_REGS->USART_INT.SERCOM_BAUD = (uint16_t)SERCOM_USART_INT_BAUD_BAUD(SERCOM5_USART_INT_BAUD_VALUE);

    /*
     * Configures RXEN
     * Configures TXEN
     * Configures CHSIZE
     * Configures Parity
     * Configures Stop bits
     */
    SERCOM5_REGS->USART_INT.SERCOM_CTRLB = SERCOM_USART_INT_CTRLB_CHSIZE_8_BIT | SERCOM_USART_INT_CTRLB_SBMODE_1_BIT | SERCOM_USART_INT_CTRLB_TXEN_Msk | SERCOM_USART_INT_CTRLB_RXEN_Msk;

    /* Wait for sync */
    while((SERCOM5_REGS->USART_INT.SERCOM_SYNCBUSY) != 0U)
    {
        /* Do nothing */
    }

    /* Enable the UART after the configurations */
    SERCOM5_REGS->USART_INT.SERCOM_CTRLA |= SERCOM_USART_INT_CTRLA_ENABLE_Msk;

    /* Wait for sync */
    while((SERCOM5_REGS->USART_INT.SERCOM_SYNCBUSY) != 0U)
    {
        /* Do nothing */
    }
    
    /* Explicitly enable RX */
    SERCOM5_REGS->USART_INT.SERCOM_CTRLB |= SERCOM_USART_INT_CTRLB_RXEN_Msk;
    
    /* Wait for sync */
    while((SERCOM5_REGS->USART_INT.SERCOM_SYNCBUSY) != 0U)
    {
        /* Do nothing */
    }
    
    /* Enable RX Complete Interrupt */
    SERCOM5_REGS->USART_INT.SERCOM_INTENSET = SERCOM_USART_INT_INTENSET_RXC_Msk | SERCOM_USART_INT_INTENSET_ERROR_Msk;
    
    /* Enable SERCOM5 interrupt in NVIC */
    NVIC_DisableIRQ(SERCOM5_0_IRQn);
    NVIC_ClearPendingIRQ(SERCOM5_0_IRQn);
    NVIC_EnableIRQ(SERCOM5_0_IRQn);
}

/*******************************************************************************
  Public API Functions
*******************************************************************************/

void SERCOM5_USART_WriteByte(int data)
{
    /* Check if USART is ready for new data */
    while((SERCOM5_REGS->USART_INT.SERCOM_INTFLAG & SERCOM_USART_INT_INTFLAG_DRE_Msk) == 0U)
    {
        /* Do nothing */
    }

    SERCOM5_REGS->USART_INT.SERCOM_DATA = (uint16_t)data;
}

bool SERCOM5_USART_ReceiverIsReady(void)
{
    bool receiverStatus = false;

    if ((SERCOM5_REGS->USART_INT.SERCOM_INTFLAG & SERCOM_USART_INT_INTFLAG_RXC_Msk) == SERCOM_USART_INT_INTFLAG_RXC_Msk)
    {
        receiverStatus = true;
    }

    return receiverStatus;
}

int SERCOM5_USART_ReadByte(void)
{
    return (int)SERCOM5_REGS->USART_INT.SERCOM_DATA;
}

void SERCOM5_USART_ReadCallbackRegister(SERCOM5_USART_RING_BUFFER_CALLBACK callback, uintptr_t context)
{
    SERCOM5_USART_ReadCallback = callback;
    SERCOM5_USART_ReadContext = context;
}

void __attribute__((used)) SERCOM5_USART_InterruptHandler(void)
{
    if(SERCOM5_REGS->USART_INT.SERCOM_INTENSET != 0U)
    {
        uint8_t flags = SERCOM5_REGS->USART_INT.SERCOM_INTFLAG;

        if((flags & SERCOM_USART_INT_INTFLAG_RXC_Msk) == SERCOM_USART_INT_INTFLAG_RXC_Msk)
        {
            if(SERCOM5_USART_ReadCallback != NULL)
            {
                uintptr_t context = SERCOM5_USART_ReadContext;

                SERCOM5_USART_ReadCallback(context);
            }
        }

        if((flags & SERCOM_USART_INT_INTFLAG_ERROR_Msk) == SERCOM_USART_INT_INTFLAG_ERROR_Msk)
        {
            SERCOM5_USART_ErrorClear();
        }
    }
}