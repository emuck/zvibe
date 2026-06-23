/* Copyright (c) 2025 Martin R. Raumann */
/* SPDX-License-Identifier: BSD-3-Clause */
/*******************************************************************************
  DMA Header - Consolidated replacement for plib_dmac.h
  
  Provides DMAC functions for DMA transfers.
  - Channel 0: SERCOM5 TX DMA support
  - Byte transfers with source increment
  - Interrupt-driven completion handling
*******************************************************************************/

#ifndef DMA_H
#define DMA_H

#include "same51_minimal.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* DMA Beat Size definitions */
#define DMAC_CRC_BEAT_SIZE_BYTE      (0x0U)
#define DMAC_CRC_BEAT_SIZE_HWORD     (0x1U)
#define DMAC_CRC_BEAT_SIZE_WORD      (0x2U)

typedef uint8_t DMAC_CRC_BEAT_SIZE;

/* DMA Channel enumeration */
typedef enum
{
    DMAC_CHANNEL_0 = 0,
    DMAC_CHANNEL_NONE = -1
} DMAC_CHANNEL;

/* DMA Transfer Event enumeration */
typedef enum
{
    DMAC_TRANSFER_EVENT_COMPLETE,
    DMAC_TRANSFER_EVENT_ERROR
} DMAC_TRANSFER_EVENT;

/* DMA Channel callback function type */
typedef void (*DMAC_CHANNEL_CALLBACK)(DMAC_TRANSFER_EVENT event, uintptr_t contextHandle);

/* Function declarations */
void DMAC_Initialize(void);
bool DMAC_ChannelTransfer(DMAC_CHANNEL channel, const void *srcAddr, const void *destAddr, size_t blockSize);
void DMAC_ChannelCallbackRegister(DMAC_CHANNEL channel, const DMAC_CHANNEL_CALLBACK callback, const uintptr_t context);

#endif /* DMA_H */