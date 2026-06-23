/*
 * SAM E51 startup for ZVibe
 *
 * Copyright (c) 2025 Martin R. Raumann
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Clean-room XC32-compatible startup. Replaces the MCC Harmony-generated
 * startup_xc32.c with a minimal, project-specific implementation.
 *
 * Responsibilities:
 *   - SAME51J20A fuse bit configuration
 *   - FPU enable (Cortex-M4F)
 *   - Cache (CMCC) enable
 *   - C runtime init (delegated to XC32 __pic32c_data_initialization)
 *   - ARM Cortex-M4 vector table
 *   - Minimal fault and dummy IRQ handlers
 *   - Syscall stubs (_exit, read, write)
 */

#include <stdint.h>
#include "same51j20a.h"

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------------
 * Fuse bits — project-specific configuration for ZVibe on SAM E51 Curiosity Nano
 * ------------------------------------------------------------------------- */
#pragma config BOD33_DIS        = SET
#pragma config BOD33USERLEVEL   = 0x1cU
#pragma config BOD33_ACTION     = RESET
#pragma config BOD33_HYST       = 0x2U
#pragma config NVMCTRL_BOOTPROT = 0
#pragma config NVMCTRL_SEESBLK  = 0x3U   /* SBLK=3: 16KB SmartEEPROM virtual size */
#pragma config NVMCTRL_SEEPSZ   = 0x7U   /* PSZ=7: 512-byte pages (fastest writes) */
#pragma config RAMECC_ECCDIS    = SET
#pragma config WDT_ENABLE       = CLEAR
#pragma config WDT_ALWAYSON     = CLEAR
#pragma config WDT_PER          = CYC8192
#pragma config WDT_WINDOW       = CYC8192
#pragma config WDT_EWOFFSET     = CYC8192
#pragma config WDT_WEN          = CLEAR
#pragma config NVMCTRL_REGION_LOCKS = 0xffffffffU

/* -------------------------------------------------------------------------
 * External symbols
 * ------------------------------------------------------------------------- */

/* XC32 runtime: copies .data and zeros .bss using the .dinit table */
extern void __pic32c_data_initialization(void);

/* C++ / constructor support */
extern void __libc_init_array(void);

/* Stack top — provided by linker script */
extern uint32_t _stack;

/* Application entry point */
extern int main(void);

/* Interrupt handlers defined in peripheral drivers */
extern void SERCOM5_USART_InterruptHandler(void);
extern void RTC_InterruptHandler(void);
extern void EIC_EXTINT_15_InterruptHandler(void);
extern void DMAC_0_InterruptHandler(void);

/* -------------------------------------------------------------------------
 * Cortex-M4F helpers (ARM architecture, not vendor expression)
 * ------------------------------------------------------------------------- */

static inline void fpu_enable(void)
{
#if defined(__ARM_FP) && (__ARM_FP != 0)
    /* Enable CP10 and CP11 full access — ARM Architecture Reference Manual */
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    SCB->CPACR |= (0xFU << 20);
    __DSB();
    __ISB();
    if (primask == 0U) {
        __enable_irq();
    }
#endif
}

static inline void cmcc_enable(void)
{
    /* Disable cache, configure 4KB data + instruction, then re-enable */
    CMCC_REGS->CMCC_CTRL &= ~CMCC_CTRL_CEN_Msk;
    while (CMCC_REGS->CMCC_SR & CMCC_SR_CSTS_Msk) {}
    CMCC_REGS->CMCC_CFG = CMCC_CFG_CSIZESW(2U) | CMCC_CFG_DCDIS_Msk;
    CMCC_REGS->CMCC_CTRL = CMCC_CTRL_CEN_Msk;
}

/* -------------------------------------------------------------------------
 * Reset handler
 * ------------------------------------------------------------------------- */

void __attribute__((section(".text.Reset_Handler"), noreturn)) Reset_Handler(void)
{
    fpu_enable();
    cmcc_enable();

    /* Set vector table base — SCB_VTOR defined in CMSIS core_cm4.h */
    extern uint32_t _sfixed;
    SCB->VTOR = (uint32_t)&_sfixed & SCB_VTOR_TBLOFF_Msk;

    /* C runtime: .data copy and .bss zero via XC32 .dinit table */
    __pic32c_data_initialization();

    /* C++ global constructors */
    __libc_init_array();

    (void)main();

    while (1) {}
}

