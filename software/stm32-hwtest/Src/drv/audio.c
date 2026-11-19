/* P4 audio test driver. See Inc/audio.h. Register-level, no HAL timer/DMA code. */

#include "audio.h"
#include "board.h"
#include "clock.h"
#include "cmd.h"
#include "timebase.h"
#include "stm32f4xx.h"

#include <math.h>
#include <string.h>

const aud_carrier_t g_aud_carriers[AUD_NCARR] = {
    {127u, 7, 750000u, "750k/7b"},
    {255u, 8, 375000u, "375k/8b"},
    {511u, 9, 187500u, "187k/9b"},
    {1023u, 10, 93750u, "94k/10b"},
};

aud_stats_t g_aud_stats;

/* ------------------------------------------------------------------ state */

typedef struct {
    volatile uint8_t type;
    uint32_t phase, step;
    int32_t gain;               /* Q15 */
    float f_hz, f1_hz, dur_s;   /* tone / chirp parameters (kept for step recalculation) */
    float stepf, ratio, stepf_end;
    float p0, p1, p2;           /* pink filter state */
    uint32_t rng;
} voice_t;

static voice_t s_v[AUD_NVOICE];
static int16_t s_sin[1024];
static bool s_inited;

static volatile int32_t s_gtgt;          /* master target, Q23 */
static int32_t s_gcur;                   /* master current, Q23 */
static float s_master_db = -24.0f;
static uint32_t s_gsh = 6;               /* smoothing shift */

static float s_max_db = -6.0f;
static int32_t s_lim = 16422;            /* 32767 * 10^(-6/20) */

static unsigned s_carrier = 1;
static aud_engine_t s_engine = AUD_ENG_NONE;
static uint32_t s_rcr_div = 1;
static uint32_t s_fs_mhz = 46875000u;
static float s_fs = 46875.0f;
static bool s_amp;
static bool s_pwm_on;
static uint32_t s_req_fs;                 /* requested Fs of the running engine (re-derived on clock change) */

/* quantiser */
static struct {
    uint8_t sh;
    int32_t half;           /* 2^(bits-1) */
    int32_t err;
    uint32_t rng;
    volatile uint8_t mode;
} q;

extern uint8_t g_arena[];   /* main.c scratch: free while an audio suite runs (suites are exclusive) */
static uint16_t *s_bufp = (uint16_t *)(void *)g_arena;   /* engine B DMA ring: 2 * AUD_HALF * 2 B */
#define s_buf s_bufp
void audio_set_buffer(void *p) { s_bufp = p ? (uint16_t *)p : (uint16_t *)(void *)g_arena; }

/* PCM stream voice (media.play): unsigned 8-bit mono ring filled by the main loop, linear interpolation to the engine
   rate. head/tail are monotonic byte counters; an empty ring plays silence and counts an underrun. */
static struct {
    const uint8_t *buf;
    uint32_t mask, rate, step;      /* step: Q16 PCM samples per output sample */
    volatile uint32_t head, tail, under;
    uint32_t phase;
    int32_t prev, cur;
} s_pcm;

#define PINK_SCALE 10890.0f

/* ------------------------------------------------------------------ small helpers */

static inline uint32_t xs32(uint32_t *s)
{
    uint32_t x = *s;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *s = x;
    return x;
}

static uint32_t tim1_clk(void)
{
    uint32_t pclk2 = clock_pclk2_hz();
    return (((RCC->CFGR >> RCC_CFGR_PPRE2_Pos) & 4u) ? 2u : 1u) * pclk2;
}

static inline int32_t sine_at(uint32_t phase)
{
    uint32_t i = phase >> 22;
    int32_t a = s_sin[i], b = s_sin[(i + 1u) & 1023u];
    return a + (((b - a) * (int32_t)((phase >> 6) & 0xFFFFu)) >> 16);
}

