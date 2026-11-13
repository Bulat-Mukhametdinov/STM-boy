/*
 * Early boot check — called from Reset_Handler (Startup/startup_stm32f411xe.s)
 * with interrupts masked, SP = _estack and FPU access granted, but BEFORE
 * SystemInit() and BEFORE .data/.bss initialisation.
 *
 * RULES for this file: touch only core/RCC/SYSCFG registers and the .noinit
 * control block. No initialised globals, no .bss, no library calls, no HAL.
 *
 * (a) DFU request: noinit.valid == NOINIT_VALID_MAGIC && dfu_magic ==
 *     NOINIT_DFU_MAGIC (set by dfu_request() just before NVIC_SystemReset()).
 *     We are in the clean post-reset state: clear the magic, map system flash
 *     at 0, load the bootloader's SP/PC from 0x1FFF0000 and jump.
 *
 * (b) Bootloader exit: the ST bootloader "leave"/`-s` jumps straight to our
 *     Reset_Handler without a reset, leaving HSE/PLL, USB, SYSCFG remap etc.
 *     configured. Detect it via SYSCLK source != HSI and do ONE clean
 *     NVIC_SystemReset() (clean_reset_count guards against loops; main()
 *     clears the counter after a successful normal boot).
 */

#include "stm32f4xx.h"
#include "noinit.h"

#define SYSMEM_BASE 0x1FFF0000u

volatile noinit_t g_noinit __attribute__((section(".noinit")));

void early_boot_check(void) __attribute__((used, noinline));

void early_boot_check(void)
{
    volatile noinit_t *n = &g_noinit;

    if (n->valid != NOINIT_VALID_MAGIC) {
        /* Cold power-up (random SRAM) or first boot: initialise everything
           before trusting any field. */
        /* volatile pointer: keeps GCC from turning this loop into memset(). */
        volatile uint32_t *p = (volatile uint32_t *)(uintptr_t)n;
        for (uint32_t i = 0; i < sizeof(noinit_t) / 4u; i++) {
            p[i] = 0u;
        }
        n->valid = NOINIT_VALID_MAGIC;
        n->boot_flags = NOINIT_BOOT_COLD;
    } else {
        n->boot_flags = 0u;
    }
    n->boot_count = n->boot_count + 1u;

    if (n->dfu_magic == NOINIT_DFU_MAGIC) {
        n->dfu_magic = 0u;

        RCC->APB2ENR |= RCC_APB2ENR_SYSCFGEN;
        (void)RCC->APB2ENR;
        SYSCFG->MEMRMP = SYSCFG_MEMRMP_MEM_MODE_0;  /* 01: system flash at 0x00000000 */
        __DSB();
        __ISB();

        uint32_t boot_sp = *(volatile uint32_t *)(SYSMEM_BASE + 0u);
        uint32_t boot_pc = *(volatile uint32_t *)(SYSMEM_BASE + 4u);

        /* Both values are in registers before MSP changes; after `msr msp`
           no C stack frame may be used. PRIMASK is restored to its reset
           value (0) because the bootloader assumes post-reset state. */
        __asm volatile(
            "msr msp, %0 \n"
            "dsb         \n"
            "isb         \n"
            "cpsie i     \n"
            "bx  %1      \n"
            :
            : "r"(boot_sp), "r"(boot_pc)
            : "memory");
        __builtin_unreachable();
    }

    if ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_HSI) {
        /* Arrived from the bootloader (or a debugger "go") with clocks set up.
           A real reset returns RCC/USB/SYSCFG to a known state. */
        if (n->clean_reset_count == 0u) {
            n->clean_reset_count = 1u;
            NVIC_SystemReset();
        }
        /* Already tried once: proceed anyway (SystemInit/clock_init cope). */
    }

    if (n->clean_reset_count != 0u) {
        n->boot_flags |= NOINIT_BOOT_CLEAN_RESET;
    }
}
