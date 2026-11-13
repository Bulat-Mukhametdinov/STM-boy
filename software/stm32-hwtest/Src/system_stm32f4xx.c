/*
 * CMSIS system file for stm32-hwtest. Trimmed from ST's
 * system_stm32f4xx.c (Copyright (c) 2017 STMicroelectronics, provided AS-IS,
 * see CMSIS/Device license in HALDIR).
 *
 * Differences: SystemInit() always sets SCB->VTOR = FLASH_BASE (0x08000000),
 * because after the ST bootloader exits into the application VTOR may still be
 * 0 / point at system memory. No external memory support.
 */

#include "stm32f4xx.h"

#if !defined(HSE_VALUE)
#define HSE_VALUE 8000000U
#endif
#if !defined(HSI_VALUE)
#define HSI_VALUE 16000000U
#endif

uint32_t SystemCoreClock = 16000000U;
const uint8_t AHBPrescTable[16] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 2, 3, 4, 6, 7, 8, 9};
const uint8_t APBPrescTable[8] = {0, 0, 0, 0, 1, 2, 3, 4};

void SystemInit(void)
{
#if (__FPU_PRESENT == 1) && (__FPU_USED == 1)
    SCB->CPACR |= ((3UL << 10 * 2) | (3UL << 11 * 2)); /* CP10/CP11 full access */
#endif
    SCB->VTOR = FLASH_BASE; /* explicit: never inherit the bootloader's table */
    __DSB();
    __ISB();
}

void SystemCoreClockUpdate(void)
{
    uint32_t tmp, pllvco, pllp, pllsource, pllm;

    tmp = RCC->CFGR & RCC_CFGR_SWS;
    switch (tmp) {
    case 0x00: /* HSI */
        SystemCoreClock = HSI_VALUE;
        break;
    case 0x04: /* HSE */
        SystemCoreClock = HSE_VALUE;
        break;
    case 0x08: /* PLL P */
        pllsource = (RCC->PLLCFGR & RCC_PLLCFGR_PLLSRC) >> 22;
        pllm = RCC->PLLCFGR & RCC_PLLCFGR_PLLM;
        if (pllsource != 0) {
            pllvco = (HSE_VALUE / pllm) * ((RCC->PLLCFGR & RCC_PLLCFGR_PLLN) >> 6);
        } else {
            pllvco = (HSI_VALUE / pllm) * ((RCC->PLLCFGR & RCC_PLLCFGR_PLLN) >> 6);
        }
        pllp = (((RCC->PLLCFGR & RCC_PLLCFGR_PLLP) >> 16) + 1) * 2;
        SystemCoreClock = pllvco / pllp;
        break;
    default:
        SystemCoreClock = HSI_VALUE;
        break;
    }
    tmp = AHBPrescTable[((RCC->CFGR & RCC_CFGR_HPRE) >> 4)];
    SystemCoreClock >>= tmp;
}