static inline int32_t voice_sample(voice_t *v)
{
    int32_t s;
    switch (v->type) {
    case AUD_GEN_SINE:
        s = sine_at(v->phase);
        v->phase += v->step;
        break;
    case AUD_GEN_SQUARE:
        s = (v->phase & 0x80000000u) ? -32767 : 32767;
        v->phase += v->step;
        break;
    case AUD_GEN_WHITE:
        s = (int32_t)(int16_t)(xs32(&v->rng) >> 16);
        break;
    case AUD_GEN_PINK: {
        float w = (float)(int16_t)(xs32(&v->rng) >> 16) * (1.0f / 32768.0f);
        v->p0 = 0.99765f * v->p0 + w * 0.0990460f;
        v->p1 = 0.96300f * v->p1 + w * 0.2965164f;
        v->p2 = 0.57000f * v->p2 + w * 1.0526913f;
        s = (int32_t)((v->p0 + v->p1 + v->p2 + w * 0.1848f) * PINK_SCALE);
        break;
    }
    case AUD_GEN_CHIRP:
        s = sine_at(v->phase);
        v->phase += v->step;
        v->stepf *= v->ratio;
        if (v->stepf > v->stepf_end) {
            v->stepf = v->stepf_end;
        }
        v->step = (uint32_t)v->stepf;
        break;
    case AUD_GEN_PCM: {
        uint32_t ph = s_pcm.phase + s_pcm.step;
        if (ph >= 65536u) {
            ph -= 65536u;
            s_pcm.prev = s_pcm.cur;
            if (s_pcm.tail != s_pcm.head) {
                s_pcm.cur = ((int32_t)s_pcm.buf[s_pcm.tail & s_pcm.mask] - 128) << 8;
                s_pcm.tail++;
            } else {
                s_pcm.cur = 0;
                s_pcm.under++;
            }
        }
        s_pcm.phase = ph;
        s = s_pcm.prev + (((s_pcm.cur - s_pcm.prev) * (int32_t)ph) >> 16);
        break;
    }
    default:
        return 0;
    }
    return (int32_t)(((int64_t)s * v->gain) >> 15);
}

static inline uint32_t render_sample(void)
{
    int32_t acc = 0;
    for (unsigned i = 0; i < AUD_NVOICE; i++) {
        if (s_v[i].type) {
            acc += voice_sample(&s_v[i]);
        }
    }
    int32_t gt = s_gtgt;
    s_gcur += (gt - s_gcur) >> s_gsh;
    int32_t x = (int32_t)(((int64_t)acc * (s_gcur >> 8)) >> 15);
    int32_t lim = s_lim;
    if (x > lim) {
        x = lim;
        g_aud_stats.clipped++;
    } else if (x < -lim) {
        x = -lim;
        g_aud_stats.clipped++;
    }
    /* quantise to the carrier resolution */
    uint32_t sh = q.sh;
    int32_t lsb = 1 << sh;
    uint32_t mode = q.mode;
    int32_t y = x, d = 0;
    if (mode) {
        uint32_t r = xs32(&q.rng);
        d = (int32_t)(r & (uint32_t)(lsb - 1)) + (int32_t)((r >> 16) & (uint32_t)(lsb - 1)) - (lsb - 1);
        if (mode == AUD_DITHER_SHAPED) {
            y -= q.err;
        }
    }
    int32_t t = y + d;
    int32_t k = (t + (lsb >> 1)) >> sh;
    if (k > q.half - 1) {
        k = q.half - 1;
        q.err = 0;
    } else if (k < -q.half) {
        k = -q.half;
        q.err = 0;
    } else {
        q.err = (k << sh) - t;
    }
    return (uint32_t)(k + q.half);
}

static inline void stat_cyc(uint32_t c)
{
    aud_stats_t *s = &g_aud_stats;
    s->cyc_sum += c;
    s->calls++;
    if (c > s->cyc_max) s->cyc_max = c;
    if (c < s->cyc_min) s->cyc_min = c;
}

static void fill_block(uint16_t *dst, uint32_t n)
{
    uint32_t c0 = DWT->CYCCNT;
    for (uint32_t i = 0; i < n; i++) {
        dst[i] = (uint16_t)render_sample();
    }
    stat_cyc(DWT->CYCCNT - c0);
}

/* ------------------------------------------------------------------ ISRs */

void TIM5_IRQHandler(void);
void DMA2_Stream5_IRQHandler(void);

