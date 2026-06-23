/* Copyright (c) 2025 Martin R. Raumann */
/* SPDX-License-Identifier: BSD-3-Clause */
/*******************************************************************************
  DMA Driver - Consolidated DMAC Functions
  
  This file replaces peripheral/dmac/plib_dmac.c with simplified direct
  register programming while maintaining identical DMA behavior.
  
  Target: SAM E51 Curiosity Nano Board
  Configuration:
  - Only DMA Channel 0 used (for UART TX)
  - Trigger source 15 (SERCOM5 TX)
  - Byte transfers, source increment
  - Interrupt on transfer complete and error
  
  Functions Used:
  - DMAC_Initialize(): Configure DMA controller and Channel 0
  - DMAC_ChannelTransfer(): Start DMA transfer on Channel 0
  - DMAC_ChannelCallbackRegister(): Register transfer complete callback
  - DMAC Channel 0 Interrupt Handler: Handle transfer complete/error
*******************************************************************************/

#include "board_support.h"

#define DMAC_CHANNELS_NUMBER        (1U)

/* DMAC channels object configuration structure */
typedef struct
{
    bool                inUse;
    DMAC_CHANNEL_CALLBACK  callback;
    uintptr_t              context;
    bool                isBusy;
} DMAC_CH_OBJECT;

/* Initial write back memory section for DMAC */
static dmac_descriptor_registers_t write_back_section[DMAC_CHANNELS_NUMBER] __ALIGNED(8);

/* Descriptor section for DMAC */
static dmac_descriptor_registers_t descriptor_section[DMAC_CHANNELS_NUMBER] __ALIGNED(8);

/* DMAC Channels object information structure */
volatile static DMAC_CH_OBJECT dmacChannelObj[DMAC_CHANNELS_NUMBER];

/*******************************************************************************
  Function: DMAC_Initialize
  
  Summary: Initializes DMA controller with exact same configuration as original
  
  Description: 
    Sets up DMA Channel 0 for UART transmit with trigger source 15 (SERCOM5 TX).
    Configures byte transfers with source increment and interrupts enabled.
*******************************************************************************/
void DMAC_Initialize(void)
{
    volatile DMAC_CH_OBJECT *dmacChObj = &dmacChannelObj[0];
    uint32_t channel = 0U;

    /* Initialize DMAC Channel objects */
    for(channel = 0U; channel < DMAC_CHANNELS_NUMBER; channel++)
    {
        dmacChObj->inUse = false;
        dmacChObj->callback = NULL;
        dmacChObj->context = 0U;
        dmacChObj->isBusy = false;

        /* Point to next channel object */
        dmacChObj++;
    }

    /* Update the Base address and Write Back address register */
    DMAC_REGS->DMAC_BASEADDR = (uint32_t) descriptor_section;
    DMAC_REGS->DMAC_WRBADDR  = (uint32_t) write_back_section;

    /* Update the Priority Control register - all priority levels 1 with round robin */
    DMAC_REGS->DMAC_PRICTRL0 |= DMAC_PRICTRL0_LVLPRI0(1U) | DMAC_PRICTRL0_RRLVLEN0_Msk | 
                                DMAC_PRICTRL0_LVLPRI1(1U) | DMAC_PRICTRL0_RRLVLEN1_Msk | 
                                DMAC_PRICTRL0_LVLPRI2(1U) | DMAC_PRICTRL0_RRLVLEN2_Msk | 
                                DMAC_PRICTRL0_LVLPRI3(1U) | DMAC_PRICTRL0_RRLVLEN3_Msk;

    /***************** Configure DMA channel 0 for UART TX ********************/
    /* Channel Control:
     * - TRIGACT=2: Block transfer trigger action
     * - TRIGSRC=15: SERCOM5 TX trigger source  
     * - THRESHOLD=0: 1 beat threshold
     * - BURSTLEN=0: Single beat burst
     */
    DMAC_REGS->CHANNEL[0].DMAC_CHCTRLA = DMAC_CHCTRLA_TRIGACT(2U) | 
                                        DMAC_CHCTRLA_TRIGSRC(15U) | 
                                        DMAC_CHCTRLA_THRESHOLD(0U) | 
                                        DMAC_CHCTRLA_BURSTLEN(0U);

    /* Descriptor Control:
     * - BLOCKACT_INT: Generate interrupt when block transfer completes
     * - BEATSIZE_BYTE: 8-bit byte transfers
     * - VALID: Descriptor is valid
     * - SRCINC: Increment source address after each beat
     */
    descriptor_section[0].DMAC_BTCTRL = DMAC_BTCTRL_BLOCKACT_INT | 
                                       DMAC_BTCTRL_BEATSIZE_BYTE | 
                                       DMAC_BTCTRL_VALID_Msk | 
                                       DMAC_BTCTRL_SRCINC_Msk;

    /* Channel priority level 0 (highest) */
    DMAC_REGS->CHANNEL[0].DMAC_CHPRILVL = DMAC_CHPRILVL_PRILVL(0U);

    /* Mark channel 0 as in use */
    dmacChannelObj[0].inUse = true;

    /* Enable transfer complete and error interrupts for Channel 0 */
    DMAC_REGS->CHANNEL[0].DMAC_CHINTENSET = (DMAC_CHINTENSET_TERR_Msk | DMAC_CHINTENSET_TCMPL_Msk);

    /* Enable the DMAC module & all Priority Levels */
    DMAC_REGS->DMAC_CTRL = DMAC_CTRL_DMAENABLE_Msk | 
                          DMAC_CTRL_LVLEN0_Msk | 
                          DMAC_CTRL_LVLEN1_Msk | 
                          DMAC_CTRL_LVLEN2_Msk | 
                          DMAC_CTRL_LVLEN3_Msk;
}

