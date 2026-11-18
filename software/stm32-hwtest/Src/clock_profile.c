/* Run-time clock profiles (block B): SYSCLK = N MHz from HSE 8 MHz (M = 4 -> 2 MHz PLL input,
   VCO = 2*N_pll MHz, P = 2/4/6/8, Q = VCO/48 when integral so USB keeps working).
   Everything that depends on the clock is recomputed in clock_apply_derived(). */

#include "clock.h"
#include "adc.h"
#include "audio.h"
#include "backlight.h"
#include "lcd.h"
#include "log.h"
#include "timebase.h"
#include "usb.h"
#include "w25q.h"
#include "stm32f4xx_hal.h"

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

#define PLL_M 4u
#define HSE_MHZ 8u
#define LOCK_SPIN 300000u

static uint32_t s_lcd_target;     /* Hz, 0 = not captured yet */
static uint32_t s_w25q_target;
static uint32_t s_adc_target = 24500000u;
static bool s_targets_valid;

void clock_lcd_target_set(uint32_t hz)
{
    s_lcd_target = hz;
}

uint32_t clock_sysclk_mhz(void) { return SystemCoreClock / 1000000u; }
uint32_t clock_ws_now(void) { return FLASH->ACR & FLASH_ACR_LATENCY; }

static uint32_t div_bits(uint32_t d)
{
    return d == 1u ? 0u : d == 2u ? 4u : d == 4u ? 5u : 6u;   /* PPRE field (/8 max used) */
}

static uint32_t bits_div(uint32_t b)
{
    return (b & 4u) ? (2u << (b & 3u)) : 1u;
}

/* ------------------------------------------------------------------ planning */

bool clock_plan(uint32_t mhz, const clock_opts_t *o, clock_plan_t *out)
{
    if (mhz < 8u || mhz > 300u) {
        return false;
    }
    memset(out, 0, sizeof(*out));
    /* PLL: prefer a (P, VCO) with VCO a multiple of 48 MHz inside 192..432 (USB), else any P with
       VCO in 192..432, else P = 2 and whatever the multiplier allows (out of the nominal range). */
    int best = -1, anyp = -1;
    for (uint32_t p = 2; p <= 8u; p += 2u) {
        uint32_t vco = mhz * p;
        if ((vco & 1u) || vco / 2u < 50u || vco / 2u > 432u) {
            continue;
        }
        if (vco >= 192u && vco <= 432u) {
            if (best < 0 && (vco % 48u) == 0u) best = (int)p;
            if (anyp < 0) anyp = (int)p;
        }
    }
    uint32_t p = best > 0 ? (uint32_t)best : (anyp > 0 ? (uint32_t)anyp : 2u);
    uint32_t vco = mhz * p;
    if (vco & 1u) {
        return false;   /* odd VCO: not representable with a 2 MHz step */
    }
    out->mhz = (uint16_t)mhz;
    out->p = (uint8_t)p;
    out->n = (uint16_t)(vco / 2u);
    if (out->n < 50u || out->n > 432u) {
        return false;
    }
    uint32_t q = (vco % 48u == 0u) ? vco / 48u : (vco + 24u) / 48u;
    if (q < 2u) q = 2u;
    if (q > 15u) q = 15u;
    out->q = (uint8_t)q;
    out->usb_ok = (vco % 48u == 0u) && (vco / 48u) >= 2u && (vco / 48u) <= 15u && mhz >= 30u;

    int ws = (int)((mhz + 29u) / 30u) - 1;
    int ws_spec = ws;
    if (mhz > 100u) ws++;
    if (o && o->ws >= 0) ws = o->ws;
    if (ws < 0) ws = 0;
    if (ws > 15) ws = 15;
    out->ws = (uint8_t)ws;

    uint32_t a1 = (o && o->apb1_max_hz) ? o->apb1_max_hz : 50000000u;
    uint32_t a2 = (o && o->apb2_max_hz) ? o->apb2_max_hz : 100000000u;
    uint32_t hz = mhz * 1000000u;
    uint32_t d1 = 1u, d2 = 1u;
    while (d1 < 8u && hz / d1 > a1) d1 <<= 1;
    while (d2 < 8u && hz / d2 > a2) d2 <<= 1;
    out->apb1_div = (uint8_t)d1;
    out->apb2_div = (uint8_t)d2;
    out->in_spec = (mhz <= 100u) && (ws >= ws_spec) && (hz / d1 <= 50000000u) && (hz / d2 <= 100000000u);
    return true;
}

