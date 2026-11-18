#include "timebase.h"
#include "clock.h"
#include "input.h"
#include "stm32f4xx_hal.h"

void timebase_init(void)
{
    /* DWT cycle counter */
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

    /* TIM2: 32-bit up-counter, 1 MHz, free running, no interrupt */
    RCC->APB1ENR |= RCC_APB1ENR_TIM2EN;
    (void)RCC->APB1ENR;
    TIM2->CR1 = 0;
    TIM2->PSC = (clock_tim_apb1_hz() / 1000000u) - 1u;
    TIM2->ARR = 0xFFFFFFFFu;
    TIM2->CNT = 0;
    TIM2->EGR = TIM_EGR_UG;  /* load PSC */
    TIM2->SR = 0;
    TIM2->CR1 = TIM_CR1_CEN;
}

uint32_t time_ms(void) { return HAL_GetTick(); }

void delay_us(uint32_t us)
{
    uint32_t t0 = time_us();
    while ((uint32_t)(time_us() - t0) < us) {
    }
}

void timebase_systick_hook(void)
{
    input_tick_1ms();
}

/* SYSCLK/APB changed: keep TIM2 at 1 MHz without losing its count. */
void timebase_retune(void)
{
    uint32_t cnt = TIM2->CNT;
    TIM2->PSC = (clock_tim_apb1_hz() / 1000000u) - 1u;
    TIM2->EGR = TIM_EGR_UG;   /* load PSC (also clears CNT) */
    TIM2->CNT = cnt;
    TIM2->SR = 0;
}
