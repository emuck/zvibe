/*
 * board_support.h — SAM E51 board support for ZVibe
 *
 * Copyright (c) 2025 Martin R. Raumann
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Consolidated replacement for the Harmony3-generated glue headers:
 *   definitions.h, device.h, device_cache.h, device_vectors.h,
 *   interrupts.h, toolchain_specifics.h
 *
 * All content is either project-specific (prototypes, device constants)
 * or standard compiler/architecture boilerplate.
 */

#ifndef BOARD_SUPPORT_H
#define BOARD_SUPPORT_H

/* -------------------------------------------------------------------------
 * Standard library headers
 * ------------------------------------------------------------------------- */
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <sys/types.h>

/* -------------------------------------------------------------------------
 * Device register definitions
 * Pull in same51_minimal.h (project-owned subset of Microchip Apache-2.0 DFP)
 * with GCC diagnostic suppression for the CMSIS headers it includes.
 * ------------------------------------------------------------------------- */
#pragma GCC diagnostic push
#ifndef __cplusplus
#pragma GCC diagnostic ignored "-Wnested-externs"
#endif
#pragma GCC diagnostic ignored "-Wsign-conversion"
#pragma GCC diagnostic ignored "-Wattributes"
#pragma GCC diagnostic ignored "-Wundef"
#ifndef DONT_USE_PREDEFINED_CORE_HANDLERS
#define DONT_USE_PREDEFINED_CORE_HANDLERS
#endif
#ifndef DONT_USE_PREDEFINED_PERIPHERALS_HANDLERS
#define DONT_USE_PREDEFINED_PERIPHERALS_HANDLERS
#endif
#include "same51_minimal.h"
#include "cmsis_compiler.h"
#pragma GCC diagnostic pop

/* -------------------------------------------------------------------------
 * Project peripheral driver headers
 * ------------------------------------------------------------------------- */
#include "clock.h"
#include "dma.h"
#include "flash.h"
#include "gpio.h"
#include "system.h"
#include "timer.h"
#include "uart.h"

/* -------------------------------------------------------------------------
 * Device constants
 * ------------------------------------------------------------------------- */
#define DEVICE_NAME             "ATSAME51J20A"
#define DEVICE_ARCH             "CORTEX-M4"
#define DEVICE_FAMILY           "SAME"
#define DEVICE_SERIES           "SAME51"
#define CPU_CLOCK_FREQUENCY     120000000U

/* -------------------------------------------------------------------------
 * Cache macros — SAME51 uses CMCC (controlled in startup), not a standard
 * coherent L1 cache. These are no-ops; provided for source compatibility.
 * ------------------------------------------------------------------------- */
#define ICACHE_ENABLE()
#define ICACHE_DISABLE()
#define ICACHE_INVALIDATE()

#define DCACHE_ENABLE()
#define DCACHE_DISABLE()
#define DCACHE_INVALIDATE()
#define DCACHE_CLEAN()
#define DCACHE_CLEAN_INVALIDATE()
#define DCACHE_CLEAN_BY_ADDR(addr, sz)
#define DCACHE_INVALIDATE_BY_ADDR(addr, sz)
#define DCACHE_CLEAN_INVALIDATE_BY_ADDR(addr, sz)

/* -------------------------------------------------------------------------
 * Toolchain / linker section macros
 * ------------------------------------------------------------------------- */
#define NO_INIT                 __attribute__((section(".no_init")))
#define SECTION(a)              __attribute__((__section__(a)))

#define CACHE_LINE_SIZE         (16u)
#define CACHE_ALIGN             __ALIGNED(CACHE_LINE_SIZE)
#define CACHE_ALIGNED_SIZE_GET(size) \
    ((size) + ((((size) % (CACHE_LINE_SIZE)) != 0U) ? \
               ((CACHE_LINE_SIZE) - ((size) % (CACHE_LINE_SIZE))) : 0U))

#ifndef FORMAT_ATTRIBUTE
#define FORMAT_ATTRIBUTE(archetype, string_index, first_to_check) \
    __attribute__((format(archetype, string_index, first_to_check)))
#endif

/* -------------------------------------------------------------------------
 * System initialisation
 * ------------------------------------------------------------------------- */
#ifdef __cplusplus
extern "C" {
#endif

void SYS_Initialize(void *data);

/* No RTOS task pump needed (PLIB-only build) */
#define SYS_Tasks()

/* -------------------------------------------------------------------------
 * Interrupt handler prototypes (active handlers for this project)
 * ------------------------------------------------------------------------- */
void Reset_Handler(void);
void NMI_Handler(void);
void HardFault_Handler(void);
void MemManage_Handler(void);
void BusFault_Handler(void);
void UsageFault_Handler(void);
void DebugMon_Handler(void);
void RTC_InterruptHandler(void);
void EIC_EXTINT_15_InterruptHandler(void);
void DMAC_0_InterruptHandler(void);

#ifdef __cplusplus
}
#endif

#endif /* BOARD_SUPPORT_H */
