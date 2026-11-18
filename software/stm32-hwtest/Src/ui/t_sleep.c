/* power.sleep and power.ampdip (block C).

   power.sleep: (a) LCD DISPOFF / SLPIN / SLPOUT / DISPON command times, backlight on/off times, full sleep-in/out
   times of lcd_sleep(); optional hold=<s> per state so a meter can be read; (b) Stop mode with RTC wake after 2 s:
   peripherals-sleep time, Stop entry, HSE+PLL restart (DWT cycles at HSI), peripherals-wake time, USB
   re-enumeration time; USB drops, so the numbers are kept in RAM and `run power.sleep report` re-emits them;
   (c) WFI wake latency (TIM5 compare) vs busy spin.

   power.ampdip [ms=200]: ADC1 IN1 (battery divider) sampled continuously by DMA2 S4 (ADCCLK 12 MHz, 480 cycles,
   ~24 kS/s) around amp enable / 1 kHz tone / amp disable. Arena 0..71999: u16 samples. */

#include "suites.h"
#include "adc.h"
#include "audio.h"
#include "backlight.h"
#include "bench.h"
#include "board.h"
#include "clock.h"
#include "cmd.h"
#include "gfx.h"
#include "input.h"
#include "lcd.h"
#include "log.h"
#include "lowpower.h"
#include "power.h"
#include "timebase.h"
#include "ui.h"
#include "usb.h"
#include "w25q.h"
#include "stm32f4xx_hal.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

/* ================================================================ power.sleep */

static struct {
    bool valid, stop_done;
    uint32_t dispoff_us, dispon_us, slpin_us, slpout_us, bl_off_us, bl_on_us, sleep_in_ms, sleep_out_ms;
    uint32_t p_sleep_us, entry_us, clk_cyc, p_wake_ms, usb_ms, rtc_s, cause, usb_how;
    uint32_t spin_min, spin_med, spin_max, wfi_min, wfi_med, wfi_max, mhz;
} R;

static void sort_u32(uint32_t *a, uint32_t n)
{
    for (uint32_t i = 1; i < n; i++) {
        uint32_t k = a[i], j = i;
        while (j > 0 && a[j - 1] > k) { a[j] = a[j - 1]; j--; }
        a[j] = k;
    }
}

static void sleep_emit(void)
{
    res_printf("{\"t\":\"sleep_lcd\",\"suite\":\"power.sleep\",\"dispoff_cmd_us\":%" PRIu32 ",\"dispon_cmd_us\":%" PRIu32
               ",\"slpin_cmd_us\":%" PRIu32 ",\"slpout_cmd_us\":%" PRIu32 ",\"backlight_off_us\":%" PRIu32
               ",\"backlight_on_us\":%" PRIu32 ",\"lcd_sleep_in_ms\":%" PRIu32 ",\"lcd_sleep_out_ms\":%" PRIu32
               ",\"note\":\"sleep-out includes the 120 ms SLPOUT wait + 10 ms; sleep-in a 5 ms wait; DISPOFF keeps GRAM, "
               "backlight stays on (panel shows blank); backlight off is instant (PWM)\"}",
               R.dispoff_us, R.dispon_us, R.slpin_us, R.slpout_us, R.bl_off_us, R.bl_on_us, R.sleep_in_ms,
               R.sleep_out_ms);
    if (R.stop_done) {
        res_printf("{\"t\":\"sleep_stop\",\"suite\":\"power.sleep\",\"cause\":%" PRIu32 ",\"rtc_s\":%" PRIu32
                   ",\"periph_sleep_us\":%" PRIu32 ",\"stop_entry_us\":%" PRIu32 ",\"clk_restart_cyc_hsi\":%" PRIu32
                   ",\"clk_restart_us_est\":%" PRIu32 ",\"periph_wake_ms\":%" PRIu32 ",\"usb_reenum_ms\":%" PRIu32
                   ",\"usb_how\":%" PRIu32 ",\"note\":\"clk_restart = wake-up -> HSE+PLL back at 96 MHz (~cyc/16 MHz); "
                   "usb_how 1 = usb_stop/usb_start then time to CONFIGURED (the stale CONFIGURED state after DevConnect is not trusted), 2 = not within 6 s; "
                   "periph_wake includes the 130 ms LCD wake\"}",
                   R.cause, R.rtc_s, R.p_sleep_us, R.entry_us, R.clk_cyc, R.clk_cyc / 16u, R.p_wake_ms, R.usb_ms,
                   R.usb_how);
    }
    res_printf("{\"t\":\"sleep_wfi\",\"suite\":\"power.sleep\",\"cpu_mhz\":%" PRIu32 ",\"spin_cyc\":[%" PRIu32 ",%" PRIu32
               ",%" PRIu32 "],\"wfi_cyc\":[%" PRIu32 ",%" PRIu32 ",%" PRIu32 "],\"wfi_us_med_x100\":%" PRIu32
               ",\"note\":\"min,med,max cycles TIM5 compare -> ISR; WFI = Sleep mode, clocks keep running\"}",
               R.mhz, R.spin_min, R.spin_med, R.spin_max, R.wfi_min, R.wfi_med, R.wfi_max,
               R.mhz ? R.wfi_med * 100u / R.mhz : 0u);
    res_printf("{\"t\":\"sleep_hint\",\"suite\":\"power.sleep\",\"measure_with_meter\":[\"A idle WFI, BL 100%%\","
               "\"B DISPOFF 0x28, BL on\",\"C BL off, display on\",\"D DISPOFF + BL off\",\"E SLPIN + BL off\","
               "\"F E + W25Q power-down\",\"G Stop (power.current S1)\"],\"how\":\"run power.sleep hold=15 and read the "
               "battery current (J2) during each 'state' line; USB stays active in A-F\"}");
}

