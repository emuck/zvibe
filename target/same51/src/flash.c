/* Copyright (c) 2025 Martin R. Raumann */
/* SPDX-License-Identifier: BSD-3-Clause */
/*******************************************************************************
  Flash Driver - Consolidated NVMCTRL Functions
  
  This file replaces peripheral/nvmctrl/plib_nvmctrl.c with simplified direct
  register programming while maintaining identical flash and SmartEEPROM behavior.
  
  Target: SAM E51 Curiosity Nano Board
  Functions Used:
  - NVMCTRL_Initialize(): Configure flash wait states and auto-wait
  - NVMCTRL_Read(): Read from flash/user row
  - NVMCTRL_SetWriteMode(): Configure write mode for SmartEEPROM
  - NVMCTRL_IsBusy(): Check flash operation status
  - NVMCTRL_SmartEEPROM_IsBusy(): Check SmartEEPROM operation status
  - NVMCTRL_SmartEEPROMStatusGet(): Get SmartEEPROM configuration
  - NVMCTRL_SmartEEPROM_IsActiveSectorFull(): Check if active sector is full
  - NVMCTRL_SmartEEPROMSectorReallocate(): Reallocate SmartEEPROM sectors
  - NVMCTRL_SmartEEPROMFlushPageBuffer(): Flush SmartEEPROM page buffer
  - NVMCTRL_USER_ROW_*(): User row operations for fuse configuration
*******************************************************************************/

#include "board_support.h"
#include <string.h>

volatile static uint16_t nvm_error;

/*******************************************************************************
  Function: NVMCTRL_Initialize
  
  Summary: Initializes flash controller with exact same configuration as original
  
  Description: 
    Configures flash wait states (RWS=5) and enables auto wait state insertion.
    This ensures proper flash access timing at 120MHz CPU clock.
*******************************************************************************/
void NVMCTRL_Initialize(void)
{
    /* Configure Read Wait States = 5 and enable Auto Wait State insertion */
    NVMCTRL_REGS->NVMCTRL_CTRLA = (uint16_t)NVMCTRL_CTRLA_RWS(5U) | NVMCTRL_CTRLA_AUTOWS_Msk;
}

/*******************************************************************************
  Flash/Memory Read Operations
*******************************************************************************/

bool NVMCTRL_Read(uint32_t *data, uint32_t length, const uint32_t address)
{
    uint32_t* paddress = (uint32_t*)address;
    (void)memcpy(data, paddress, length);
    return true;
}

/*******************************************************************************
  SmartEEPROM Configuration Functions
*******************************************************************************/

void NVMCTRL_SetWriteMode(NVMCTRL_WRITEMODE mode)
{
    NVMCTRL_REGS->NVMCTRL_CTRLA = (uint16_t)((NVMCTRL_REGS->NVMCTRL_CTRLA & (~NVMCTRL_CTRLA_WMODE_Msk)) | (uint16_t)mode);
}

uint32_t NVMCTRL_SmartEEPROMStatusGet(void)
{
    return NVMCTRL_REGS->NVMCTRL_SEESTAT;
}

bool NVMCTRL_SmartEEPROM_IsBusy(void)
{
    return ((NVMCTRL_REGS->NVMCTRL_SEESTAT & NVMCTRL_SEESTAT_BUSY_Msk) != 0U);
}

/*******************************************************************************
  Flash Operation Status Functions
*******************************************************************************/

bool NVMCTRL_IsBusy(void)
{
    return (bool)((NVMCTRL_REGS->NVMCTRL_STATUS & NVMCTRL_STATUS_READY_Msk) == 0U);
}

uint16_t NVMCTRL_ErrorGet(void)
{
    return (uint16_t)(nvm_error);
}

uint16_t NVMCTRL_StatusGet(void)
{
    return (uint16_t)(NVMCTRL_REGS->NVMCTRL_STATUS);
}

/*******************************************************************************
  User Row Operations (for SmartEEPROM fuse configuration)
*******************************************************************************/