void bench_tim5_wake_isr(void);

/* TIM5 is shared with the bench.sys wake-latency test (CC1 compare): dispatch when no engine runs. */
void TIM5_IRQHandler(void)
{
    if (s_engine != AUD_ENG_A) {
        bench_tim5_wake_isr();
        return;
    }
    uint32_t c0 = DWT->CYCCNT;
    g_aud_stats.last_us = TIM2->CNT;
    TIM5->SR = ~TIM_SR_UIF;
    TIM1->CCR1 = render_sample();
    g_aud_stats.isr_count++;
    stat_cyc(DWT->CYCCNT - c0);
    if (TIM5->SR & TIM_SR_UIF) {
        g_aud_stats.late++;
    }
}

#define S5_HT (1u << 10)
#define S5_TC (1u << 11)
#define S5_TE (1u << 9)
#define S5_DME (1u << 8)
#define S5_FE (1u << 6)
#define S5_ALL (S5_HT | S5_TC | S5_TE | S5_DME | S5_FE)

void DMA2_Stream5_IRQHandler(void)
{
    uint32_t f = DMA2->HISR;
    g_aud_stats.last_us = TIM2->CNT;
    DMA2->HIFCR = f & S5_ALL;
    if ((f & S5_HT) && (f & S5_TC)) {
        g_aud_stats.late++;
    }
    if (f & (S5_TE | S5_DME)) {
        g_aud_stats.dma_err++;
    }
    if (f & S5_FE) {
        g_aud_stats.fe++;
    }
    if (f & S5_HT) {
        g_aud_stats.dma_ht++;
        fill_block(&s_buf[0], AUD_HALF);
        if (DMA2_Stream5->NDTR > AUD_HALF) {   /* HT: DMA should be in the 2nd half (NDTR <= HALF) */
            g_aud_stats.uf++;
        }
    }
    if (f & S5_TC) {
        g_aud_stats.dma_tc++;
        fill_block(&s_buf[AUD_HALF], AUD_HALF);
        if (DMA2_Stream5->NDTR <= AUD_HALF) {  /* TC: DMA should be in the 1st half (NDTR > HALF) */
            g_aud_stats.uf++;
        }
    }
}

/* ------------------------------------------------------------------ init */

void audio_init(void)
{
    if (s_inited) {
        return;
    }
    for (unsigned i = 0; i < 1024u; i++) {
        s_sin[i] = (int16_t)lrintf(sinf((float)i * (6.2831853f / 1024.0f)) * 32767.0f);
    }
    memset(s_v, 0, sizeof(s_v));
    for (unsigned i = 0; i < AUD_NVOICE; i++) {
        s_v[i].rng = 0x9E3779B9u + 0x1234567u * i;
        s_v[i].gain = 32768;
    }
    q.rng = 0xC0FFEE11u;
    s_inited = true;
    audio_set_master_db(-24.0f);
    audio_set_max_db(-6.0f);
    s_gcur = 0;
    RCC->AHB1ENR |= RCC_AHB1ENR_DMA2EN;
    gpio_clr(AMP_SD_PORT, AMP_SD_PIN);
    gpio_config(AMP_SD_PORT, AMP_SD_PIN, GPIO_MODE_OUT_, GPIO_PULL_NONE_, GPIO_SPEED_LOW_, 0);
    s_amp = false;
}

void audio_stats_reset(void)
{
    memset((void *)&g_aud_stats, 0, sizeof(g_aud_stats));
    g_aud_stats.cyc_min = 0xFFFFFFFFu;
}

static void wait_force(uint32_t ms)
{
    uint32_t t0 = time_us(), w = ms * 1000u;
    while ((time_us() - t0) < w) {
        sys_service();
    }
}

bool audio_wait_ms(uint32_t ms)
{
    uint32_t t0 = time_us(), w = ms * 1000u;
    while ((time_us() - t0) < w) {
        sys_service();
        if (abort_requested()) {
            return false;
        }
    }
    return true;
}

/* ------------------------------------------------------------------ carrier / pins */

