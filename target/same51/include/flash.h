/* Copyright (c) 2025 Martin R. Raumann */
/* SPDX-License-Identifier: BSD-3-Clause */
/*******************************************************************************
  Flash Header - Consolidated replacement for plib_nvmctrl.h
  
  Provides NVMCTRL functions for flash and SmartEEPROM operations.
  - Flash read/write/erase operations
  - SmartEEPROM configuration and status
  - User row operations for fuse settings
*******************************************************************************/

#ifndef FLASH_H
#define FLASH_H

#include "same51_minimal.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* Flash page size */
#define NVMCTRL_FLASH_PAGESIZE    (512U)
#define NVMCTRL_USERROW_SIZE      (512U)
#define NVMCTRL_USERROW_START_ADDRESS (0x00804000UL)

/* Write modes */
typedef enum
{
    NVMCTRL_WMODE_MAN = NVMCTRL_CTRLA_WMODE_MAN_Val,
    NVMCTRL_WMODE_ADW = NVMCTRL_CTRLA_WMODE_ADW_Val,
    NVMCTRL_WMODE_AQW = NVMCTRL_CTRLA_WMODE_AQW_Val,
    NVMCTRL_WMODE_AP = NVMCTRL_CTRLA_WMODE_AP_Val,
} NVMCTRL_WRITEMODE;

/* Function declarations */
void NVMCTRL_Initialize(void);
bool NVMCTRL_Read(uint32_t *data, uint32_t length, const uint32_t address);
void NVMCTRL_SetWriteMode(NVMCTRL_WRITEMODE mode);
bool NVMCTRL_QuadWordWrite(const uint32_t *data, const uint32_t address);
bool NVMCTRL_PageWrite(const uint32_t *data, const uint32_t address);
bool NVMCTRL_BlockErase(uint32_t address);
bool NVMCTRL_IsBusy(void);
uint16_t NVMCTRL_ErrorGet(void);
uint16_t NVMCTRL_StatusGet(void);

/* SmartEEPROM functions */
uint32_t NVMCTRL_SmartEEPROMStatusGet(void);
bool NVMCTRL_SmartEEPROM_IsBusy(void);
bool NVMCTRL_SmartEEPROM_IsActiveSectorFull(void);
void NVMCTRL_SmartEEPROMSectorReallocate(void);
void NVMCTRL_SmartEEPROMFlushPageBuffer(void);

/* User row functions */
bool NVMCTRL_USER_ROW_PageWrite(uint32_t *data, const uint32_t address);
bool NVMCTRL_USER_ROW_RowErase(uint32_t address);

#endif /* FLASH_H */