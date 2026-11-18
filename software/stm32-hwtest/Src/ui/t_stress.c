/* stress.game / soak: game-like concurrent load.

   Concurrent load: raycaster at the target fps with the native 8bpp DMA present (DMA2 S3, 48 MHz SPI1),
   audio engine B with 4 voices at 22.05 kHz (DMA2 S5, TIM1_UP; amp stays off unless amp=1), a 2 KB W25Q DMA
   read per frame (DMA1 S0 + S5, CRC-checked against values computed at the start), ADC batch every 250 ms
   (DMA2 S4) and the input scan. Counted: audio underruns / late blocks, DMA error flags (TEIF/DMEIF/FEIF)
   on every stream, missed frame deadlines, max frame time, W25Q CRC errors.

   Arena (72 KB): game pipeline 0..61951, W25Q stream buffer 61952 (2 KB), reference sector 64000 (4 KB),
   scratch 68096 (4 KB). W25Q write: only the pattern sector 0xFDB000 (once, if it differs). */

#include "suites.h"
#include "adc.h"
#include "audio.h"
#include "bench.h"
#include "cmd.h"
#include "crc.h"
#include "gamepipe.h"
#include "gfx.h"
#include "input.h"
#include "lcd.h"
#include "log.h"
#include "power.h"
#include "timebase.h"
#include "ui.h"
#include "w25q.h"
#include "stm32f4xx_hal.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#define STRM_BUF   (g_arena + 61952u)
#define STRM_REF   (g_arena + 64000u)
#define STRM_TMP   (g_arena + 68096u)
#define STRM_ADDR  0xFDB000u        /* last 4 KB sector of the block-C scratch range */
#define STRM_LEN   2048u
#define STRM_NOFF  9u

/* DMA flag masks (error flags only) */
#define D1_S0_ERR ((1u << 0) | (1u << 2) | (1u << 3))      /* LISR */
#define D1_S5_ERR ((1u << 6) | (1u << 8) | (1u << 9))      /* HISR */
#define D2_S4_ERR ((1u << 0) | (1u << 2) | (1u << 3))      /* HISR */

typedef struct {
    uint32_t frames, missed, max_us, sum_us;
    uint32_t w_max_us;                     /* window (10 s) max */
    uint32_t crc_err, crc_n, w25_to;       /* mismatching chunks, chunks checked, DMA timeouts */
    uint32_t d_err, d_fe;                  /* TEIF/DMEIF, FEIF counted by polling (W25Q + ADC) */
    uint32_t adc_n, adc_bad, vmin, vmax;
    uint32_t busy_cyc, total_cyc;
} gstat_t;

typedef struct {
    gstat_t s;
    uint32_t fps, period_us, amp;
    uint32_t t0_ms, t0_us, slot_us, seq, adc_seq;
    uint32_t crc_tab[STRM_NOFF];
    uint8_t back;
    bool w25_active;
    uint32_t w25_idx;
    uint32_t aud_t0_blocks;
    char l[4][24];
    uint32_t ov_ms;
} game_t;

static game_t G;

static uint32_t rng_next(uint32_t *s)
{
    uint32_t x = *s;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return *s = x;
}

static bool w25_prepare(game_t *g)
{
    uint32_t r = 0x1BADB002u;
    uint32_t *ref = (uint32_t *)(void *)STRM_REF;
    for (uint32_t i = 0; i < 1024u; i++) {
        ref[i] = rng_next(&r);
    }
    if (w25q_read(STRM_ADDR, STRM_TMP, 4096) != W25Q_OK) {
        return false;
    }
    if (memcmp(STRM_TMP, STRM_REF, 4096) != 0) {
        if (w25q_erase_cmd(0x20u, STRM_ADDR, NULL) != W25Q_OK) return false;
        for (uint32_t p = 0; p < 16u; p++) {
            if (w25q_program(STRM_ADDR + p * 256u, STRM_REF + p * 256u, 256) != W25Q_OK) return false;
        }
        if (w25q_read(STRM_ADDR, STRM_TMP, 4096) != W25Q_OK || memcmp(STRM_TMP, STRM_REF, 4096) != 0) {
            return false;
        }
    }
    for (uint32_t o = 0; o < STRM_NOFF; o++) {
        g->crc_tab[o] = crc_hw_words((const uint32_t *)(const void *)(STRM_REF + o * 256u), STRM_LEN / 4u);
    }
    return true;
}