unsigned audio_carrier(void) { return s_carrier; }
uint16_t audio_mid(void) { return (uint16_t)((g_aud_carriers[s_carrier].arr + 1u) >> 1); }
aud_engine_t audio_engine(void) { return s_engine; }
uint32_t audio_fs_mhz(void) { return s_fs_mhz; }
uint32_t audio_rcr_div(void) { return s_rcr_div; }
bool audio_amp_is_on(void) { return s_amp; }

void audio_pwm_start(unsigned carrier, bool at_mid)
{
    audio_init();
    audio_engine_stop();
    if (carrier >= AUD_NCARR) {
        carrier = 1;
    }
    s_carrier = carrier;
    q.sh = (uint8_t)(16u - g_aud_carriers[carrier].bits);
    q.half = 1 << (g_aud_carriers[carrier].bits - 1u);
    RCC->APB2ENR |= RCC_APB2ENR_TIM1EN;
    (void)RCC->APB2ENR;
    TIM1->CR1 = 0;
    TIM1->PSC = 0;
    TIM1->ARR = g_aud_carriers[carrier].arr;
    TIM1->RCR = 0;
    TIM1->CCMR1 = TIM_CCMR1_OC1M_2 | TIM_CCMR1_OC1M_1 | TIM_CCMR1_OC1PE;
    TIM1->CCR1 = at_mid ? audio_mid() : 0u;
    TIM1->CCER = TIM_CCER_CC1E;
    TIM1->BDTR = TIM_BDTR_MOE;
    TIM1->CNT = 0;
    TIM1->EGR = TIM_EGR_UG;
    TIM1->SR = 0;
    gpio_config(AUDIO_PWM_PORT, AUDIO_PWM_PIN, GPIO_MODE_AF_, GPIO_PULL_NONE_, GPIO_SPEED_VHIGH_, 1);
    TIM1->CR1 = TIM_CR1_ARPE | TIM_CR1_CEN;
    s_pwm_on = true;
}

void audio_pwm_stop(void)
{
    audio_engine_stop();
    TIM1->CR1 = 0;
    TIM1->CCER = 0;
    TIM1->BDTR = 0;
    gpio_config(AUDIO_PWM_PORT, AUDIO_PWM_PIN, GPIO_MODE_IN_, GPIO_PULL_NONE_, GPIO_SPEED_LOW_, 0);
    RCC->APB2ENR &= ~RCC_APB2ENR_TIM1EN;
    s_pwm_on = false;
}

void audio_pa8_mode(aud_pa8_t m)
{
    audio_init();
    audio_pwm_stop();
    if (m == AUD_PA8_HIZ) {
        return;
    }
    if (m == AUD_PA8_HIGH) {
        gpio_set(AUDIO_PWM_PORT, AUDIO_PWM_PIN);
    } else {
        gpio_clr(AUDIO_PWM_PORT, AUDIO_PWM_PIN);
    }
    gpio_config(AUDIO_PWM_PORT, AUDIO_PWM_PIN, GPIO_MODE_OUT_, GPIO_PULL_NONE_, GPIO_SPEED_LOW_, 0);
}

void audio_amp_raw(bool on)
{
    audio_init();
    if (on) {
        gpio_set(AMP_SD_PORT, AMP_SD_PIN);
    } else {
        gpio_clr(AMP_SD_PORT, AMP_SD_PIN);
    }
    s_amp = on;
}

bool audio_amp_on(uint32_t ramp_ms)
{
    if (ramp_ms == 0u) {
        /* Pop-free recipe (user listening test 2026-11-19): bring PWM to mid-scale with the amp
           OFF, let the 10k x 1uF input coupling settle (tau 10 ms), then enable the amp; its own
           soft-start handles the rest. Ramping PWM with the amp already on was audible (thump). */
        if (s_pwm_on && TIM1->CCR1 != audio_mid()) {
            TIM1->CCR1 = audio_mid();
            wait_force(30);
        }
        audio_amp_raw(true);
        return true;
    }
    if (s_pwm_on) TIM1->CCR1 = 0;
    audio_amp_raw(true);
    uint32_t mid = audio_mid();
    for (uint32_t i = 1; i <= ramp_ms; i++) {
        if (s_pwm_on) TIM1->CCR1 = (mid * i + ramp_ms / 2u) / ramp_ms;
        wait_force(1);
    }
    return true;
}