bool NVMCTRL_USER_ROW_PageWrite(uint32_t *data, const uint32_t address)
{
    uint32_t i;
    uint32_t * paddress = (uint32_t *)address;
    bool wr_status = false;

    /* Clear global error flag */
    nvm_error = 0U;

    /* writing 32-bit data into the given address */
    for (i = 0U; i < (NVMCTRL_USERROW_SIZE/4U); i++)
    {
        *paddress = data[i];
         paddress++;
    }

    /* Set address and command */
    NVMCTRL_REGS->NVMCTRL_ADDR = address >> 1U;

    NVMCTRL_REGS->NVMCTRL_CTRLB = NVMCTRL_CTRLB_CMD_WP | NVMCTRL_CTRLB_CMDEX_KEY;

    wr_status = true;

    return wr_status;
}

bool NVMCTRL_USER_ROW_RowErase(uint32_t address)
{
    bool er_status = false;

    /* Clear global error flag */
    nvm_error = 0U;

    /* Set address and command */
    NVMCTRL_REGS->NVMCTRL_ADDR = address >> 1U;

    NVMCTRL_REGS->NVMCTRL_CTRLB = NVMCTRL_CTRLB_CMD_EP | NVMCTRL_CTRLB_CMDEX_KEY;

    er_status = true;

    return er_status;
}

/*******************************************************************************
  Additional Flash Functions (for completeness - used by other code)
*******************************************************************************/

bool NVMCTRL_QuadWordWrite(const uint32_t *data, const uint32_t address)
{
    uint8_t i;
    bool wr_status = false;
    uint32_t * paddress = (uint32_t *)address;
    uint16_t wr_mode = (NVMCTRL_REGS->NVMCTRL_CTRLA & NVMCTRL_CTRLA_WMODE_Msk);

    /* Clear global error flag */
    nvm_error = 0U;

    /* If the address is not a quad word address, return error */
    if((address & 0x0fU) != 0U)
    {
        wr_status = false;
    }
    else
    {
        /* Configure Quad Word Write */
        NVMCTRL_SetWriteMode(NVMCTRL_WMODE_AQW);

        /* Writing 32-bit data into the given address.  Writes to the page buffer must be 32 bits. */
        for (i = 0U; i <= 3U; i++)
        {
            *paddress = data[i];
             paddress++;
        }
        /* Restore the write mode */
        NVMCTRL_SetWriteMode(wr_mode);
        wr_status = true;
    }

    return wr_status;
}

bool NVMCTRL_PageWrite(const uint32_t *data, const uint32_t address)
{
    uint32_t i;
    uint32_t * paddress = (uint32_t *)address;
    bool wr_status = false;

    /* Clear global error flag */
    nvm_error = 0U;

    /* writing 32-bit data into the given address */
    for (i = 0U; i < (NVMCTRL_FLASH_PAGESIZE/4U); i++)
    {
        *paddress = data[i];
         paddress++;
    }

    /* Set address and command */
    NVMCTRL_REGS->NVMCTRL_ADDR = address >> 1U;

    NVMCTRL_REGS->NVMCTRL_CTRLB = NVMCTRL_CTRLB_CMD_WP | NVMCTRL_CTRLB_CMDEX_KEY;

    wr_status = true;

    return wr_status;
}

bool NVMCTRL_BlockErase(uint32_t address)
{
    bool er_status = false;

    /* Clear global error flag */
    nvm_error = 0U;

    /* Set address and command */
    NVMCTRL_REGS->NVMCTRL_ADDR = address >> 1U;

    NVMCTRL_REGS->NVMCTRL_CTRLB = NVMCTRL_CTRLB_CMD_EB | NVMCTRL_CTRLB_CMDEX_KEY;

    er_status = true;

    return er_status;
}

/*******************************************************************************
  Additional SmartEEPROM Functions
  
  Summary: Additional SmartEEPROM support for sector management
*******************************************************************************/

bool NVMCTRL_SmartEEPROM_IsActiveSectorFull(void)
{
    return ((NVMCTRL_REGS->NVMCTRL_INTFLAG & NVMCTRL_INTFLAG_SEESFULL_Msk) != 0U);
}

void NVMCTRL_SmartEEPROMSectorReallocate(void)
{
    NVMCTRL_REGS->NVMCTRL_CTRLB = NVMCTRL_CTRLB_CMD_SEERALOC | NVMCTRL_CTRLB_CMDEX_KEY;
}

void NVMCTRL_SmartEEPROMFlushPageBuffer(void)
{
    /* Clear global error flag */
    nvm_error = 0U;
    NVMCTRL_REGS->NVMCTRL_CTRLB = NVMCTRL_CTRLB_CMD_SEEFLUSH | NVMCTRL_CTRLB_CMDEX_KEY;
}