static void w25_poll_start(game_t *g)
{
    g->w25_idx = g->s.frames % STRM_NOFF;
    memset(STRM_BUF, 0, 16);
    g->w25_active = w25q_dma_start(STRM_ADDR + g->w25_idx * 256u, STRM_BUF, STRM_LEN, true);
}

static void w25_poll_finish(game_t *g)
{
    if (!g->w25_active) {
        return;
    }
    uint32_t t0 = time_us();
    while (!w25q_dma_done() && (uint32_t)(time_us() - t0) < 50000u) {
    }
    bool ok = w25q_dma_done();
    uint32_t l = DMA1->LISR, h = DMA1->HISR;
    if ((l & ((1u << 2) | (1u << 3))) || (h & ((1u << 8) | (1u << 9)))) g->s.d_err++;
    if ((l & 1u) || (h & (1u << 6))) g->s.d_fe++;
    w25q_dma_finish();
    g->w25_active = false;
    if (!ok) {
        g->s.w25_to++;
        return;
    }
    g->s.crc_n++;
    if (crc_hw_words((const uint32_t *)(const void *)STRM_BUF, STRM_LEN / 4u) != g->crc_tab[g->w25_idx]) {
        g->s.crc_err++;
    }
}

static void adc_poll(game_t *g)
{
    adc_task();
    uint32_t h = DMA2->HISR;
    if (h & ((1u << 2) | (1u << 3))) {
        g->s.d_err++;
        DMA2->HIFCR = D2_S4_ERR;
    }
    if (h & 1u) {
        g->s.d_fe++;
        DMA2->HIFCR = 1u;
    }
    const adc_result_t *r = adc_result();
    if (r->seq != g->adc_seq) {
        g->adc_seq = r->seq;
        g->s.adc_n++;
        if (r->vbat_mv < 2500u || r->vbat_mv > 4500u || r->vdda_mv < 2900u || r->vdda_mv > 3600u) {
            g->s.adc_bad++;
        }
        if (r->vbat_mv < g->s.vmin) g->s.vmin = r->vbat_mv;
        if (r->vbat_mv > g->s.vmax) g->s.vmax = r->vbat_mv;
    }
}

static bool game_begin(game_t *g, uint32_t fps, uint32_t amp)
{
    memset(g, 0, sizeof(*g));
    g->s.vmin = 0xFFFFu;
    g->fps = fps;
    g->period_us = fps ? 1000000u / fps : 0u;
    g->amp = amp;
    if (!w25q_present() || !w25_prepare(g)) {
        res_printf("{\"t\":\"error\",\"suite\":\"stress.game\",\"why\":\"w25q pattern sector 0xFDB000 not usable\"}");
        return false;
    }
    gp_open();
    gp_game_enter();
    /* audio: engine B, 4 voices, 22.05 kHz, 375k/8b carrier. The DMA runs with the amp off, too. */
    audio_init();
    audio_pwm_start(1, true);
    audio_set_master_db(-12.0f);
    audio_engine_start(AUD_ENG_B, 22050u, AUD_DITHER_OFF);
    audio_voice_tone(0, AUD_GEN_SINE, 440.0f, -14.0f);
    audio_voice_tone(1, AUD_GEN_SINE, 660.0f, -16.0f);
    audio_voice_tone(2, AUD_GEN_SQUARE, 220.0f, -22.0f);
    audio_voice_noise(3, true, -24.0f);
    if (amp) {
        audio_amp_on(20);
    }
    adc_init();
    adc_background(true);
    input_flush();
    g->t0_ms = HAL_GetTick();
    g->t0_us = time_us();
    g->slot_us = g->t0_us;
    snprintf(g->l[0], sizeof(g->l[0]), "start");
    return true;
}