bool audio_amp_off(uint32_t ramp_ms)
{
    bool ok = true;
    if (ramp_ms != 0u && s_pwm_on) {
        uint32_t mid = audio_mid();
        for (uint32_t i = ramp_ms; i-- > 0;) {
            TIM1->CCR1 = (mid * i + ramp_ms / 2u) / ramp_ms;
            wait_force(1);
        }
        TIM1->CCR1 = 0;
    }
    audio_amp_raw(false);
    return ok;
}

void audio_shutdown(void)
{
    audio_engine_stop();
    audio_silence();
    audio_amp_raw(false);
    audio_pwm_stop();
    s_gcur = 0;
}

/* ------------------------------------------------------------------ engines */

static void recalc_voices(void)
{
    for (unsigned i = 0; i < AUD_NVOICE; i++) {
        voice_t *v = &s_v[i];
        uint8_t t = v->type;
        if (t == AUD_GEN_PCM) {
            s_pcm.step = (uint32_t)((float)s_pcm.rate / s_fs * 65536.0f);
        } else if (t == AUD_GEN_SINE || t == AUD_GEN_SQUARE) {
            v->step = (uint32_t)(v->f_hz / s_fs * 4294967296.0f);
        } else if (t == AUD_GEN_CHIRP) {
            float n = v->dur_s * s_fs;
            v->stepf = v->f_hz / s_fs * 4294967296.0f;
            v->stepf_end = v->f1_hz / s_fs * 4294967296.0f;
            v->ratio = powf(v->f1_hz / v->f_hz, 1.0f / n);
            v->step = (uint32_t)v->stepf;
        }
    }
}

void audio_dither_set(aud_dither_t d)
{
    q.err = 0;
    q.mode = (uint8_t)d;
}

bool audio_engine_start(aud_engine_t e, uint32_t fs_hz, aud_dither_t d)
{
    if (!s_pwm_on || fs_hz < 1000u || fs_hz > 200000u) {
        return false;
    }
    audio_engine_stop();
    q.err = 0;
    q.mode = (uint8_t)d;
    uint32_t car = tim1_clk() / (g_aud_carriers[s_carrier].arr + 1u);   /* actual carrier at the current clock */
    s_req_fs = fs_hz;
    if (e == AUD_ENG_A) {
        uint32_t clk = clock_tim_apb1_hz();
        uint32_t div = (clk + fs_hz / 2u) / fs_hz;
        s_fs_mhz = (uint32_t)(((uint64_t)clk * 1000u) / div);
        s_rcr_div = 1;
    } else {
        uint32_t n = (car + fs_hz / 2u) / fs_hz;
        if (n < 1u) n = 1u;
        if (n > 256u) n = 256u;
        s_rcr_div = n;
        s_fs_mhz = (uint32_t)(((uint64_t)tim1_clk() * 1000u) / ((g_aud_carriers[s_carrier].arr + 1u) * n));
    }
    s_fs = (float)s_fs_mhz * 0.001f;
    s_gsh = (s_fs > 30000.0f) ? 6u : (s_fs > 16000.0f) ? 5u : 4u;
    recalc_voices();
    audio_stats_reset();

    if (e == AUD_ENG_A) {
        uint32_t clk = clock_tim_apb1_hz();
        RCC->APB1ENR |= RCC_APB1ENR_TIM5EN;
        (void)RCC->APB1ENR;
        TIM5->CR1 = 0;
        TIM5->PSC = 0;
        TIM5->ARR = (clk + fs_hz / 2u) / fs_hz - 1u;
        TIM5->CNT = 0;
        TIM5->EGR = TIM_EGR_UG;
        TIM5->SR = 0;
        TIM5->DIER = TIM_DIER_UIE;
        NVIC_SetPriority(TIM5_IRQn, 3);
        NVIC_EnableIRQ(TIM5_IRQn);
        s_engine = AUD_ENG_A;
        TIM5->CR1 = TIM_CR1_CEN;
        return true;
    }

    /* engine B: prefill both halves, DMA circular -> CCR1, TIM1 update request */
    fill_block(&s_buf[0], AUD_HALF);
    fill_block(&s_buf[AUD_HALF], AUD_HALF);
    audio_stats_reset();
    DMA2_Stream5->CR = 0;
    while (DMA2_Stream5->CR & DMA_SxCR_EN) {
    }
    DMA2->HIFCR = S5_ALL;
    DMA2_Stream5->PAR = (uint32_t)&TIM1->CCR1;
    DMA2_Stream5->M0AR = (uint32_t)s_buf;
    DMA2_Stream5->NDTR = 2u * AUD_HALF;
    DMA2_Stream5->FCR = 0;
    DMA2_Stream5->CR = (6u << DMA_SxCR_CHSEL_Pos) | (3u << DMA_SxCR_PL_Pos) | DMA_SxCR_MSIZE_0 |
                       DMA_SxCR_PSIZE_0 | DMA_SxCR_MINC | DMA_SxCR_CIRC | (1u << DMA_SxCR_DIR_Pos) |
                       DMA_SxCR_HTIE | DMA_SxCR_TCIE | DMA_SxCR_TEIE | DMA_SxCR_DMEIE;
    NVIC_SetPriority(DMA2_Stream5_IRQn, 5);
    NVIC_EnableIRQ(DMA2_Stream5_IRQn);
    s_engine = AUD_ENG_B;
    DMA2_Stream5->CR |= DMA_SxCR_EN;
    TIM1->CR1 &= ~TIM_CR1_CEN;
    TIM1->RCR = (s_rcr_div - 1u);
    TIM1->CNT = 0;
    TIM1->EGR = TIM_EGR_UG;
    TIM1->SR = 0;
    TIM1->DIER = TIM_DIER_UDE;
    TIM1->CR1 |= TIM_CR1_CEN;
    return true;
}