/* -------------------------------------------------------------------------
 * Default and fault handlers
 * ------------------------------------------------------------------------- */

void __attribute__((noreturn, weak)) Dummy_Handler(void)
{
    while (1) {}
}

void __attribute__((noreturn, weak)) HardFault_Handler(void)    { while (1) {} }
void __attribute__((noreturn, weak)) NMI_Handler(void)          { while (1) {} }
void __attribute__((noreturn, weak)) MemManage_Handler(void)    { while (1) {} }
void __attribute__((noreturn, weak)) BusFault_Handler(void)     { while (1) {} }
void __attribute__((noreturn, weak)) UsageFault_Handler(void)   { while (1) {} }
void __attribute__((noreturn, weak)) DebugMon_Handler(void)     { while (1) {} }

/* -------------------------------------------------------------------------
 * SERCOM5 IRQ demux (routes to USART handler)
 * ------------------------------------------------------------------------- */
void SERCOM5_0_Handler(void) { SERCOM5_USART_InterruptHandler(); }

/* -------------------------------------------------------------------------
 * Vector table — layout from ARM Cortex-M4 Architecture Reference Manual
 * (ARM DDI 0403). Each slot is a function pointer; slot 0 is initial SP.
 * ------------------------------------------------------------------------- */

typedef void (*isr_fn_t)(void);

#define WEAK_DUMMY __attribute__((noreturn, weak, alias("Dummy_Handler")))