static void game_frame(game_t *g)
{
    uint32_t t_start = time_us();
    uint8_t *fb = gp_fb(g->back);
    w25_poll_start(g);
    gp_game_render(fb, HAL_GetTick() - g->t0_ms);
    if ((uint32_t)(HAL_GetTick() - g->ov_ms) >= 250u) {
        g->ov_ms = HAL_GetTick();
        uint32_t el = (HAL_GetTick() - g->t0_ms) / 1000u;
        const gstat_t *s = &g->s;
        snprintf(g->l[0], sizeof(g->l[0]), "%us %ufps", (unsigned)el, (unsigned)(el ? s->frames / el : 0u));
        snprintf(g->l[1], sizeof(g->l[1]), "UF%u LT%u", (unsigned)g_aud_stats.uf, (unsigned)g_aud_stats.late);
        snprintf(g->l[2], sizeof(g->l[2]), "DMA%u CRC%u", (unsigned)(g_aud_stats.dma_err + s->d_err + g_gp_dma_err),
                 (unsigned)s->crc_err);
        snprintf(g->l[3], sizeof(g->l[3]), "MISS%u mx%uus", (unsigned)s->missed, (unsigned)s->w_max_us);
    }
    int bw = 0;
    for (int i = 0; i < 4; i++) {
        int w = (int)strlen(g->l[i]) * 6;
        if (w > bw) bw = w;
    }
    gp_box(fb, 0, 0, bw + 4, 4 * 8 + 3, GP_IDX_BLACK);
    for (int i = 0; i < 4; i++) {
        gp_text(fb, 2, 2 + 8 * i, g->l[i], GP_IDX_WHITE, GP_IDX_BLACK);
    }
    w25_poll_finish(g);
    uint32_t tw0 = time_us();
    gp_wait();
    uint32_t wait_us = time_us() - tw0;
    gp_start(fb);
    g->back ^= 1u;
    adc_poll(g);
    (void)input_held();
    uint32_t t_work = time_us() - t_start;
    uint32_t c_work = t_work - wait_us;
    sys_service();
    g->s.frames++;
    g->s.sum_us += t_work;
    if (t_work > g->s.max_us) g->s.max_us = t_work;
    if (t_work > g->s.w_max_us) g->s.w_max_us = t_work;
    if (g->period_us) {
        uint32_t now = time_us();
        g->slot_us += g->period_us;
        if ((int32_t)(now - g->slot_us) > 0) {      /* the frame ended after its slot */
            g->s.missed++;
            g->slot_us = now;
        }
        while ((int32_t)(g->slot_us - time_us()) > 0) {
            sys_service();
        }
    }
    g->s.busy_cyc += c_work;
    g->s.total_cyc += time_us() - t_start;
}

static uint32_t pl_of(uint32_t cr) { return (cr >> DMA_SxCR_PL_Pos) & 3u; }

static void game_end(game_t *g)
{
    w25_poll_finish(g);
    gp_wait();
    if (g->amp) {
        audio_amp_off(20);
    }
    audio_shutdown();
    adc_background(false);
    adc_shutdown();
    gp_close();
}

static uint32_t g_dma_total_err(const game_t *g)
{
    return g_aud_stats.dma_err + g->s.d_err + g_gp_dma_err;
}

static uint32_t g_dma_total_fe(const game_t *g)
{
    return g_aud_stats.fe + g->s.d_fe + g_gp_dma_fe;
}

static void game_progress(const game_t *g, const char *suite, uint32_t el_s)
{
    res_printf("{\"t\":\"progress\",\"suite\":\"%s\",\"s\":%" PRIu32 ",\"frames\":%" PRIu32 ",\"max_us_10s\":%" PRIu32
               ",\"missed\":%" PRIu32 ",\"late\":%" PRIu32 ",\"uf\":%" PRIu32 ",\"dma_err\":%" PRIu32
               ",\"dma_fe\":%" PRIu32 ",\"crc_err\":%" PRIu32 ",\"adc_n\":%" PRIu32 ",\"vbat_mv\":%u}",
               suite, el_s, g->s.frames, g->s.w_max_us, g->s.missed, g_aud_stats.late, g_aud_stats.uf,
               g_dma_total_err(g), g_dma_total_fe(g), g->s.crc_err, g->s.adc_n,
               (unsigned)adc_result()->vbat_mv);
}

static bool game_pass(const game_t *g)
{
    return g_aud_stats.late == 0u && g_aud_stats.uf == 0u && g_dma_total_err(g) == 0u &&
           g->s.crc_err == 0u && g->s.w25_to == 0u && g->s.adc_bad == 0u;
}