void audio_engine_stop(void)
{
    if (s_engine == AUD_ENG_A) {
        TIM5->CR1 = 0;
        TIM5->DIER = 0;
        NVIC_DisableIRQ(TIM5_IRQn);
        TIM5->SR = 0;
    } else if (s_engine == AUD_ENG_B) {
        TIM1->DIER = 0;
        DMA2_Stream5->CR = 0;
        uint32_t n = 0;
        while ((DMA2_Stream5->CR & DMA_SxCR_EN) && ++n < 100000u) {
        }
        NVIC_DisableIRQ(DMA2_Stream5_IRQn);
        DMA2->HIFCR = S5_ALL;
        TIM1->CR1 &= ~TIM_CR1_CEN;
        TIM1->RCR = 0;
        TIM1->EGR = TIM_EGR_UG;
        TIM1->SR = 0;
        TIM1->CR1 |= TIM_CR1_CEN;
    }
    if (s_engine != AUD_ENG_NONE && s_pwm_on) {
        TIM1->CCR1 = audio_mid();
    }
    s_engine = AUD_ENG_NONE;
}

/* ------------------------------------------------------------------ mixer control */

static int32_t db_to_q15(float db)
{
    float lin = powf(10.0f, db * 0.05f);
    if (lin > 1.0f) lin = 1.0f;
    return (int32_t)(lin * 32768.0f + 0.5f);
}

void audio_set_master_db(float db)
{
    if (db > 0.0f) db = 0.0f;
    s_master_db = db;
    s_gtgt = db_to_q15(db) << 8;
}

float audio_master_db(void) { return s_master_db; }

void audio_set_max_db(float db)
{
    if (db > 0.0f) db = 0.0f;
    s_max_db = db;
    s_lim = (int32_t)(32767.0f * powf(10.0f, db * 0.05f));
}

float audio_max_db(void) { return s_max_db; }

void audio_voice_off(unsigned v)
{
    if (v < AUD_NVOICE) s_v[v].type = AUD_GEN_OFF;
}

void audio_silence(void)
{
    for (unsigned i = 0; i < AUD_NVOICE; i++) s_v[i].type = AUD_GEN_OFF;
}

