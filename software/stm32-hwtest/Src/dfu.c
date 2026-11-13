#include "dfu.h"
#include "noinit.h"
#include "stm32f4xx.h"

void dfu_request(void)
{
    __disable_irq();
    g_noinit.valid = NOINIT_VALID_MAGIC;
    g_noinit.dfu_magic = NOINIT_DFU_MAGIC;
    __DSB();
    /* The reset also drops the USB D+ pull-up: the host sees a disconnect. */
    NVIC_SystemReset();
}