static void game_summary(const game_t *g, const char *suite, uint32_t el_ms, bool pass)
{
    uint32_t fps100 = el_ms ? (uint32_t)((uint64_t)g->s.frames * 100000u / el_ms) : 0u;
    uint32_t exp_blk = (uint32_t)((uint64_t)el_ms * audio_fs_mhz() / 1000u / AUD_HALF / 1000u);
    uint32_t cpu100 = g->s.total_cyc ? (uint32_t)((uint64_t)g->s.busy_cyc * 10000u / g->s.total_cyc) : 0u;
    res_printf("{\"t\":\"stress\",\"suite\":\"%s\",\"name\":\"summary\",\"pass\":%s,\"s\":%" PRIu32 ",\"fps_target\":%" PRIu32
               ",\"frames\":%" PRIu32 ",\"fps_x100\":%" PRIu32 ",\"frame_us_avg\":%" PRIu32 ",\"frame_us_max\":%" PRIu32
               ",\"missed\":%" PRIu32 ",\"cpu_pct_x100\":%" PRIu32 ",\"audio_late\":%" PRIu32 ",\"audio_uf\":%" PRIu32
               ",\"audio_blocks\":%" PRIu32 ",\"audio_expected\":%" PRIu32 ",\"audio_body_cyc_max\":%" PRIu32
               ",\"dma_err\":%" PRIu32 ",\"fe_lcd\":%" PRIu32 ",\"fe_audio\":%" PRIu32 ",\"fe_other\":%" PRIu32 ",\"crc_err\":%" PRIu32 ",\"crc_n\":%" PRIu32
               ",\"w25q_to\":%" PRIu32 ",\"adc_n\":%" PRIu32 ",\"adc_bad\":%" PRIu32 ",\"vbat_min\":%" PRIu32
               ",\"vbat_max\":%" PRIu32 ",\"amp\":%" PRIu32 "}",
               suite, pass ? "true" : "false", el_ms / 1000u, g->fps, g->s.frames, fps100,
               g->s.frames ? g->s.sum_us / g->s.frames : 0u, g->s.max_us, g->s.missed, cpu100, g_aud_stats.late,
               g_aud_stats.uf, g_aud_stats.dma_ht + g_aud_stats.dma_tc, exp_blk, g_aud_stats.cyc_max,
               g_dma_total_err(g), g_gp_dma_fe, g_aud_stats.fe, g->s.d_fe, g->s.crc_err, g->s.crc_n, g->s.w25_to, g->s.adc_n,
               g->s.adc_bad, g->s.vmin == 0xFFFFu ? 0u : g->s.vmin, g->s.vmax, g->amp);
    res_printf("{\"t\":\"stress\",\"suite\":\"%s\",\"name\":\"dma_priorities\",\"lcd_s3_pl\":%" PRIu32
               ",\"audio_s5_pl\":%" PRIu32 ",\"adc_s4_pl\":%" PRIu32 ",\"w25q_rx_s0_pl\":%" PRIu32
               ",\"w25q_tx_s5_pl\":%" PRIu32 ",\"nvic\":{\"lcd_s3\":2,\"audio_s5\":5},\"w25q_hz\":%" PRIu32
               ",\"note\":\"PL 0 low,1 medium,2 high,3 very high; keep audio S5 very high (45 us sample period, no "
               "slack), LCD S3 high, W25Q RX medium/high, ADC low; same PL = lower stream wins\"}",
               suite, pl_of(DMA2_Stream3->CR), pl_of(DMA2_Stream5->CR), pl_of(DMA2_Stream4->CR),
               pl_of(DMA1_Stream0->CR), pl_of(DMA1_Stream5->CR), w25q_spi_hz());
}

static void game_window_reset(game_t *g) { g->s.w_max_us = 0; }

suite_verdict_t suite_stress_game(int argc, char **argv)
{
    uint32_t secs = suite_arg_u32(argc, argv, "s", 60u);
    uint32_t amp = suite_arg_u32(argc, argv, "amp", 0u) ? 1u : 0u;
    uint32_t fps = suite_arg_u32(argc, argv, "fps", 60u);
    game_t *g = &G;
    audio_stats_reset();
    g_gp_dma_err = g_gp_dma_fe = 0;
    if (!game_begin(g, fps, amp)) {
        return SUITE_FAIL;
    }
    ui_con_line(COLOR_GRAY, "running...");
    uint32_t next_prog = 10u;
    suite_verdict_t v = SUITE_PASS;
    uint32_t el_ms;
    while ((el_ms = HAL_GetTick() - g->t0_ms) < secs * 1000u) {
        game_frame(g);
        if (abort_requested()) {
            v = SUITE_ABORT;
            break;
        }
        input_event_t ev;
        while (input_pop(&ev)) {
            if (ev.pressed && ev.button == BTN_B) {
                abort_request();
            }
        }
        if (el_ms / 1000u >= next_prog) {
            game_progress(g, "stress.game", next_prog);
            game_window_reset(g);
            next_prog += 10u;
        }
    }
    el_ms = HAL_GetTick() - g->t0_ms;
    game_end(g);
    bool pass = game_pass(g);
    game_summary(g, "stress.game", el_ms, pass);
    ui_con_begin("stress.game");
    ui_con_line(pass ? COLOR_GREEN : COLOR_RED, "%s %us %ufps miss%u", pass ? "PASS" : "FAIL", (unsigned)(el_ms / 1000u),
                (unsigned)(g->s.frames * 1000u / (el_ms ? el_ms : 1u)), (unsigned)g->s.missed);
    ui_con_line(COLOR_WHITE, "audio late %u uf %u", (unsigned)g_aud_stats.late, (unsigned)g_aud_stats.uf);
    ui_con_line(COLOR_WHITE, "dma err %u fe %u", (unsigned)g_dma_total_err(g), (unsigned)g_dma_total_fe(g));
    ui_con_line(COLOR_WHITE, "crc err %u/%u", (unsigned)g->s.crc_err, (unsigned)g->s.crc_n);
    ui_con_line(COLOR_WHITE, "frame max %u us", (unsigned)g->s.max_us);
    if (v == SUITE_ABORT) {
        return v;
    }
    return pass ? SUITE_PASS : SUITE_FAIL;
}