/* Weak aliases for all peripheral vectors not routed in this project */
extern void SVCall_Handler(void)              WEAK_DUMMY;
extern void PendSV_Handler(void)              WEAK_DUMMY;
extern void SysTick_Handler(void)             WEAK_DUMMY;
extern void PM_Handler(void)                  WEAK_DUMMY;
extern void MCLK_Handler(void)                WEAK_DUMMY;
extern void OSCCTRL_XOSC0_Handler(void)       WEAK_DUMMY;
extern void OSCCTRL_XOSC1_Handler(void)       WEAK_DUMMY;
extern void OSCCTRL_DFLL_Handler(void)        WEAK_DUMMY;
extern void OSCCTRL_DPLL0_Handler(void)       WEAK_DUMMY;
extern void OSCCTRL_DPLL1_Handler(void)       WEAK_DUMMY;
extern void OSC32KCTRL_Handler(void)          WEAK_DUMMY;
extern void SUPC_OTHER_Handler(void)          WEAK_DUMMY;
extern void SUPC_BODDET_Handler(void)         WEAK_DUMMY;
extern void WDT_Handler(void)                 WEAK_DUMMY;
extern void EIC_EXTINT_0_Handler(void)        WEAK_DUMMY;
extern void EIC_EXTINT_1_Handler(void)        WEAK_DUMMY;
extern void EIC_EXTINT_2_Handler(void)        WEAK_DUMMY;
extern void EIC_EXTINT_3_Handler(void)        WEAK_DUMMY;
extern void EIC_EXTINT_4_Handler(void)        WEAK_DUMMY;
extern void EIC_EXTINT_5_Handler(void)        WEAK_DUMMY;
extern void EIC_EXTINT_6_Handler(void)        WEAK_DUMMY;
extern void EIC_EXTINT_7_Handler(void)        WEAK_DUMMY;
extern void EIC_EXTINT_8_Handler(void)        WEAK_DUMMY;
extern void EIC_EXTINT_9_Handler(void)        WEAK_DUMMY;
extern void EIC_EXTINT_10_Handler(void)       WEAK_DUMMY;
extern void EIC_EXTINT_11_Handler(void)       WEAK_DUMMY;
extern void EIC_EXTINT_12_Handler(void)       WEAK_DUMMY;
extern void EIC_EXTINT_13_Handler(void)       WEAK_DUMMY;
extern void EIC_EXTINT_14_Handler(void)       WEAK_DUMMY;
extern void FREQM_Handler(void)               WEAK_DUMMY;
extern void NVMCTRL_0_Handler(void)           WEAK_DUMMY;
extern void NVMCTRL_1_Handler(void)           WEAK_DUMMY;
extern void DMAC_1_Handler(void)              WEAK_DUMMY;
extern void DMAC_2_Handler(void)              WEAK_DUMMY;
extern void DMAC_3_Handler(void)              WEAK_DUMMY;
extern void DMAC_OTHER_Handler(void)          WEAK_DUMMY;
extern void EVSYS_0_Handler(void)             WEAK_DUMMY;
extern void EVSYS_1_Handler(void)             WEAK_DUMMY;
extern void EVSYS_2_Handler(void)             WEAK_DUMMY;
extern void EVSYS_3_Handler(void)             WEAK_DUMMY;
extern void EVSYS_OTHER_Handler(void)         WEAK_DUMMY;
extern void PAC_Handler(void)                 WEAK_DUMMY;
extern void RAMECC_Handler(void)              WEAK_DUMMY;
extern void SERCOM0_0_Handler(void)           WEAK_DUMMY;
extern void SERCOM0_1_Handler(void)           WEAK_DUMMY;
extern void SERCOM0_2_Handler(void)           WEAK_DUMMY;
extern void SERCOM0_OTHER_Handler(void)       WEAK_DUMMY;
extern void SERCOM1_0_Handler(void)           WEAK_DUMMY;
extern void SERCOM1_1_Handler(void)           WEAK_DUMMY;
extern void SERCOM1_2_Handler(void)           WEAK_DUMMY;
extern void SERCOM1_OTHER_Handler(void)       WEAK_DUMMY;
extern void SERCOM2_0_Handler(void)           WEAK_DUMMY;
extern void SERCOM2_1_Handler(void)           WEAK_DUMMY;
extern void SERCOM2_2_Handler(void)           WEAK_DUMMY;
extern void SERCOM2_OTHER_Handler(void)       WEAK_DUMMY;
extern void SERCOM3_0_Handler(void)           WEAK_DUMMY;
extern void SERCOM3_1_Handler(void)           WEAK_DUMMY;
extern void SERCOM3_2_Handler(void)           WEAK_DUMMY;
extern void SERCOM3_OTHER_Handler(void)       WEAK_DUMMY;
extern void SERCOM4_0_Handler(void)           WEAK_DUMMY;
extern void SERCOM4_1_Handler(void)           WEAK_DUMMY;
extern void SERCOM4_2_Handler(void)           WEAK_DUMMY;
extern void SERCOM4_OTHER_Handler(void)       WEAK_DUMMY;
extern void SERCOM5_1_Handler(void)           WEAK_DUMMY;
extern void SERCOM5_2_Handler(void)           WEAK_DUMMY;
extern void SERCOM5_OTHER_Handler(void)       WEAK_DUMMY;
extern void CAN0_Handler(void)                WEAK_DUMMY;
extern void CAN1_Handler(void)                WEAK_DUMMY;
extern void USB_OTHER_Handler(void)           WEAK_DUMMY;
extern void USB_SOF_HSOF_Handler(void)        WEAK_DUMMY;
extern void USB_TRCPT0_Handler(void)          WEAK_DUMMY;
extern void USB_TRCPT1_Handler(void)          WEAK_DUMMY;
extern void TCC0_OTHER_Handler(void)          WEAK_DUMMY;
extern void TCC0_MC0_Handler(void)            WEAK_DUMMY;
extern void TCC0_MC1_Handler(void)            WEAK_DUMMY;
extern void TCC0_MC2_Handler(void)            WEAK_DUMMY;
extern void TCC0_MC3_Handler(void)            WEAK_DUMMY;
extern void TCC0_MC4_Handler(void)            WEAK_DUMMY;
extern void TCC0_MC5_Handler(void)            WEAK_DUMMY;
extern void TCC1_OTHER_Handler(void)          WEAK_DUMMY;
extern void TCC1_MC0_Handler(void)            WEAK_DUMMY;
extern void TCC1_MC1_Handler(void)            WEAK_DUMMY;
extern void TCC1_MC2_Handler(void)            WEAK_DUMMY;
extern void TCC1_MC3_Handler(void)            WEAK_DUMMY;
extern void TCC2_OTHER_Handler(void)          WEAK_DUMMY;
extern void TCC2_MC0_Handler(void)            WEAK_DUMMY;
extern void TCC2_MC1_Handler(void)            WEAK_DUMMY;
extern void TCC2_MC2_Handler(void)            WEAK_DUMMY;
extern void TCC3_OTHER_Handler(void)          WEAK_DUMMY;
extern void TCC3_MC0_Handler(void)            WEAK_DUMMY;
extern void TCC3_MC1_Handler(void)            WEAK_DUMMY;
extern void TCC4_OTHER_Handler(void)          WEAK_DUMMY;
extern void TCC4_MC0_Handler(void)            WEAK_DUMMY;
extern void TCC4_MC1_Handler(void)            WEAK_DUMMY;
extern void TC0_Handler(void)                 WEAK_DUMMY;
extern void TC1_Handler(void)                 WEAK_DUMMY;
extern void TC2_Handler(void)                 WEAK_DUMMY;
extern void TC3_Handler(void)                 WEAK_DUMMY;
extern void TC4_Handler(void)                 WEAK_DUMMY;
extern void TC5_Handler(void)                 WEAK_DUMMY;
extern void PDEC_OTHER_Handler(void)          WEAK_DUMMY;
extern void PDEC_MC0_Handler(void)            WEAK_DUMMY;
extern void PDEC_MC1_Handler(void)            WEAK_DUMMY;
extern void ADC0_OTHER_Handler(void)          WEAK_DUMMY;
extern void ADC0_RESRDY_Handler(void)         WEAK_DUMMY;
extern void ADC1_OTHER_Handler(void)          WEAK_DUMMY;
extern void ADC1_RESRDY_Handler(void)         WEAK_DUMMY;
extern void AC_Handler(void)                  WEAK_DUMMY;
extern void DAC_OTHER_Handler(void)           WEAK_DUMMY;
extern void DAC_EMPTY_0_Handler(void)         WEAK_DUMMY;
extern void DAC_EMPTY_1_Handler(void)         WEAK_DUMMY;
extern void DAC_RESRDY_0_Handler(void)        WEAK_DUMMY;
extern void DAC_RESRDY_1_Handler(void)        WEAK_DUMMY;
extern void I2S_Handler(void)                 WEAK_DUMMY;
extern void PCC_Handler(void)                 WEAK_DUMMY;
extern void AES_Handler(void)                 WEAK_DUMMY;
extern void TRNG_Handler(void)                WEAK_DUMMY;
extern void ICM_Handler(void)                 WEAK_DUMMY;
extern void PUKCC_Handler(void)               WEAK_DUMMY;
extern void QSPI_Handler(void)                WEAK_DUMMY;
extern void SDHC0_Handler(void)               WEAK_DUMMY;

