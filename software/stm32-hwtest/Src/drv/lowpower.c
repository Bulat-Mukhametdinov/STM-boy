#include "lowpower.h"
#include <string.h>
#include "adc.h"
#include "backlight.h"
#include "board.h"
#include "clock.h"
#include "input.h"
#include "lcd.h"
#include "power.h"
#include "w25q.h"
#include "timebase.h"
#include "stm32f4xx_hal.h"

extern PCD_HandleTypeDef hpcd;

volatile bool lp_off_req_seen;
volatile uint32_t lp_off_req_edge_us;
volatile uint32_t lp_off_req_edge_ms;
static volatile uint32_t s_wake_flags;

void EXTI2_IRQHandler(void);
void RTC_WKUP_IRQHandler(void);

void EXTI2_IRQHandler(void)
{
    if (EXTI->PR & (1u << 2)) {
        EXTI->PR = (1u << 2);
        if (!lp_off_req_seen) {
            lp_off_req_edge_us = TIM2->CNT;
            lp_off_req_edge_ms = HAL_GetTick();
            lp_off_req_seen = true;
        }
        s_wake_flags |= LP_WAKE_OFFREQ;
    }
}

void RTC_WKUP_IRQHandler(void)
{
    EXTI->PR = (1u << 22);
    RTC->ISR = (uint32_t)(~(RTC_ISR_WUTF | RTC_ISR_INIT) | (RTC->ISR & RTC_ISR_INIT)); /* never set INIT: it stops the RTC */
    s_wake_flags |= LP_WAKE_TIMER;
}

void lp_off_req_arm(void)
{
    RCC->APB2ENR |= RCC_APB2ENR_SYSCFGEN;
    (void)RCC->APB2ENR;
    SYSCFG->EXTICR[0] &= ~(0xFu << 8);          /* line 2 = PA2 */
    lp_off_req_seen = false;
    EXTI->PR = (1u << 2);
    EXTI->RTSR &= ~(1u << 2);
    EXTI->FTSR |= (1u << 2);
    EXTI->IMR |= (1u << 2);
    NVIC_SetPriority(EXTI2_IRQn, 7);
    NVIC_ClearPendingIRQ(EXTI2_IRQn);
    NVIC_EnableIRQ(EXTI2_IRQn);
}

/* ---------------------------------------------------------------- RTC */

static uint32_t bcd(uint32_t v) { return ((v >> 4) & 0xFu) * 10u + (v & 0xFu); }

static uint32_t rtc_sod(void)
{
    uint32_t tr = RTC->TR;
    (void)RTC->DR;                               /* unlock the shadow registers */
    return bcd((tr >> 16) & 0x3Fu) * 3600u + bcd((tr >> 8) & 0x7Fu) * 60u + bcd(tr & 0x7Fu);
}

static uint32_t s_rtc0;

static void rtc_unlock(void)
{
    RTC->WPR = 0xCAu;
    RTC->WPR = 0x53u;
}

void lp_rtc_start(void)
{
    RCC->APB1ENR |= RCC_APB1ENR_PWREN;
    (void)RCC->APB1ENR;
    PWR->CR |= PWR_CR_DBP;
    /* LSION lives in RCC_CSR and is cleared by every system reset, while the
       RTC config in BDCR survives it: always (re)start the LSI. */
    RCC->CSR |= RCC_CSR_LSION;
    for (uint32_t n = 0; !(RCC->CSR & RCC_CSR_LSIRDY) && n < 1000000u; n++) {
    }
    if ((RCC->BDCR & RCC_BDCR_RTCEN) == 0u || (RCC->BDCR & RCC_BDCR_RTCSEL) != RCC_BDCR_RTCSEL_1) {
        RCC->BDCR |= RCC_BDCR_BDRST;             /* RTCSEL can only change after a backup-domain reset */
        RCC->BDCR &= ~RCC_BDCR_BDRST;
        RCC->BDCR |= RCC_BDCR_RTCSEL_1 | RCC_BDCR_RTCEN;   /* LSI */
        rtc_unlock();
        RTC->ISR = RTC_ISR_INIT;
        for (uint32_t n = 0; !(RTC->ISR & RTC_ISR_INITF) && n < 1000000u; n++) {
        }
        RTC->PRER = (249u) | (127u << 16);       /* LSI/128/250 ~ 1 Hz */
        RTC->CR &= ~RTC_CR_FMT;
        RTC->TR = 0;
        RTC->ISR &= ~RTC_ISR_INIT;
    }
    s_rtc0 = rtc_sod();
}

uint32_t lp_rtc_seconds(void)
{
    uint32_t n = rtc_sod();
    return (n >= s_rtc0) ? (n - s_rtc0) : (n + 86400u - s_rtc0);
}