suite_verdict_t suite_soak(int argc, char **argv)
{
    uint32_t mins = suite_arg_u32(argc, argv, "min", 60u);
    uint32_t amp = suite_arg_u32(argc, argv, "amp", 0u) ? 1u : 0u;
    uint32_t fps = suite_arg_u32(argc, argv, "fps", 60u);
    game_t *g = &G;
    audio_stats_reset();
    g_gp_dma_err = g_gp_dma_fe = 0;
    if (!game_begin(g, fps, amp)) {
        return SUITE_FAIL;
    }
    uint32_t next_min = 1u, next_prog = 10u;
    suite_verdict_t v = SUITE_PASS;
    uint32_t el_ms;
    while ((el_ms = HAL_GetTick() - g->t0_ms) < mins * 60000u) {
        game_frame(g);
        if (abort_requested()) {
            v = SUITE_ABORT;
            break;
        }
        input_event_t ev;
        while (input_pop(&ev)) {
            if (ev.pressed && ev.button == BTN_B) {
                abort_request();
            }
        }
        uint32_t el_s = el_ms / 1000u;
        if (el_s >= next_prog && el_s < next_min * 60u) {
            game_progress(g, "soak", next_prog);
            game_window_reset(g);
            next_prog += 10u;
        }
        if (el_s >= next_min * 60u) {
            const adc_result_t *r = adc_result();
            res_printf("{\"t\":\"soak\",\"suite\":\"soak\",\"min\":%" PRIu32 ",\"temp_dC\":%d,\"vbat_mv\":%u,\"vdda_mv\":%u"
                       ",\"frames\":%" PRIu32 ",\"missed\":%" PRIu32 ",\"max_us\":%" PRIu32 ",\"late\":%" PRIu32
                       ",\"uf\":%" PRIu32 ",\"dma_err\":%" PRIu32 ",\"dma_fe\":%" PRIu32 ",\"crc_err\":%" PRIu32
                       ",\"crc_n\":%" PRIu32 ",\"adc_bad\":%" PRIu32 ",\"vbus\":%d,\"ok\":%s}",
                       next_min, (int)r->temp_dC, (unsigned)r->vbat_mv, (unsigned)r->vdda_mv, g->s.frames, g->s.missed,
                       g->s.max_us, g_aud_stats.late, g_aud_stats.uf, g_dma_total_err(g), g_dma_total_fe(g),
                       g->s.crc_err, g->s.crc_n, g->s.adc_bad, power_vbus() ? 1 : 0, game_pass(g) ? "true" : "false");
            next_min++;
            next_prog = (el_s / 10u + 1u) * 10u;
            game_window_reset(g);
        }
    }
    el_ms = HAL_GetTick() - g->t0_ms;
    game_end(g);
    bool pass = game_pass(g);
    game_summary(g, "soak", el_ms, pass);
    ui_con_begin("soak");
    ui_con_line(pass ? COLOR_GREEN : COLOR_RED, "%s %u s", pass ? "PASS" : "FAIL", (unsigned)(el_ms / 1000u));
    ui_con_line(COLOR_WHITE, "audio late %u uf %u", (unsigned)g_aud_stats.late, (unsigned)g_aud_stats.uf);
    ui_con_line(COLOR_WHITE, "dma err %u fe %u crc %u", (unsigned)g_dma_total_err(g), (unsigned)g_dma_total_fe(g),
                (unsigned)g->s.crc_err);
    if (v == SUITE_ABORT) {
        return v;
    }
    return pass ? SUITE_PASS : SUITE_FAIL;
}