static void hold_state(const char *id, const char *what, uint32_t hold_s)
{
    res_printf("{\"t\":\"sleep_state\",\"suite\":\"power.sleep\",\"state\":\"%s\",\"what\":\"%s\",\"hold_s\":%" PRIu32 "}",
               id, what, hold_s);
    ui_con_line(COLOR_CYAN, "%s %s", id, what);
    uint32_t t0 = HAL_GetTick();
    while ((uint32_t)(HAL_GetTick() - t0) < hold_s * 1000u && !abort_requested()) {
        sys_service();
        __WFI();
    }
}

suite_verdict_t suite_power_sleep(int argc, char **argv)
{
    if (suite_has_word(argc, argv, "report")) {
        if (!R.valid) {
            res_printf("{\"t\":\"error\",\"suite\":\"power.sleep\",\"why\":\"no stored result since boot\"}");
            return SUITE_FAIL;
        }
        sleep_emit();
        return SUITE_PASS;
    }
    uint32_t hold = suite_arg_u32(argc, argv, "hold", 0u);
    bool do_stop = suite_arg_u32(argc, argv, "stop", 1u) != 0u;
    memset(&R, 0, sizeof(R));
    R.mhz = SystemCoreClock / 1000000u;

    /* (a) LCD + backlight */
    lcd_fill_rect(0, 0, 160, 128, COLOR_WHITE);
    backlight_full();
    uint32_t t = time_us();
    lcd_cmd_raw(0x28, NULL, 0);
    R.dispoff_us = time_us() - t;
    hold_state("A", "idle WFI, BL 100%", hold);
    hold_state("B", "DISPOFF 0x28, BL on", hold);
    t = time_us();
    lcd_cmd_raw(0x29, NULL, 0);
    R.dispon_us = time_us() - t;
    t = time_us();
    backlight_off();
    R.bl_off_us = time_us() - t;
    hold_state("C", "BL off, display on", hold);
    t = time_us();
    backlight_full();
    R.bl_on_us = time_us() - t;
    lcd_cmd_raw(0x28, NULL, 0);
    backlight_off();
    hold_state("D", "DISPOFF + BL off", hold);
    lcd_cmd_raw(0x29, NULL, 0);
    t = time_us();
    lcd_cmd_raw(0x10, NULL, 0);
    R.slpin_us = time_us() - t;
    HAL_Delay(5);
    hold_state("E", "SLPIN + BL off", hold);
    t = time_us();
    lcd_cmd_raw(0x11, NULL, 0);
    R.slpout_us = time_us() - t;
    HAL_Delay(120);
    lcd_cmd_raw(0x29, NULL, 0);
    HAL_Delay(10);
    backlight_full();
    if (hold) {
        lcd_sleep(true);
        backlight_off();
        w25q_power_down();
        hold_state("F", "SLPIN + BL off + W25Q power-down", hold);
        w25q_release_power_down();
        lcd_sleep(false);
        backlight_full();
    }
    t = time_us();
    backlight_off();
    lcd_sleep(true);
    R.sleep_in_ms = (time_us() - t) / 1000u;
    t = time_us();
    lcd_sleep(false);
    R.sleep_out_ms = (time_us() - t) / 1000u;
    backlight_full();
    lcd_fill_rect(0, 0, 160, 128, COLOR_BLACK);
    if (abort_requested()) return SUITE_ABORT;

    /* (c) WFI wake latency */
    uint32_t v[33];
    bench_wake_cycles(false, 19200u, v, 33);
    sort_u32(v, 33);
    R.spin_min = v[0]; R.spin_med = v[16]; R.spin_max = v[32];
    bench_wake_cycles(true, 19200u, v, 33);
    sort_u32(v, 33);
    R.wfi_min = v[0]; R.wfi_med = v[16]; R.wfi_max = v[32];
    R.valid = true;

    /* (b) Stop, RTC wake after 2 s (USB drops) */
    if (do_stop) {
        ui_con_line(COLOR_YELLOW, "Stop 2 s: USB drops");
        log_printf("power.sleep: entering Stop for 2 s (USB detaches); read with `run power.sleep report`\r\n");
        sleep_emit();
        log_flush(100);
        lp_rtc_start();
        t = time_us();
        lp_peripherals_sleep();
        R.p_sleep_us = time_us() - t;
        uint32_t cause = lp_stop(2, LP_WAKE_TIMER | LP_WAKE_OFFREQ);
        R.entry_us = lp_last_stop.entry_us;
        R.clk_cyc = lp_last_stop.clk_cyc;
        R.cause = cause;
        R.rtc_s = lp_rtc_seconds();
        uint32_t tw = HAL_GetTick();
        lp_peripherals_wake();
        R.p_wake_ms = HAL_GetTick() - tw;
        backlight_full();
        /* dev_state still says CONFIGURED after the soft disconnect (no bus event seen in Stop): restart the
           stack cleanly and time the host's re-enumeration */
        usb_stop();
        uint32_t tc = HAL_GetTick();
        usb_start();
        R.usb_how = 1;
        while (!usb_configured() && (uint32_t)(HAL_GetTick() - tc) < 6000u) {
        }
        if (!usb_configured()) R.usb_how = 2;
        R.usb_ms = HAL_GetTick() - tc;
        R.stop_done = true;
        HAL_Delay(300);
        lcd_fill_rect(0, 0, 160, 128, COLOR_BLACK);
        ui_con_begin("power.sleep");
    }
    sleep_emit();
    ui_con_line(COLOR_GREEN, "stop clk %u us", (unsigned)(R.clk_cyc / 16u));
    ui_con_line(COLOR_GREEN, "usb enum %u ms", (unsigned)R.usb_ms);
    ui_con_line(COLOR_GREEN, "WFI wake %u cyc", (unsigned)R.wfi_med);
    return (!do_stop || (R.cause & LP_WAKE_TIMER)) ? SUITE_PASS : SUITE_FAIL;
}

