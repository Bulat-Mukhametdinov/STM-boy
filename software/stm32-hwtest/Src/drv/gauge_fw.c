/* Firmware glue for the voltage-only battery gauge (gauge.c): feeds it from the ADC path
   every 250 ms while the main loop is idle (not inside a suite) and draws the status-bar icon.
   Load model: constant power, I = P / Vbat with P = i_state_ma * 3.95 V (the state currents
   were measured near 3.95 V). */

#include "gauge_fw.h"
#include "adc.h"
#include "backlight.h"
#include "gfx.h"
#include "hwcal.h"
#include "lcd.h"
#include "power.h"
#include "stm32f4xx_hal.h"

#include <inttypes.h>
#include <stdio.h>

#define P_REF_MV 3950u

static gauge_cfg_t s_cfg;
static gauge_t s_g;
static bool s_init;
static uint32_t s_seq, s_req_ms;
static uint8_t s_state_prev;
static uint16_t s_load_ma, s_vbat_mv;
static uint8_t s_load_state;

static uint8_t load_state(void)
{
    uint32_t ppm = backlight_ppm();
    if (ppm == 0u) return 2u;                       /* S2: WFI idle, backlight off */
    return (uint8_t)(3u + (ppm >= 1000000u ? 4u : (ppm * 5u) / 1000000u));   /* S3..S7: backlight 10..100 % */
}

static void init_now(void)
{
    gauge_cfg_defaults(&s_cfg);
    for (unsigned i = 0; i < GAUGE_OCV_POINTS; i++) s_cfg.ocv_mv[i] = g_hwcal.ocv_mv[i];
    if (g_hwcal.rint_mohm) s_cfg.rint_mohm = g_hwcal.rint_mohm;
    if (g_hwcal.i_chg_ma) s_cfg.i_chg_ma = g_hwcal.i_chg_ma;
    for (unsigned i = 0; i < GAUGE_N_STATES; i++) s_cfg.i_state_ma[i] = g_hwcal.i_state_ma[i];
    gauge_persist_t st;
    bool have = g_hwcal.gauge_seq != 0u;
    st.level = g_hwcal.gauge_level;
    st.mode = g_hwcal.gauge_mode;
    st.seq = g_hwcal.gauge_seq;
    gauge_init(&s_g, &s_cfg, have ? &st : NULL, HAL_GetTick());
    s_init = true;
}

void gauge_fw_poll(void)
{
    if (!s_init) init_now();
    uint32_t now = HAL_GetTick();
    const adc_result_t *a = adc_result();
    if (a->seq != 0u && a->seq != s_seq) {
        s_seq = a->seq;
        s_load_state = load_state();
        s_vbat_mv = a->vbat_mv;
        uint32_t ma = ((uint32_t)s_cfg.i_state_ma[s_load_state] * P_REF_MV) / (a->vbat_mv ? a->vbat_mv : P_REF_MV);
        s_load_ma = (uint16_t)ma;
        gauge_input_t in;
        in.now_ms = now;
        in.vbat_mv = a->vbat_mv;
        in.load_ma = s_load_ma;
        in.vbus = power_vbus();
        in.chg_low = power_charging();
        in.spread_mv = a->spread_mv;
        in.mode_changed = (s_load_state != s_state_prev);
        s_state_prev = s_load_state;
        (void)gauge_update(&s_g, &in);
    }
    if (!adc_busy() && (uint32_t)(now - s_req_ms) >= 250u) {
        s_req_ms = now;
        adc_request();
    }
}

uint32_t gauge_fw_sig(void)
{
    if (!s_init) return 0;
    gauge_display_t d;
    gauge_display(&s_g, &d);
    uint32_t ph = (HAL_GetTick() >> 9) & 1u;   /* 512 ms blink / animation phase */
    return 0x100u | d.level | (d.valid ? 0x10u : 0u) | ((d.blink ? ph : 0u) << 5) | (d.plug ? 0x40u : 0u) |
           ((d.charge_anim ? ph : 0u) << 7) | (d.crossed ? 0x80000u : 0u);
}