/* ------------------------------------------------------------------ register-level switch */

static void set_ws(uint32_t ws)
{
    FLASH->ACR = (FLASH->ACR & ~FLASH_ACR_LATENCY) | ws;
    for (uint32_t i = 0; i < 1000u && (FLASH->ACR & FLASH_ACR_LATENCY) != ws; i++) {
    }
}

static bool to_hsi(void)
{
    RCC->CR |= RCC_CR_HSION;
    for (uint32_t i = 0; i < LOCK_SPIN && !(RCC->CR & RCC_CR_HSIRDY); i++) {
    }
    if (!(RCC->CR & RCC_CR_HSIRDY)) {
        return false;
    }
    RCC->CFGR = (RCC->CFGR & ~RCC_CFGR_SW) | RCC_CFGR_SW_HSI;
    for (uint32_t i = 0; i < LOCK_SPIN; i++) {
        if ((RCC->CFGR & RCC_CFGR_SWS) == RCC_CFGR_SWS_HSI) {
            RCC->CFGR &= ~(RCC_CFGR_HPRE | RCC_CFGR_PPRE1 | RCC_CFGR_PPRE2);
            return true;
        }
    }
    return false;
}

static clock_status_t do_switch(const clock_plan_t *p)
{
    if (!(RCC->CR & RCC_CR_HSERDY)) {
        return CLOCK_ERR_HSE;
    }
    set_ws(7u);                       /* raising WS is always safe */
    if (!to_hsi()) {
        return CLOCK_ERR_SWITCH;
    }
    RCC->CR &= ~RCC_CR_PLLON;
    for (uint32_t i = 0; i < LOCK_SPIN && (RCC->CR & RCC_CR_PLLRDY); i++) {
    }
    RCC->PLLCFGR = PLL_M | ((uint32_t)p->n << RCC_PLLCFGR_PLLN_Pos) |
                   (((uint32_t)(p->p / 2u - 1u)) << RCC_PLLCFGR_PLLP_Pos) | RCC_PLLCFGR_PLLSRC_HSE |
                   ((uint32_t)p->q << RCC_PLLCFGR_PLLQ_Pos);
    RCC->CR |= RCC_CR_PLLON;
    uint32_t i;
    for (i = 0; i < LOCK_SPIN && !(RCC->CR & RCC_CR_PLLRDY); i++) {
    }
    if (!(RCC->CR & RCC_CR_PLLRDY)) {
        RCC->CR &= ~RCC_CR_PLLON;
        return CLOCK_ERR_PLL;
    }
    set_ws(p->ws);
    RCC->CFGR = (RCC->CFGR & ~(RCC_CFGR_HPRE | RCC_CFGR_PPRE1 | RCC_CFGR_PPRE2)) |
                (div_bits(p->apb1_div) << RCC_CFGR_PPRE1_Pos) | (div_bits(p->apb2_div) << RCC_CFGR_PPRE2_Pos);
    RCC->CFGR = (RCC->CFGR & ~RCC_CFGR_SW) | RCC_CFGR_SW_PLL;
    for (i = 0; i < LOCK_SPIN; i++) {
        if ((RCC->CFGR & RCC_CFGR_SWS) == RCC_CFGR_SWS_PLL) {
            return CLOCK_OK;
        }
    }
    (void)to_hsi();
    return CLOCK_ERR_SWITCH;
}

/* ------------------------------------------------------------------ derived clocks */

uint16_t clock_lcd_div_for_hz(uint32_t hz)
{
    uint32_t pclk = HAL_RCC_GetPCLK2Freq();
    uint32_t d = 2;
    while (d < 256u && pclk / d > hz) d <<= 1;
    return (uint16_t)d;
}

static uint32_t apb_div_of(uint32_t ppre_pos)
{
    return bits_div((RCC->CFGR >> ppre_pos) & 7u);
}