/* ================================================================ power.ampdip */

#define AD_N 36000u

static uint16_t *const ADB = (uint16_t *)(void *)g_arena;

static inline uint32_t ad_idx(void) { return AD_N - DMA2_Stream4->NDTR; }

static void busy_us(uint32_t us)
{
    uint32_t t0 = time_us();
    while ((uint32_t)(time_us() - t0) < us) {
    }
}

suite_verdict_t suite_power_ampdip(int argc, char **argv)
{
    uint32_t ms = suite_arg_u32(argc, argv, "ms", 200u);
    if (ms > 800u) ms = 800u;
    bool vbus = power_vbus();
    bool chg = power_charging();
    adc_background(false);
    adc_shutdown();
    if (!adc_measure(200)) {
        res_printf("{\"t\":\"error\",\"suite\":\"power.ampdip\",\"why\":\"adc\"}");
        return SUITE_FAIL;
    }
    const adc_result_t r0 = *adc_result();
    uint32_t mv_per_cnt_x1000 = r0.raw_bat ? (uint32_t)r0.vbat_mv * 1000u / r0.raw_bat : 1130u;
    adc_shutdown();

    audio_init();
    audio_pwm_start(1, true);          /* PA8 PWM 50 % = silence; amp still off */
    audio_set_master_db(0.0f);
    audio_voice_off(0); audio_voice_off(1); audio_voice_off(2); audio_voice_off(3);
    HAL_Delay(50);

    RCC->APB2ENR |= RCC_APB2ENR_ADC1EN;
    RCC->AHB1ENR |= RCC_AHB1ENR_DMA2EN;
    (void)RCC->AHB1ENR;
    gpio_config(BAT_SNS_PORT, BAT_SNS_PIN, GPIO_MODE_AN_, GPIO_PULL_NONE_, GPIO_SPEED_LOW_, 0);
    ADC->CCR = (3u << ADC_CCR_ADCPRE_Pos);                 /* PCLK2 / 8 */
    ADC1->CR2 = 0;
    ADC1->CR1 = 0;
    ADC1->SMPR2 = (7u << 3);                               /* IN1: 480 cycles */
    ADC1->SQR1 = 0;
    ADC1->SQR3 = 1u;
    DMA2_Stream4->CR = 0;
    while (DMA2_Stream4->CR & DMA_SxCR_EN) {
    }
    DMA2->HIFCR = 0x3Du;
    DMA2_Stream4->PAR = (uint32_t)&ADC1->DR;
    DMA2_Stream4->M0AR = (uint32_t)ADB;
    DMA2_Stream4->NDTR = AD_N;
    DMA2_Stream4->FCR = 0;
    DMA2_Stream4->CR = DMA_SxCR_MINC | (1u << DMA_SxCR_PSIZE_Pos) | (1u << DMA_SxCR_MSIZE_Pos) | (1u << DMA_SxCR_PL_Pos);
    DMA2_Stream4->CR |= DMA_SxCR_EN;
    ADC1->CR2 = ADC_CR2_ADON | ADC_CR2_DMA | ADC_CR2_DDS;
    busy_us(30);
    uint32_t t_start = time_us();
    ADC1->CR2 |= ADC_CR2_CONT | ADC_CR2_SWSTART;

    busy_us(120000);                                       /* the first ~45 ms read ~0.1 V (ADC/pin start-up), see note */
    uint32_t i_pwm = ad_idx();                             /* baseline ends here (PWM already on: no load) */
    busy_us(20000);
    audio_amp_on(0);                                       /* SD# high, PWM at 50 % */
    uint32_t i_amp = ad_idx();
    busy_us(ms * 1000u);
    audio_engine_start(AUD_ENG_B, 22050u, AUD_DITHER_OFF);
    audio_voice_tone(0, AUD_GEN_SINE, 1000.0f, -12.0f);
    uint32_t i_tone = ad_idx();
    busy_us(100000);
    audio_silence();
    audio_engine_stop();
    audio_amp_off(0);
    uint32_t i_off = ad_idx();
    busy_us(150000);
    uint32_t n = ad_idx();
    uint32_t t_end = time_us();
    ADC1->CR2 = 0;
    DMA2_Stream4->CR &= ~DMA_SxCR_EN;
    audio_shutdown();
    adc_shutdown();
    if (n < 1000u) {
        res_printf("{\"t\":\"error\",\"suite\":\"power.ampdip\",\"why\":\"no samples\"}");
        return SUITE_FAIL;
    }
    if (n > AD_N) n = AD_N;
    uint32_t per_x10 = (t_end - t_start) * 10u / n;        /* sample period in 0.1 us */
    uint32_t per_us = (per_x10 + 5u) / 10u;

    /* 8-sample boxcar -> smoothed mV, in place */
    uint32_t win[8] = {0};
    uint32_t sum = 0;
    for (uint32_t i = 0; i < n; i++) {
        uint32_t k = i & 7u;
        sum += ADB[i] - win[k];
        win[k] = ADB[i];
        uint32_t m = (i >= 7u) ? sum / 8u : ADB[i];
        ADB[i] = (uint16_t)((m * mv_per_cnt_x1000 + 500u) / 1000u);     /* mV, smoothed */
    }

    uint32_t base_n = 0, base_sum = 0;
    uint32_t b0 = (i_pwm > 700u) ? i_pwm - 700u : 8u;
    for (uint32_t i = b0; i < i_pwm; i++) { base_sum += ADB[i]; base_n++; }
    uint32_t base = base_n ? base_sum / base_n : 0u;

    struct { const char *nm; uint32_t a, b; } ph[4] = {
        {"pwm_amp_off", i_pwm, i_amp}, {"amp_on_idle", i_amp, i_tone}, {"tone_1k_-12dB", i_tone, i_off}, {"amp_off", i_off, n}};
    char out[400];
    int ol = 0;
    uint32_t gmin = 0xFFFFu;
    for (int p = 0; p < 4; p++) {
        uint32_t mn = 0xFFFFu, sm2 = 0, c = 0;
        for (uint32_t i = ph[p].a + 8u; i < ph[p].b; i++) {
            if (ADB[i] < mn) mn = ADB[i];
            sm2 += ADB[i];
            c++;
        }
        if (mn < gmin) gmin = mn;
        uint32_t mean = c ? sm2 / c : 0u;
        ol += snprintf(out + ol, sizeof(out) - (size_t)ol, "%s\"%s\":{\"min_mv\":%" PRIu32 ",\"mean_mv\":%" PRIu32
                       ",\"dip_mv\":%d}", p ? "," : "", ph[p].nm, mn, mean, (int)base - (int)mn);
    }
    /* settle times: after amp on (vs idle steady = mean of the last 30 ms of the idle phase), after amp off */
    uint32_t n30 = 30000u / (per_us ? per_us : 1u);
    uint32_t st_on = 0, st_off = 0;
    {
        uint32_t a = i_tone > n30 ? i_tone - n30 : i_amp, sm3 = 0, c = 0;
        for (uint32_t i = a; i < i_tone; i++) { sm3 += ADB[i]; c++; }
        uint32_t steady = c ? sm3 / c : 0u, last = i_amp;
        for (uint32_t i = i_amp + 8u; i < i_tone; i++) {
            if (ADB[i] + 12u < steady || ADB[i] > steady + 12u) last = i;
        }
        st_on = (last - i_amp) * per_us;
        a = n > n30 ? n - n30 : i_off; sm3 = 0; c = 0;
        for (uint32_t i = a; i < n; i++) { sm3 += ADB[i]; c++; }
        steady = c ? sm3 / c : 0u; last = i_off;
        for (uint32_t i = i_off + 8u; i < n; i++) {
            if (ADB[i] + 12u < steady || ADB[i] > steady + 12u) last = i;
        }
        st_off = (last - i_off) * per_us;
    }
    /* 5 ms bins (min) */
    uint16_t bins[120];
    uint32_t bs = 5000u / (per_us ? per_us : 1u), nb = 0;
    for (uint32_t i = 0; i + bs <= n && nb < 120u; i += bs) {
        uint32_t mn = 0xFFFFu;
        for (uint32_t k = i; k < i + bs; k++) if (ADB[k] < mn) mn = ADB[k];
        bins[nb++] = (uint16_t)mn;
    }
    res_printf("{\"t\":\"ampdip\",\"suite\":\"power.ampdip\",\"vbus\":%s,\"chg\":%s,\"vbat_before_mv\":%u,\"vdda_mv\":%u,"
               "\"samples\":%" PRIu32 ",\"sample_us\":%" PRIu32 ",\"base_mv\":%" PRIu32 ",\"min_mv\":%" PRIu32
               ",\"dip_mv\":%d,\"settle_after_on_us\":%" PRIu32 ",\"settle_after_off_us\":%" PRIu32
               ",\"events_ms\":{\"pwm\":%" PRIu32 ",\"amp\":%" PRIu32 ",\"tone\":%" PRIu32 ",\"off\":%" PRIu32 "},"
               "\"phases\":{%s}}",
               vbus ? "true" : "false", chg ? "true" : "false", (unsigned)r0.vbat_mv, (unsigned)r0.vdda_mv, n, per_us,
               base, gmin, (int)base - (int)gmin, st_on, st_off, i_pwm * per_us / 1000u, i_amp * per_us / 1000u,
               i_tone * per_us / 1000u, i_off * per_us / 1000u, out);
    jb_t j;
    jb_begin(&j, "ampdip_trace");
    jb_str(&j, "suite", "power.ampdip");
    jb_u32(&j, "bin_us", bs * per_us);
    jb_u16_array(&j, "min_mv", bins, nb);
    jb_end(&j);
    jb_emit(&j, "@R ");
    res_printf("{\"t\":\"ampdip_note\",\"suite\":\"power.ampdip\",\"note\":\"%s\"}",
               vbus ? "VBUS present: the battery is behind the BQ24075 power path, so the dip is damped/hidden by the "
                      "charger; repeat on battery only for the real F6 number"
                    : "battery only: dip = real battery + wiring sag");
    ui_con_line(COLOR_GREEN, "dip %d mV (base %u)", (int)base - (int)gmin, (unsigned)base);
    return SUITE_PASS;
}
