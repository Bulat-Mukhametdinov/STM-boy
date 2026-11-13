/* Exception and interrupt handlers. */

#include "main.h"
#include "noinit.h"
#include "timebase.h"

extern PCD_HandleTypeDef hpcd;

void NMI_Handler(void);
void HardFault_Handler(void);
void MemManage_Handler(void);
void BusFault_Handler(void);
void UsageFault_Handler(void);
void SVC_Handler(void);
void DebugMon_Handler(void);
void PendSV_Handler(void);
void SysTick_Handler(void);
void OTG_FS_IRQHandler(void);
void fault_record(uint32_t *frame) __attribute__((used, noreturn));

/* Fault breadcrumbs: store the stacked PC/LR and fault status registers in
   the .noinit block (reported by `info` after the reset), then reset. */
void fault_record(uint32_t *frame)
{
    g_noinit.fault_count++;
    g_noinit.fault_pc = frame[6];
    g_noinit.fault_lr = frame[5];
    g_noinit.fault_cfsr = SCB->CFSR;
    g_noinit.fault_hfsr = SCB->HFSR;
    g_noinit.fault_ipsr = __get_IPSR();
    if (SCB->CFSR & SCB_CFSR_MMARVALID_Msk) {
        g_noinit.fault_addr = SCB->MMFAR;
    } else if (SCB->CFSR & SCB_CFSR_BFARVALID_Msk) {
        g_noinit.fault_addr = SCB->BFAR;
    } else {
        g_noinit.fault_addr = 0;
    }
    __DSB();
    NVIC_SystemReset();
}

#define FAULT_ENTRY(name)                                   \
    __attribute__((naked)) void name(void)                  \
    {                                                       \
        __asm volatile("tst   lr, #4      \n"               \
                       "ite   eq          \n"               \
                       "mrseq r0, msp     \n"               \
                       "mrsne r0, psp     \n"               \
                       "b     fault_record\n");             \
    }

FAULT_ENTRY(HardFault_Handler)
FAULT_ENTRY(MemManage_Handler)
FAULT_ENTRY(BusFault_Handler)
FAULT_ENTRY(UsageFault_Handler)

void NMI_Handler(void)
{
    while (1) {
    }
}

void SVC_Handler(void) {}
void DebugMon_Handler(void) {}
void PendSV_Handler(void) {}

void SysTick_Handler(void)
{
    HAL_IncTick();
    timebase_systick_hook();
}

void OTG_FS_IRQHandler(void)
{
    HAL_PCD_IRQHandler(&hpcd);
}