void audio_voice_tone(unsigned v, aud_gen_t type, float f_hz, float db)
{
    if (v >= AUD_NVOICE) return;
    voice_t *p = &s_v[v];
    p->type = AUD_GEN_OFF;
    p->f_hz = f_hz;
    p->gain = db_to_q15(db);
    p->phase = 0;
    p->step = (uint32_t)(f_hz / s_fs * 4294967296.0f);
    p->type = (uint8_t)type;
}

void audio_voice_noise(unsigned v, bool pink, float rms_db)
{
    if (v >= AUD_NVOICE) return;
    voice_t *p = &s_v[v];
    p->type = AUD_GEN_OFF;
    /* both generators have natural RMS = 0.5774 FS (-4.77 dB) */
    float db = rms_db + 4.77f;
    p->gain = db_to_q15(db);
    p->p0 = p->p1 = p->p2 = 0.0f;
    p->type = pink ? AUD_GEN_PINK : AUD_GEN_WHITE;
}

void audio_voice_chirp(unsigned v, float f0, float f1, float dur_s, float db)
{
    if (v >= AUD_NVOICE) return;
    voice_t *p = &s_v[v];
    p->type = AUD_GEN_OFF;
    p->f_hz = f0;
    p->f1_hz = f1;
    p->dur_s = dur_s;
    p->gain = db_to_q15(db);
    p->phase = 0;
    float n = dur_s * s_fs;
    p->stepf = f0 / s_fs * 4294967296.0f;
    p->stepf_end = f1 / s_fs * 4294967296.0f;
    p->ratio = powf(f1 / f0, 1.0f / n);
    p->step = (uint32_t)p->stepf;
    p->type = AUD_GEN_CHIRP;
}

/* SYSCLK/APB changed: the carrier (TIM1 ARR is in ticks) scales with the clock, but the sample
   rate must stay nominal, so a running engine is restarted and its divider re-derived. */
void audio_clock_changed(void)
{
    aud_engine_t e = s_engine;
    if (e == AUD_ENG_NONE) {
        return;
    }
    uint32_t fs = s_req_fs;
    aud_dither_t d = (aud_dither_t)q.mode;
    audio_engine_stop();
    (void)audio_engine_start(e, fs, d);
}

/* ------------------------------------------------------------------ PCM stream (media.play) */

/* ring: power-of-two size, rate_hz = PCM sample rate. Call before audio_engine_start(). */
void audio_pcm_start(uint8_t *ring, uint32_t size, uint32_t rate_hz, float db)
{
    audio_init();
    s_v[0].type = AUD_GEN_OFF;
    s_pcm.buf = ring;
    s_pcm.mask = size - 1u;
    s_pcm.rate = rate_hz;
    s_pcm.head = s_pcm.tail = s_pcm.under = 0;
    s_pcm.phase = 65535u;       /* first output sample pulls byte 0 */
    s_pcm.prev = s_pcm.cur = 0;
    s_pcm.step = (uint32_t)((float)rate_hz / s_fs * 65536.0f);
    s_v[0].gain = db_to_q15(db);
    s_v[0].type = AUD_GEN_PCM;
}

uint32_t audio_pcm_free(void) { return s_pcm.mask + 1u - (s_pcm.head - s_pcm.tail); }
uint32_t audio_pcm_under(void) { return s_pcm.under; }

void audio_pcm_put(const uint8_t *src, uint32_t n)
{
    uint32_t h = s_pcm.head;
    for (uint32_t i = 0; i < n; i++) {
        ((uint8_t *)(uintptr_t)s_pcm.buf)[(h + i) & s_pcm.mask] = src[i];
    }
    __DMB();
    s_pcm.head = h + n;
}

/* Output samples played so far by engine B (DMA position, resolution one carrier period x RCR). */
uint32_t audio_played(void)
{
    __disable_irq();
    uint32_t tc = g_aud_stats.dma_tc, nd = DMA2_Stream5->NDTR;
    bool pend = (DMA2->HISR & S5_TC) != 0u;
    __enable_irq();
    return (tc + (pend ? 1u : 0u)) * 2u * AUD_HALF + (2u * AUD_HALF - nd);
}