static void wut_arm(uint32_t secs)
{
    rtc_unlock();
    RTC->CR &= ~RTC_CR_WUTE;
    for (uint32_t n = 0; !(RTC->ISR & RTC_ISR_WUTWF) && n < 1000000u; n++) {
    }
    RTC->WUTR = (secs ? secs - 1u : 0u) & 0xFFFFu;
    RTC->CR = (RTC->CR & ~RTC_CR_WUCKSEL) | RTC_CR_WUCKSEL_2 | RTC_CR_WUTIE;   /* ck_spre */
    RTC->ISR = (uint32_t)(~(RTC_ISR_WUTF | RTC_ISR_INIT) | (RTC->ISR & RTC_ISR_INIT)); /* never set INIT: it stops the RTC */
    RTC->CR |= RTC_CR_WUTE;
    EXTI->PR = (1u << 22);
    EXTI->RTSR |= (1u << 22);
    EXTI->IMR |= (1u << 22);
    NVIC_SetPriority(RTC_WKUP_IRQn, 7);
    NVIC_ClearPendingIRQ(RTC_WKUP_IRQn);
    NVIC_EnableIRQ(RTC_WKUP_IRQn);
}

static void wut_disarm(void)
{
    rtc_unlock();
    RTC->CR &= ~(RTC_CR_WUTE | RTC_CR_WUTIE);
    EXTI->IMR &= ~(1u << 22);
    NVIC_DisableIRQ(RTC_WKUP_IRQn);
}

/* ---------------------------------------------------------------- peripherals */

void lp_peripherals_sleep(void)
{
    adc_shutdown();
    backlight_off();
    lcd_sleep(true);
    w25q_power_down();
    HAL_PCD_DevDisconnect(&hpcd);
}

void lp_peripherals_wake(void)
{
    power_sense_init();
    w25q_release_power_down();
    lcd_sleep(false);
    HAL_PCD_DevConnect(&hpcd);
}

/* ---------------------------------------------------------------- Stop */

lp_stop_info_t lp_last_stop;

uint32_t lp_stop(uint32_t timeout_s, uint32_t mask)
{
    uint32_t t_in = time_us();
    memset(&lp_last_stop, 0, sizeof(lp_last_stop));
    uint32_t exti9_save = SYSCFG->EXTICR[2];
    bool vbus = (mask & LP_WAKE_VBUS) != 0u;

    s_wake_flags = 0;
    if (mask & LP_WAKE_OFFREQ) {
        lp_off_req_arm();
        if (power_off_req()) {
            return LP_WAKE_OFFREQ;
        }
    }
    if (vbus) {
        RCC->APB2ENR |= RCC_APB2ENR_SYSCFGEN;
        (void)RCC->APB2ENR;
        input_exti_enable(false);
        SYSCFG->EXTICR[2] &= ~(0xFu << 4);       /* line 9 = PA9 (shared with PB9 UP) */
        EXTI->PR = (1u << 9);
        EXTI->FTSR &= ~(1u << 9);
        EXTI->RTSR |= (1u << 9);
        EXTI->IMR |= (1u << 9);
        NVIC_SetPriority(EXTI9_5_IRQn, 7);
        NVIC_ClearPendingIRQ(EXTI9_5_IRQn);
        NVIC_EnableIRQ(EXTI9_5_IRQn);
        if (power_vbus()) {
            s_wake_flags |= LP_WAKE_VBUS;
        }
    }
    if (timeout_s) {
        wut_arm(timeout_s);
    }

    uint32_t cause = 0;
    if (!(s_wake_flags & mask)) {
        HAL_SuspendTick();
        HAL_PWREx_EnableFlashPowerDown();
        lp_last_stop.entry_us = time_us() - t_in;
        uint32_t c0 = 0;
        for (;;) {
            HAL_PWR_EnterSTOPMode(PWR_LOWPOWERREGULATOR_ON, PWR_STOPENTRY_WFI);
            c0 = DWT->CYCCNT;           /* first cycles after the wake-up: core runs from HSI 16 MHz */
            if (vbus && power_vbus()) {
                s_wake_flags |= LP_WAKE_VBUS;
            }
            if (s_wake_flags & mask) {
                break;
            }
        }
        HAL_PWREx_DisableFlashPowerDown();
        /* R21 (review 2026-11-21): resume the tick BEFORE the HSE/PLL bring-up so the HAL timeouts inside
           clock_init_96mhz() can fire (SysTick runs ~6x slow on HSI until the switch: still bounded). */
        HAL_ResumeTick();
        clock_init_96mhz();
        lp_last_stop.clk_cyc = DWT->CYCCNT - c0;
    }
    cause = s_wake_flags & mask;
    lp_last_stop.cause = cause;

    if (timeout_s) {
        wut_disarm();
    }
    if (vbus) {
        EXTI->IMR &= ~(1u << 9);
        EXTI->RTSR &= ~(1u << 9);
        EXTI->PR = (1u << 9);
        NVIC_DisableIRQ(EXTI9_5_IRQn);
        SYSCFG->EXTICR[2] = exti9_save;
    }
    return cause;
}