__attribute__((section(".vectors"), used))
const isr_fn_t isr_vectors[] = {
    /* ARM Cortex-M4 core vectors (ARM DDI 0403 Table B1-4) */
    (isr_fn_t)&_stack,           /* Initial stack pointer */
    Reset_Handler,                /* Reset */
    NMI_Handler,                  /* NMI */
    HardFault_Handler,            /* Hard fault */
    MemManage_Handler,            /* Memory management fault */
    BusFault_Handler,             /* Bus fault */
    UsageFault_Handler,           /* Usage fault */
    Dummy_Handler,                /* Reserved */
    Dummy_Handler,                /* Reserved */
    Dummy_Handler,                /* Reserved */
    Dummy_Handler,                /* Reserved */
    SVCall_Handler,               /* SVCall */
    DebugMon_Handler,             /* Debug monitor */
    Dummy_Handler,                /* Reserved */
    PendSV_Handler,               /* PendSV */
    SysTick_Handler,              /* SysTick */

    /* SAME51 peripheral vectors (SAM E51 datasheet Table 11-2) */
    PM_Handler,                   /* 0  PM */
    MCLK_Handler,                 /* 1  MCLK */
    OSCCTRL_XOSC0_Handler,        /* 2  OSCCTRL XOSC0 */
    OSCCTRL_XOSC1_Handler,        /* 3  OSCCTRL XOSC1 */
    OSCCTRL_DFLL_Handler,         /* 4  OSCCTRL DFLL */
    OSCCTRL_DPLL0_Handler,        /* 5  OSCCTRL DPLL0 */
    OSCCTRL_DPLL1_Handler,        /* 6  OSCCTRL DPLL1 */
    OSC32KCTRL_Handler,           /* 7  OSC32KCTRL */
    SUPC_OTHER_Handler,           /* 8  SUPC */
    SUPC_BODDET_Handler,          /* 9  SUPC BODDET */
    WDT_Handler,                  /* 10 WDT */
    RTC_InterruptHandler,         /* 11 RTC */
    EIC_EXTINT_0_Handler,         /* 12 EIC 0 */
    EIC_EXTINT_1_Handler,         /* 13 EIC 1 */
    EIC_EXTINT_2_Handler,         /* 14 EIC 2 */
    EIC_EXTINT_3_Handler,         /* 15 EIC 3 */
    EIC_EXTINT_4_Handler,         /* 16 EIC 4 */
    EIC_EXTINT_5_Handler,         /* 17 EIC 5 */
    EIC_EXTINT_6_Handler,         /* 18 EIC 6 */
    EIC_EXTINT_7_Handler,         /* 19 EIC 7 */
    EIC_EXTINT_8_Handler,         /* 20 EIC 8 */
    EIC_EXTINT_9_Handler,         /* 21 EIC 9 */
    EIC_EXTINT_10_Handler,        /* 22 EIC 10 */
    EIC_EXTINT_11_Handler,        /* 23 EIC 11 */
    EIC_EXTINT_12_Handler,        /* 24 EIC 12 */
    EIC_EXTINT_13_Handler,        /* 25 EIC 13 */
    EIC_EXTINT_14_Handler,        /* 26 EIC 14 */
    EIC_EXTINT_15_InterruptHandler, /* 27 EIC 15 */
    FREQM_Handler,                /* 28 FREQM */
    NVMCTRL_0_Handler,            /* 29 NVMCTRL 0 */
    NVMCTRL_1_Handler,            /* 30 NVMCTRL 1 (SmartEEPROM) */
    DMAC_0_InterruptHandler,      /* 31 DMAC 0 */
    DMAC_1_Handler,               /* 32 DMAC 1 */
    DMAC_2_Handler,               /* 33 DMAC 2 */
    DMAC_3_Handler,               /* 34 DMAC 3 */
    DMAC_OTHER_Handler,           /* 35 DMAC other */
    EVSYS_0_Handler,              /* 36 EVSYS 0 */
    EVSYS_1_Handler,              /* 37 EVSYS 1 */
    EVSYS_2_Handler,              /* 38 EVSYS 2 */
    EVSYS_3_Handler,              /* 39 EVSYS 3 */
    EVSYS_OTHER_Handler,          /* 40 EVSYS other */
    PAC_Handler,                  /* 41 PAC */
    RAMECC_Handler,               /* 42 RAMECC (note: not in all variants) */
    SERCOM0_0_Handler,            /* 43 SERCOM0 0 */
    SERCOM0_1_Handler,            /* 44 SERCOM0 1 */
    SERCOM0_2_Handler,            /* 45 SERCOM0 2 */
    SERCOM0_OTHER_Handler,        /* 46 SERCOM0 other */
    SERCOM1_0_Handler,            /* 47 SERCOM1 0 */
    SERCOM1_1_Handler,            /* 48 SERCOM1 1 */
    SERCOM1_2_Handler,            /* 49 SERCOM1 2 */
    SERCOM1_OTHER_Handler,        /* 50 SERCOM1 other */
    SERCOM2_0_Handler,            /* 51 SERCOM2 0 */
    SERCOM2_1_Handler,            /* 52 SERCOM2 1 */
    SERCOM2_2_Handler,            /* 53 SERCOM2 2 */
    SERCOM2_OTHER_Handler,        /* 54 SERCOM2 other */
    SERCOM3_0_Handler,            /* 55 SERCOM3 0 */
    SERCOM3_1_Handler,            /* 56 SERCOM3 1 */
    SERCOM3_2_Handler,            /* 57 SERCOM3 2 */
    SERCOM3_OTHER_Handler,        /* 58 SERCOM3 other */
    SERCOM4_0_Handler,            /* 59 SERCOM4 0 */
    SERCOM4_1_Handler,            /* 60 SERCOM4 1 */
    SERCOM4_2_Handler,            /* 61 SERCOM4 2 */
    SERCOM4_OTHER_Handler,        /* 62 SERCOM4 other */
    SERCOM5_0_Handler,            /* 63 SERCOM5 0 (USART RXC) */
    SERCOM5_1_Handler,            /* 64 SERCOM5 1 */
    SERCOM5_2_Handler,            /* 65 SERCOM5 2 */
    SERCOM5_OTHER_Handler,        /* 66 SERCOM5 other */
    CAN0_Handler,                 /* 67 CAN0 */
    CAN1_Handler,                 /* 68 CAN1 */
    USB_OTHER_Handler,            /* 69 USB other */
    USB_SOF_HSOF_Handler,         /* 70 USB SOF/HSOF */
    USB_TRCPT0_Handler,           /* 71 USB TRCPT0 */
    USB_TRCPT1_Handler,           /* 72 USB TRCPT1 */
    TCC0_OTHER_Handler,           /* 73 TCC0 other */
    TCC0_MC0_Handler,             /* 74 TCC0 MC0 */
    TCC0_MC1_Handler,             /* 75 TCC0 MC1 */
    TCC0_MC2_Handler,             /* 76 TCC0 MC2 */
    TCC0_MC3_Handler,             /* 77 TCC0 MC3 */
    TCC0_MC4_Handler,             /* 78 TCC0 MC4 */
    TCC0_MC5_Handler,             /* 79 TCC0 MC5 */
    TCC1_OTHER_Handler,           /* 80 TCC1 other */
    TCC1_MC0_Handler,             /* 81 TCC1 MC0 */
    TCC1_MC1_Handler,             /* 82 TCC1 MC1 */
    TCC1_MC2_Handler,             /* 83 TCC1 MC2 */
    TCC1_MC3_Handler,             /* 84 TCC1 MC3 */
    TCC2_OTHER_Handler,           /* 85 TCC2 other */
    TCC2_MC0_Handler,             /* 86 TCC2 MC0 */
    TCC2_MC1_Handler,             /* 87 TCC2 MC1 */
    TCC2_MC2_Handler,             /* 88 TCC2 MC2 */
    TCC3_OTHER_Handler,           /* 89 TCC3 other */
    TCC3_MC0_Handler,             /* 90 TCC3 MC0 */
    TCC3_MC1_Handler,             /* 91 TCC3 MC1 */
    TCC4_OTHER_Handler,           /* 92 TCC4 other */
    TCC4_MC0_Handler,             /* 93 TCC4 MC0 */
    TCC4_MC1_Handler,             /* 94 TCC4 MC1 */
    TC0_Handler,                  /* 95 TC0 */
    TC1_Handler,                  /* 96 TC1 */
    TC2_Handler,                  /* 97 TC2 */
    TC3_Handler,                  /* 98 TC3 */
    TC4_Handler,                  /* 99 TC4 */
    TC5_Handler,                  /* 100 TC5 */
    PDEC_OTHER_Handler,           /* 101 PDEC other */
    PDEC_MC0_Handler,             /* 102 PDEC MC0 */
    PDEC_MC1_Handler,             /* 103 PDEC MC1 */
    ADC0_OTHER_Handler,           /* 104 ADC0 other */
    ADC0_RESRDY_Handler,          /* 105 ADC0 RESRDY */
    ADC1_OTHER_Handler,           /* 106 ADC1 other */
    ADC1_RESRDY_Handler,          /* 107 ADC1 RESRDY */
    AC_Handler,                   /* 108 AC */
    DAC_OTHER_Handler,            /* 109 DAC other */
    DAC_EMPTY_0_Handler,          /* 110 DAC EMPTY 0 */
    DAC_EMPTY_1_Handler,          /* 111 DAC EMPTY 1 */
    DAC_RESRDY_0_Handler,         /* 112 DAC RESRDY 0 */
    DAC_RESRDY_1_Handler,         /* 113 DAC RESRDY 1 */
    I2S_Handler,                  /* 114 I2S */
    PCC_Handler,                  /* 115 PCC */
    AES_Handler,                  /* 116 AES */
    TRNG_Handler,                 /* 117 TRNG */
    ICM_Handler,                  /* 118 ICM */
    PUKCC_Handler,                /* 119 PUKCC */
    QSPI_Handler,                 /* 120 QSPI */
    SDHC0_Handler,                /* 121 SDHC0 */
};

/* -------------------------------------------------------------------------
 * Syscall stubs — satisfy newlib without pulling in semihosting
 * ------------------------------------------------------------------------- */

void __attribute__((noreturn)) _exit(int status)
{
    (void)status;
    while (1) {}
}

int read(int handle, void *buffer, unsigned int len)
{
    (void)handle; (void)buffer; (void)len;
    return -1;
}

int write(int handle, void *buffer, unsigned int len)
{
    (void)handle; (void)buffer; (void)len;
    return -1;
}

#ifdef __cplusplus
}
#endif