/*******************************************************************************
  DMA Transfer Functions
*******************************************************************************/

bool DMAC_ChannelTransfer(DMAC_CHANNEL channel, const void *srcAddr, const void *destAddr, size_t blockSize)
{
    bool returnStatus = false;
    bool isBusy = dmacChannelObj[channel].isBusy;
    const uint32_t* pu32srcAddr = (const uint32_t*)srcAddr;
    const uint32_t* pu32dstAddr = (const uint32_t*)destAddr;

    /* Check if we can start a new transfer (same logic as original PLIB) */
    if (((DMAC_REGS->CHANNEL[channel].DMAC_CHINTFLAG & (DMAC_CHINTENCLR_TCMPL_Msk | DMAC_CHINTENCLR_TERR_Msk)) != 0U) || (!isBusy))
    {
        /* Clear the transfer complete flag */
        DMAC_REGS->CHANNEL[channel].DMAC_CHINTFLAG = DMAC_CHINTENCLR_TCMPL_Msk | DMAC_CHINTENCLR_TERR_Msk;

        dmacChannelObj[channel].isBusy = true;

        /* Get a pointer to the module hardware instance */
        dmac_descriptor_registers_t *const dmacDescReg = &descriptor_section[channel];

        /* Set source address */
        if ((dmacDescReg->DMAC_BTCTRL & DMAC_BTCTRL_SRCINC_Msk) != 0U)
        {
            dmacDescReg->DMAC_SRCADDR = ((uintptr_t)pu32srcAddr + blockSize);
        }
        else
        {
            dmacDescReg->DMAC_SRCADDR = (uintptr_t)(pu32srcAddr);
        }

        /* Set destination address */
        if ((dmacDescReg->DMAC_BTCTRL & DMAC_BTCTRL_DSTINC_Msk) != 0U)
        {
            dmacDescReg->DMAC_DSTADDR = ((uintptr_t)pu32dstAddr + blockSize);
        }
        else
        {
            dmacDescReg->DMAC_DSTADDR = (uintptr_t)(pu32dstAddr);
        }

        /* Calculate beat size for block count */
        uint8_t beat_size = (uint8_t)((dmacDescReg->DMAC_BTCTRL & DMAC_BTCTRL_BEATSIZE_Msk) >> DMAC_BTCTRL_BEATSIZE_Pos);
        
        /* Set block transfer count */
        dmacDescReg->DMAC_BTCNT = (uint16_t)(blockSize / (1UL << beat_size));

        /* Set the descriptor to be valid */
        dmacDescReg->DMAC_BTCTRL |= DMAC_BTCTRL_VALID_Msk;

        /* Enable the channel */
        DMAC_REGS->CHANNEL[channel].DMAC_CHCTRLA |= DMAC_CHCTRLA_ENABLE_Msk;

        returnStatus = true;
    }

    return returnStatus;
}

/*******************************************************************************
  Callback Functions
*******************************************************************************/

void DMAC_ChannelCallbackRegister(DMAC_CHANNEL channel, DMAC_CHANNEL_CALLBACK eventHandler, uintptr_t contextHandle)
{
    if (dmacChannelObj[channel].inUse == true)
    {
        dmacChannelObj[channel].callback = eventHandler;
        dmacChannelObj[channel].context = contextHandle;
    }
}

/*******************************************************************************
  Interrupt Handler for DMA Channel 0
*******************************************************************************/

void __attribute__((used)) DMAC_0_InterruptHandler(void)
{
    DMAC_TRANSFER_EVENT transferEvent = DMAC_TRANSFER_EVENT_ERROR;
    uint8_t channelIntFlagStatus = 0U;

    /* Get the channel interrupt flag status for Channel 0 */
    channelIntFlagStatus = (uint8_t)DMAC_REGS->CHANNEL[0].DMAC_CHINTFLAG;

    /* Check if transfer complete interrupt occurred */
    if ((channelIntFlagStatus & (uint8_t)DMAC_CHINTENSET_TCMPL_Msk) != 0U)
    {
        /* Clear transfer complete interrupt flag */
        DMAC_REGS->CHANNEL[0].DMAC_CHINTFLAG = (uint8_t)DMAC_CHINTENSET_TCMPL_Msk;

        transferEvent = DMAC_TRANSFER_EVENT_COMPLETE;
        dmacChannelObj[0].isBusy = false;
    }

    /* Check if transfer error interrupt occurred */
    if ((channelIntFlagStatus & (uint8_t)DMAC_CHINTENSET_TERR_Msk) != 0U)
    {
        /* Clear transfer error interrupt flag */
        DMAC_REGS->CHANNEL[0].DMAC_CHINTFLAG = (uint8_t)DMAC_CHINTENSET_TERR_Msk;

        transferEvent = DMAC_TRANSFER_EVENT_ERROR;
        dmacChannelObj[0].isBusy = false;
    }

    /* Execute the callback function */
    if ((dmacChannelObj[0].callback != NULL) && (dmacChannelObj[0].inUse == true))
    {
        uintptr_t contextHandle = dmacChannelObj[0].context;
        dmacChannelObj[0].callback(transferEvent, contextHandle);
    }
}