/* 5-segment battery with the plug marker, 30 x 8 px at (x, y). */
void gauge_fw_draw_icon(int16_t x, int16_t y, uint16_t bg)
{
    if (!s_init) return;
    gauge_display_t d;
    gauge_display(&s_g, &d);
    uint32_t ph = (HAL_GetTick() >> 9) & 1u;
    uint16_t outline = (d.blink && ph) ? COLOR_RED : COLOR_WHITE;
    if (!d.valid) outline = COLOR_GRAY;
    if (d.plug) {   /* plug: two prongs + body */
        lcd_fill_rect(x, (int16_t)(y + 1), 1, 2, COLOR_YELLOW);
        lcd_fill_rect((int16_t)(x + 3), (int16_t)(y + 1), 1, 2, COLOR_YELLOW);
        lcd_fill_rect((int16_t)(x - 1), (int16_t)(y + 3), 6, 3, COLOR_YELLOW);
        lcd_fill_rect((int16_t)(x + 1), (int16_t)(y + 6), 2, 2, COLOR_YELLOW);
    } else {
        lcd_fill_rect((int16_t)(x - 1), y, 7, 8, bg);
    }
    int16_t bx = (int16_t)(x + 7);
    lcd_fill_rect(bx, y, 23, 8, outline);
    lcd_fill_rect((int16_t)(bx + 23), (int16_t)(y + 2), 2, 4, outline);
    lcd_fill_rect((int16_t)(bx + 1), (int16_t)(y + 1), 21, 6, bg);
    uint16_t col = d.level <= 1u ? COLOR_RED : d.level == 2u ? COLOR_YELLOW : COLOR_GREEN;
    for (uint8_t i = 0; i < GAUGE_N_SEGMENTS; i++) {
        bool on = d.valid && i < d.level;
        if (d.charge_anim && i == d.level && ph) on = true;
        if (on) lcd_fill_rect((int16_t)(bx + 2 + i * 4), (int16_t)(y + 2), 3, 4, col);
    }
    if (d.crossed) {
        lcd_fill_rect((int16_t)(bx + 2), (int16_t)(y + 3), 19, 2, COLOR_RED);
    }
}

void gauge_fw_json(char *buf, size_t n)
{
    if (!s_init) init_now();
    const gauge_stats_t *st = gauge_stats(&s_g);
    gauge_display_t d;
    gauge_display(&s_g, &d);
    gauge_mode_t m = gauge_mode(&s_g);
    uint32_t soc10 = (uint32_t)(gauge_soc(&s_g) * 10.0f + 0.5f);
    uint32_t vc = (uint32_t)(gauge_vcomp(&s_g) + 0.5f);
    snprintf(buf, n,
             "{\"t\":\"gauge\",\"mode\":\"%s\",\"level\":%u,\"valid\":%s,\"flags\":%u,\"soc\":%" PRIu32 ".%" PRIu32
             ",\"vcomp_mv\":%" PRIu32 ",\"vbat_mv\":%u,\"load_ma\":%u,\"load_state\":%u,\"vbus\":%s,\"chg\":%s,"
             "\"drops\":%" PRIu32 ",\"rises\":%" PRIu32 ",\"escapes\":%" PRIu32 ",\"reversals\":%" PRIu32 "}",
             gauge_mode_name(m), (unsigned)gauge_level(&s_g), d.valid ? "true" : "false", (unsigned)gauge_flags(&s_g),
             soc10 / 10u, soc10 % 10u, vc, (unsigned)s_vbat_mv, (unsigned)s_load_ma, (unsigned)s_load_state,
             power_vbus() ? "true" : "false", power_charging() ? "true" : "false", st->drops, st->rises, st->escapes,
             st->reversals[0] + st->reversals[1] + st->reversals[2] + st->reversals[3] + st->reversals[4] +
                 st->reversals[5]);
}
