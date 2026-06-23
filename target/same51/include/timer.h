/* Copyright (c) 2025 Martin R. Raumann */
/* SPDX-License-Identifier: BSD-3-Clause */
/*******************************************************************************
  Timer Header - Consolidated replacement for plib_rtc.h
  
  Provides RTC timer functions for 32-bit periodic timer operation.
  - RTC MODE0: 32-bit timer with prescaler and compare interrupts
  - Timer callback registration for periodic events
*******************************************************************************/

#ifndef TIMER_H
#define TIMER_H

#include "same51_minimal.h"
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

/* Frequency of Counter Clock for RTC */
#define RTC_COUNTER_CLOCK_FREQUENCY        (1024U / (1UL << (0x1U - 1U)))

/* RTC Timer interrupt masks */
#define RTC_TIMER32_INT_MASK_PER0  RTC_MODE0_INTENSET_PER0_Msk
#define RTC_TIMER32_INT_MASK_PER1  RTC_MODE0_INTENSET_PER1_Msk
#define RTC_TIMER32_INT_MASK_PER2  RTC_MODE0_INTENSET_PER2_Msk
#define RTC_TIMER32_INT_MASK_PER3  RTC_MODE0_INTENSET_PER3_Msk
#define RTC_TIMER32_INT_MASK_PER4  RTC_MODE0_INTENSET_PER4_Msk
#define RTC_TIMER32_INT_MASK_PER5  RTC_MODE0_INTENSET_PER5_Msk
#define RTC_TIMER32_INT_MASK_PER6  RTC_MODE0_INTENSET_PER6_Msk
#define RTC_TIMER32_INT_MASK_PER7  RTC_MODE0_INTENSET_PER7_Msk
#define RTC_TIMER32_INT_MASK_CMP0  RTC_MODE0_INTENSET_CMP0_Msk
#define RTC_TIMER32_INT_MASK_CMP1  RTC_MODE0_INTENSET_CMP1_Msk
#define RTC_TIMER32_INT_MASK_TAMPER  RTC_MODE0_INTENSET_TAMPER_Msk
#define RTC_TIMER32_INT_MASK_OVF  RTC_MODE0_INTENSET_OVF_Msk
#define RTC_TIMER32_INT_MASK_INVALID 0xFFFFFFFFU

/* Type definitions */
typedef uint32_t RTC_TIMER32_INT_MASK;
typedef enum
{
    BACKUP_REGISTER_0 = 0U,
    BACKUP_REGISTER_1 = 1U,
    BACKUP_REGISTER_2 = 2U,
    BACKUP_REGISTER_3 = 3U,
    BACKUP_REGISTER_4 = 4U,
    BACKUP_REGISTER_5 = 5U,
    BACKUP_REGISTER_6 = 6U,
    BACKUP_REGISTER_7 = 7U
} BACKUP_REGISTER;

#define TAMPER_CHANNEL_0  (0U)
#define TAMPER_CHANNEL_1  (1U)
#define TAMPER_CHANNEL_2  (2U)
typedef uint32_t TAMPER_CHANNEL;

/* RTC callback function type */
typedef void (*RTC_TIMER32_CALLBACK)( RTC_TIMER32_INT_MASK intCause, uintptr_t context );

/* RTC object structure */
typedef struct
{
    RTC_TIMER32_CALLBACK timer32BitCallback;
    RTC_TIMER32_INT_MASK timer32intCause;
    uintptr_t context;
} RTC_OBJECT;

/* Function declarations */
void RTC_Initialize(void);
void RTC_Timer32Start(void);
void RTC_Timer32CounterSet(uint32_t count);
void RTC_Timer32Compare0Set(uint32_t compareValue);
void RTC_Timer32CallbackRegister(RTC_TIMER32_CALLBACK callback, uintptr_t context);

#endif /* TIMER_H */