void clock_apply_derived(void)
{
    SystemCoreClockUpdate();
    HAL_InitTick(TICK_INT_PRIORITY);     /* SysTick 1 kHz from the new HCLK (uwTick is kept) */
    timebase_retune();                   /* TIM2 1 MHz */

    /* SPI1 (LCD) <= target on APB2 */
    if (s_lcd_target == 0u) s_lcd_target = 48000000u;
    lcd_apply_spi_div(clock_lcd_div_for_hz(s_lcd_target));
    /* SPI3 (W25Q) <= target on APB1 */
    if (s_w25q_target == 0u) s_w25q_target = 12000000u;
    {
        uint32_t pclk1 = HAL_RCC_GetPCLK1Freq(), d = 2;
        while (d < 256u && pclk1 / d > s_w25q_target) d <<= 1;
        w25q_set_div((uint16_t)d);
    }
    /* ADC (PCLK2/4, /6, /8: never /2) <= target */
    {
        uint32_t pclk2 = HAL_RCC_GetPCLK2Freq(), idx = 1;
        while (idx < 3u && pclk2 / (2u * (idx + 1u)) > s_adc_target) idx++;
        adc_set_pre(idx);
    }
    backlight_retune();                  /* TIM3 */
    audio_clock_changed();               /* TIM1 / TIM5 / RCR: Fs stays nominal */
}

bool clock_usb_ok_now(void)
{
    if ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_PLL) {
        return false;
    }
    uint32_t n = (RCC->PLLCFGR & RCC_PLLCFGR_PLLN) >> RCC_PLLCFGR_PLLN_Pos;
    uint32_t q = (RCC->PLLCFGR & RCC_PLLCFGR_PLLQ) >> RCC_PLLCFGR_PLLQ_Pos;
    uint32_t vco = (HSE_MHZ / PLL_M) * n;
    return q >= 2u && (vco % q) == 0u && (vco / q) == 48u && SystemCoreClock >= 30000000u;
}

clock_status_t clock_set_mhz(uint32_t mhz, const clock_opts_t *o)
{
    clock_plan_t pl, p96;
    if (!clock_plan(mhz, o, &pl)) {
        return CLOCK_ERR_PLL;
    }
    (void)clock_plan(96u, NULL, &p96);
    if (!s_targets_valid) {              /* capture the rates the board is configured for right now */
        if (s_lcd_target == 0u) s_lcd_target = lcd_spi_hz();
        if (s_w25q_target == 0u) s_w25q_target = w25q_spi_hz();
        s_targets_valid = true;
    }
    bool usb_was = usb_is_running();
    if (usb_was) {            /* a live USB transfer does not survive the PLL restart: always stop cleanly */
        usb_stop();
    }
    uint32_t pm = __get_PRIMASK();
    __disable_irq();
    clock_status_t st = do_switch(&pl);
    if (st != CLOCK_OK && mhz != 96u) {
        (void)do_switch(&p96);           /* known-good profile; the caller still gets the error */
    }
    clock_apply_derived();
    if (!pm) {
        __enable_irq();
    }
    if (!usb_is_running() && clock_usb_ok_now()) {   /* also after a stretch without USB (e.g. 100 MHz -> 120 MHz) */
        usb_start();
    }
    return st;
}

/* ------------------------------------------------------------------ info + command */

