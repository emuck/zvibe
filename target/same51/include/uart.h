/* Copyright (c) 2025 Martin R. Raumann */
/* SPDX-License-Identifier: BSD-3-Clause */
/*******************************************************************************
  UART Header - Consolidated replacement for plib_sercom5_usart.h
  
  Provides SERCOM5 UART functions for serial communication.
  - 115200 baud, 8N1 configuration
  - PB16 (TX), PB17 (RX) pins
  - Interrupt-driven receive with callback support
*******************************************************************************/

#ifndef UART_H
#define UART_H

#include "same51_minimal.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* USART Error convenience macros */
#define USART_ERROR_NONE 0U
#define USART_ERROR_PARITY SERCOM_USART_INT_STATUS_PERR_Msk
#define USART_ERROR_FRAMING SERCOM_USART_INT_STATUS_FERR_Msk
#define USART_ERROR_OVERRUN SERCOM_USART_INT_STATUS_BUFOVF_Msk

/* Type definitions */
typedef uint16_t USART_ERROR;

typedef enum
{
    USART_DATA_5_BIT = SERCOM_USART_INT_CTRLB_CHSIZE_5_BIT,
    USART_DATA_6_BIT = SERCOM_USART_INT_CTRLB_CHSIZE_6_BIT,
    USART_DATA_7_BIT = SERCOM_USART_INT_CTRLB_CHSIZE_7_BIT,
    USART_DATA_8_BIT = SERCOM_USART_INT_CTRLB_CHSIZE_8_BIT,
    USART_DATA_9_BIT = SERCOM_USART_INT_CTRLB_CHSIZE_9_BIT,
    USART_DATA_INVALID = 0xFFFFFFFFU
} USART_DATA;

typedef enum
{
    USART_PARITY_EVEN = SERCOM_USART_INT_CTRLB_PMODE_EVEN,
    USART_PARITY_ODD = SERCOM_USART_INT_CTRLB_PMODE_ODD,
    USART_PARITY_NONE = 0x2,
    USART_PARITY_INVALID = 0xFFFFFFFFU
} USART_PARITY;

typedef enum
{
    USART_STOP_0_BIT = SERCOM_USART_INT_CTRLB_SBMODE_1_BIT,
    USART_STOP_1_BIT = SERCOM_USART_INT_CTRLB_SBMODE_2_BIT,
    USART_STOP_INVALID = 0xFFFFFFFFU
} USART_STOP;

typedef struct
{
    uint32_t baudRate;
    USART_PARITY parity;
    USART_DATA dataWidth;
    USART_STOP stopBits;
} USART_SERIAL_SETUP;

/* SERCOM5 UART callback function type */
typedef void (*SERCOM5_USART_RING_BUFFER_CALLBACK)(uintptr_t context);

/* Function declarations */
void SERCOM5_USART_Initialize(void);
void SERCOM5_USART_WriteByte(int data);
bool SERCOM5_USART_ReceiverIsReady(void);
int SERCOM5_USART_ReadByte(void);
void SERCOM5_USART_ReadCallbackRegister(SERCOM5_USART_RING_BUFFER_CALLBACK callback, uintptr_t context);

#endif /* UART_H */