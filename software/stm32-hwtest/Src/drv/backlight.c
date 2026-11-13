/* LCD backlight PWM: TIM3_CH1 on PA6 (AF2), PWM mode 1, edge aligned.
   duty 0      -> CCR1 = 0       (output constantly low)
   duty 100 %  -> CCR1 = ARR + 1 (output constantly high) */

#include "backlight.h"
#include "board.h"
#include "clock.h"

/* ((i+1)/16)^2.2 * 1e6, i = 0..15 (perceptual, gamma 2.2) */
static const uint32_t s_level_ppm[BL_LEVELS] = {
    2244,   10309,  25154,  47366,  77387,  115576, 162238, 217638,
    282013, 355579, 438531, 531049, 633303, 745449, 867634, 1000000,
};

static uint32_t s_ppm;
static uint32_t s_hz = 1000;

uint32_t backlight_level_ppm(uint8_t level)
{
    return s_level_ppm[level < BL_LEVELS ? level : BL_LEVELS - 1u];
}

static void apply_duty(void)
{
    uint32_t period = TIM3->ARR + 1u;
    uint32_t ccr;
    if (s_ppm >= 1000000u) {
        ccr = period; /* > ARR: always high */
    } else {
        ccr = (uint32_t)(((uint64_t)period * s_ppm + 500000u) / 1000000u);
        if (ccr == 0u && s_ppm > 0u) {
            ccr = 1u; /* smallest non-zero pulse */
        }
    }
    TIM3->CCR1 = ccr;
}

bool backlight_set_freq(uint32_t hz)
{
    if (hz < 50u || hz > 100000u) {
        return false;
    }
    uint32_t clk = clock_tim_apb1_hz();
    uint32_t ticks = clk / hz;                 /* total timer ticks per period */
    uint32_t psc = (ticks + 65535u) / 65536u;  /* smallest prescaler giving ARR <= 0xFFFF */
    if (psc == 0u) psc = 1u;
    uint32_t arr = (ticks + psc / 2u) / psc;   /* period in prescaled ticks */
    if (arr < 2u) arr = 2u;
    TIM3->CR1 &= ~TIM_CR1_CEN;
    TIM3->PSC = psc - 1u;
    TIM3->ARR = arr - 1u;
    TIM3->CNT = 0;
    apply_duty();
    TIM3->EGR = TIM_EGR_UG; /* load PSC/ARR/CCR1 now */
    TIM3->CR1 |= TIM_CR1_CEN;
    s_hz = hz;
    return true;
}

uint32_t backlight_freq_hz(void)
{
    return clock_tim_apb1_hz() / ((TIM3->PSC + 1u) * (TIM3->ARR + 1u));
}

void backlight_set_ppm(uint32_t ppm)
{
    s_ppm = (ppm > 1000000u) ? 1000000u : ppm;
    apply_duty();
}

uint32_t backlight_ppm(void) { return s_ppm; }

void backlight_set_level(uint8_t level) { backlight_set_ppm(backlight_level_ppm(level)); }
void backlight_full(void) { backlight_set_ppm(1000000u); }
void backlight_off(void) { backlight_set_ppm(0u); }

void backlight_init(void)
{
    RCC->APB1ENR |= RCC_APB1ENR_TIM3EN;
    (void)RCC->APB1ENR;
    TIM3->CR1 = TIM_CR1_ARPE;
    TIM3->CCMR1 = (6u << TIM_CCMR1_OC1M_Pos) | TIM_CCMR1_OC1PE; /* PWM mode 1, preload */
    TIM3->CCER = TIM_CCER_CC1E;                                /* active high */
    s_ppm = 0;
    backlight_set_freq(s_hz);
    gpio_config(LCD_BLK_PORT, LCD_BLK_PIN, GPIO_MODE_AF_, GPIO_PULL_NONE_, GPIO_SPEED_LOW_, 2);
}

void backlight_regs(backlight_regs_t *r)
{
    r->psc = TIM3->PSC;
    r->arr = TIM3->ARR;
    r->ccr1 = TIM3->CCR1;
    r->cr1 = TIM3->CR1;
    r->ccmr1 = TIM3->CCMR1;
    r->ccer = TIM3->CCER;
    r->timclk = clock_tim_apb1_hz();
    r->moder6 = (LCD_BLK_PORT->MODER >> (LCD_BLK_PIN * 2u)) & 3u;
    r->afr6 = (LCD_BLK_PORT->AFR[LCD_BLK_PIN >> 3] >> ((LCD_BLK_PIN & 7u) * 4u)) & 0xFu;
}