void clock_emit_info(void)
{
    static jb_t j;
    uint32_t n = (RCC->PLLCFGR & RCC_PLLCFGR_PLLN) >> RCC_PLLCFGR_PLLN_Pos;
    uint32_t p = 2u * (((RCC->PLLCFGR & RCC_PLLCFGR_PLLP) >> RCC_PLLCFGR_PLLP_Pos) + 1u);
    uint32_t q = (RCC->PLLCFGR & RCC_PLLCFGR_PLLQ) >> RCC_PLLCFGR_PLLQ_Pos;
    uint32_t d1 = apb_div_of(RCC_CFGR_PPRE1_Pos), d2 = apb_div_of(RCC_CFGR_PPRE2_Pos);
    uint32_t pc1 = HAL_RCC_GetPCLK1Freq(), pc2 = HAL_RCC_GetPCLK2Freq();
    uint32_t adc = pc2 / adc_pre_div();
    jb_begin(&j, "clk");
    jb_u32(&j, "sysclk", SystemCoreClock);
    jb_u32(&j, "hclk", HAL_RCC_GetHCLKFreq());
    jb_u32(&j, "pclk1", pc1);
    jb_u32(&j, "pclk2", pc2);
    jb_u32(&j, "tim1", d1 > 1u ? 2u * pc1 : pc1);   /* APB1 timers (TIM2/3/5) */
    jb_u32(&j, "tim2", d2 > 1u ? 2u * pc2 : pc2);   /* APB2 timers (TIM1) */
    jb_u32(&j, "ws", clock_ws_now());
    jb_u32(&j, "apb1_div", d1);
    jb_u32(&j, "apb2_div", d2);
    jb_u32(&j, "pll_n", n);
    jb_u32(&j, "pll_p", p);
    jb_u32(&j, "pll_q", q);
    jb_u32(&j, "spi1_hz", lcd_spi_hz());
    jb_u32(&j, "spi3_hz", w25q_spi_hz());
    jb_u32(&j, "adc_hz", adc);
    jb_u32(&j, "bl_hz", backlight_freq_hz());
    jb_bool(&j, "usb_ok", clock_usb_ok_now());
    jb_bool(&j, "usb_up", usb_is_running());
    jb_str(&j, "status", clock_status_str(clock_last_status()));
    jb_end(&j);
    jb_emit(&j, "@R ");
}

static uint32_t kv_mhz(int argc, char **argv, const char *key, uint32_t def_hz)
{
    size_t kl = strlen(key);
    for (int i = 2; i < argc; i++) {
        if (strncmp(argv[i], key, kl) == 0 && argv[i][kl] == '=') {
            return (uint32_t)(strtod(&argv[i][kl + 1], NULL) * 1e6 + 0.5);
        }
    }
    return def_hz;
}

void clk_cmd(int argc, char **argv)
{
    if (argc >= 2 && strcmp(argv[1], "info") == 0) {
        clock_emit_info();
        proto_ok("clk");
        return;
    }
    if (argc >= 3 && strcmp(argv[1], "set") == 0) {
        uint32_t mhz = (uint32_t)strtoul(argv[2], NULL, 0);
        clock_opts_t o = {.ws = -1};
        for (int i = 3; i < argc; i++) {
            if (strncmp(argv[i], "ws=", 3) == 0) o.ws = (int)strtol(&argv[i][3], NULL, 0);
        }
        o.apb1_max_hz = kv_mhz(argc, argv, "apb1_max", 0);
        o.apb2_max_hz = kv_mhz(argc, argv, "apb2_max", 0);
        uint32_t t = kv_mhz(argc, argv, "lcd", 0);
        if (t) clock_lcd_target_set(t);
        t = kv_mhz(argc, argv, "w25q", 0);
        if (t) { s_w25q_target = t; s_targets_valid = true; }
        t = kv_mhz(argc, argv, "adc", 0);
        if (t) s_adc_target = t;
        clock_plan_t pl;
        if (!clock_plan(mhz, &o, &pl)) {
            proto_err("clk", "unreachable frequency (8..300 MHz, even VCO)");
            return;
        }
        res_printf("{\"t\":\"clk\",\"plan\":{\"mhz\":%u,\"n\":%u,\"p\":%u,\"q\":%u,\"ws\":%u,\"apb1_div\":%u,"
                   "\"apb2_div\":%u,\"usb_ok\":%s,\"in_spec\":%s}}",
                   pl.mhz, pl.n, pl.p, pl.q, pl.ws, pl.apb1_div, pl.apb2_div, pl.usb_ok ? "true" : "false",
                   pl.in_spec ? "true" : "false");
        log_flush(100);
        clock_status_t st = clock_set_mhz(mhz, &o);
        clock_emit_info();
        if (st == CLOCK_OK) {
            proto_ok("clk");
        } else {
            proto_err("clk", clock_status_str(st));
        }
        return;
    }
    proto_err("clk", "usage: clk info | clk set <MHz> [ws=] [apb1_max=MHz] [apb2_max=MHz] [lcd=MHz] [w25q=MHz] [adc=MHz]");
}
