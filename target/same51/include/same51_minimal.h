/*
 * Minimal device header for ATSAME51J20A — ZVibe project
 *
 * Copyright (c) 2025 Martin R. Raumann
 * Copyright (c) 2025 Microchip Technology Inc. and its subsidiaries
 * SPDX-License-Identifier: Apache-2.0
 *
 * Derived from Microchip's ATSAME51J20A DFP (Apache-2.0, ATDF 2025-07-02).
 * Reduced to the peripheral components used by ZVibe (~71% smaller than
 * the full device header).
 */

#ifndef _SAME51_MINIMAL_H_
#define _SAME51_MINIMAL_H_

/* Header version uses Semantic Versioning 2.0.0 (https://semver.org/) */
#define HEADER_FORMAT_VERSION "2.1.1"

#ifdef __cplusplus
 extern "C" {
#endif

#if !(defined(__ASSEMBLER__) || defined(__IAR_SYSTEMS_ASM__))
#  include <stdint.h>
#endif

#if !defined(SKIP_INTEGER_LITERALS)
#  if defined(_UINT8_) || defined(_UINT16_) || defined(_UINT32_)
#    error "Integer constant value macros already defined elsewhere"
#  endif

#if !(defined(__ASSEMBLER__) || defined(__IAR_SYSTEMS_ASM__))
#  define _UINT8_(x)   ((uint8_t)(x))
#  define _UINT16_(x)  ((uint16_t)(x))
#  define _UINT32_(x)  ((uint32_t)(x))
#else /* Assembler */
#  define _UINT8_(x) x
#  define _UINT16_(x) x
#  define _UINT32_(x) x
#endif
#endif /* SKIP_INTEGER_LITERALS */

/* CMSIS DEFINITIONS FOR SAME51J20A */
#if !(defined(__ASSEMBLER__) || defined(__IAR_SYSTEMS_ASM__))

/* Interrupt Number Definition - must be defined before core_cm4.h */
typedef enum IRQn
{
/******  CORTEX-M4 Processor Exceptions Numbers ******************************/
  Reset_IRQn                = -15,
  NonMaskableInt_IRQn       = -14,
  HardFault_IRQn            = -13,
  MemoryManagement_IRQn     = -12,
  BusFault_IRQn             = -11,
  UsageFault_IRQn           = -10,
  SVCall_IRQn               =  -5,
  DebugMonitor_IRQn         =  -4,
  PendSV_IRQn               =  -2,
  SysTick_IRQn              =  -1,

/******  SAME51J20A specific - only used peripherals *************************/
  MCLK_IRQn                 =   1,
  OSCCTRL_XOSC0_IRQn        =   2,
  OSCCTRL_DFLL_IRQn         =   4,
  OSC32KCTRL_IRQn           =   7,
  RTC_IRQn                  =  11,
  EIC_EXTINT_0_IRQn         =  12,
  EIC_EXTINT_1_IRQn         =  13,
  EIC_EXTINT_15_IRQn        =  27,
  DMAC_0_IRQn               =  31,
  DMAC_1_IRQn               =  32,
  DMAC_2_IRQn               =  33,
  DMAC_3_IRQn               =  34,
  DMAC_4_IRQn               =  35,
  NVMCTRL_0_IRQn            =  37,
  NVMCTRL_1_IRQn            =  38,
  SERCOM5_0_IRQn            =  80,
  SERCOM5_1_IRQn            =  81,
  SERCOM5_2_IRQn            =  82,
  SERCOM5_3_IRQn            =  83,
  
  PERIPH_MAX_IRQn           =  136  /* Maximum peripheral ID */
} IRQn_Type;

/* CMSIS Core definitions */
#define __CM4_REV                 0x0001
#define __DSP_PRESENT                  1
#define __FPU_PRESENT                  1
#define __MPU_PRESENT                  1
#define __NVIC_PRIO_BITS               3
#define __Vendor_SysTickConfig         0

/* Include CMSIS core definitions after IRQn_Type and device macros */
#include "core_cm4.h"

/* Essential device-specific peripheral instance IDs */
#define ID_MCLK          (  2)
#define ID_OSCCTRL       (  4)
#define ID_OSC32KCTRL    (  5)
#define ID_GCLK          (  7)
#define ID_RTC           (  9)
#define ID_EIC           ( 10)
#define ID_NVMCTRL       ( 34)
#define ID_CMCC          ( 35)
#define ID_PORT          ( 36)
#define ID_DMAC          ( 37)
#define ID_SERCOM5       ( 97)

/* Essential base addresses for used peripherals */
#define CMCC_BASE_ADDRESS                _UINT32_(0x41006000)
#define DMAC_BASE_ADDRESS                _UINT32_(0x4100a000)
#define EIC_BASE_ADDRESS                 _UINT32_(0x40002800)
#define GCLK_BASE_ADDRESS                _UINT32_(0x40001c00)
#define MCLK_BASE_ADDRESS                _UINT32_(0x40000800)
#define NVMCTRL_BASE_ADDRESS             _UINT32_(0x41004000)
#define OSC32KCTRL_BASE_ADDRESS          _UINT32_(0x40001400)
#define OSCCTRL_BASE_ADDRESS             _UINT32_(0x40001000)
#define PORT_BASE_ADDRESS                _UINT32_(0x41008000)
#define RTC_BASE_ADDRESS                 _UINT32_(0x40002400)
#define SERCOM5_BASE_ADDRESS             _UINT32_(0x43000400)

/* Memory layout definitions */
#define FLASH_SIZE                     _UINT32_(0x00100000)
#define FLASH_PAGE_SIZE                _UINT32_(       512)
#define HSRAM_SIZE                     _UINT32_(0x00040000)
#define SEEPROM_SIZE                   _UINT32_(0x00020000)

#endif /* !(defined(__ASSEMBLER__) || defined(__IAR_SYSTEMS_ASM__)) */


/* ONLY INCLUDE USED PERIPHERAL COMPONENTS */
#include "component/cmcc.h"     /* Cache controller */
#include "component/dmac.h"     /* DMA controller */
#include "component/eic.h"      /* External interrupts */
#include "component/gclk.h"     /* Generic clocks */
#include "component/mclk.h"     /* Main clock */
#include "component/nvmctrl.h"  /* Flash/EEPROM */
#include "component/osc32kctrl.h" /* 32kHz oscillator */
#include "component/oscctrl.h"  /* Main oscillator */
#include "component/port.h"     /* GPIO ports */
#include "component/rtc.h"      /* Real-time counter */
#include "component/sercom.h"   /* Serial communication */

/* ONLY INCLUDE USED PERIPHERAL INSTANCES */
#include "instance/cmcc.h"
#include "instance/dmac.h"
#include "instance/eic.h"
#include "instance/gclk.h"
#include "instance/mclk.h"
#include "instance/nvmctrl.h"
#include "instance/osc32kctrl.h"
#include "instance/oscctrl.h"
#include "instance/port.h"
#include "instance/rtc.h"
#include "instance/sercom5.h"   /* Only SERCOM5 used for UART */

/* PIO definitions for SAME51J20A */
#include "pio/same51j20a.h"

/* Register pointer definitions for used peripherals */
#if !(defined(__ASSEMBLER__) || defined(__IAR_SYSTEMS_ASM__))
#define CMCC_REGS                        ((cmcc_registers_t*)0x41006000)
#define DMAC_REGS                        ((dmac_registers_t*)0x4100a000) 
#define EIC_REGS                         ((eic_registers_t*)0x40002800)
#define GCLK_REGS                        ((gclk_registers_t*)0x40001c00)
#define MCLK_REGS                        ((mclk_registers_t*)0x40000800)
#define NVMCTRL_REGS                     ((nvmctrl_registers_t*)0x41004000)
#define OSC32KCTRL_REGS                  ((osc32kctrl_registers_t*)0x40001400)
#define OSCCTRL_REGS                     ((oscctrl_registers_t*)0x40001000)
#define PORT_REGS                        ((port_registers_t*)0x41008000)
#define RTC_REGS                         ((rtc_registers_t*)0x40002400)
#define SERCOM5_REGS                     ((sercom_registers_t*)0x43000400)
#endif

#ifdef __cplusplus
}
#endif

#endif /* _SAME51_MINIMAL_H_ */