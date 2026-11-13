#include "adc.h"
#include "board.h"
#include "hwcal.h"
#include "timebase.h"
#include "stm32f4xx_hal.h"

#include <string.h>

#define VREFINT_CAL (*(volatile const uint16_t *)0x1FFF7A2Au)
#define TS_CAL1     (*(volatile const uint16_t *)0x1FFF7A2Cu)
#define TS_CAL2     (*(volatile const uint16_t *)0x1FFF7A2Eu)

#define N_ROUNDS 16u
#define N_CH     3u
#define N_CONV   (N_ROUNDS * N_CH)
#define DROP     4u
#define WIN      8u

#define S4_ALL (0x3Du)          /* HIFCR/HISR stream 4 flags */
#define S4_TC  (1u << 5)

static uint16_t s_buf[N_CONV];
static adc_result_t s_res;
static volatile enum { A_IDLE, A_WARM, A_RUN } s_state;
static uint32_t s_t0;
static uint32_t s_started_ms;
static bool s_bg;
static uint32_t s_last_req_ms;
static int16_t s_win[WIN];
static uint32_t s_win_n;

void adc_init(void)
{
    memset(&s_res, 0, sizeof(s_res));
    s_state = A_IDLE;
    s_win_n = 0;
}

void adc_background(bool on)
{
    s_bg = on;
    s_last_req_ms = HAL_GetTick() - 250u;
}

bool adc_busy(void) { return s_state != A_IDLE; }
const adc_result_t *adc_result(void) { return &s_res; }

void adc_shutdown(void)
{
    DMA2_Stream4->CR &= ~DMA_SxCR_EN;
    for (uint32_t n = 0; (DMA2_Stream4->CR & DMA_SxCR_EN) && n < 100000u; n++) {
    }
    DMA2->HIFCR = S4_ALL;
    ADC1->CR2 = 0;
    ADC->CCR = 0;
    RCC->APB2ENR &= ~RCC_APB2ENR_ADC1EN;
    s_state = A_IDLE;
}

void adc_request(void)
{
    if (s_state != A_IDLE) {
        return;
    }
    RCC->APB2ENR |= RCC_APB2ENR_ADC1EN;
    RCC->AHB1ENR |= RCC_AHB1ENR_DMA2EN;
    (void)RCC->AHB1ENR;
    gpio_config(BAT_SNS_PORT, BAT_SNS_PIN, GPIO_MODE_AN_, GPIO_PULL_NONE_, GPIO_SPEED_LOW_, 0);
    ADC->CCR = (1u << ADC_CCR_ADCPRE_Pos) | ADC_CCR_TSVREFE;   /* PCLK2/4 */
    ADC1->CR2 = 0;
    ADC1->CR1 = ADC_CR1_SCAN;
    ADC1->SMPR1 = (7u << 21) | (7u << 24);                      /* IN17, IN18: 480 cycles */
    ADC1->SMPR2 = (7u << 3);                                    /* IN1: 480 cycles */
    ADC1->SQR1 = ((N_CH - 1u) << 20);
    ADC1->SQR2 = 0;
    ADC1->SQR3 = 1u | (17u << 5) | (18u << 10);
    DMA2_Stream4->CR = 0;
    for (uint32_t n = 0; (DMA2_Stream4->CR & DMA_SxCR_EN) && n < 100000u; n++) {
    }
    DMA2->HIFCR = S4_ALL;
    DMA2_Stream4->PAR = (uint32_t)&ADC1->DR;
    DMA2_Stream4->M0AR = (uint32_t)s_buf;
    DMA2_Stream4->NDTR = N_CONV;
    DMA2_Stream4->FCR = 0;
    DMA2_Stream4->CR = DMA_SxCR_MINC | (1u << DMA_SxCR_PSIZE_Pos) | (1u << DMA_SxCR_MSIZE_Pos) |
                       (1u << DMA_SxCR_PL_Pos);                 /* CHSEL 0, periph -> mem */
    DMA2_Stream4->CR |= DMA_SxCR_EN;
    ADC1->CR2 = ADC_CR2_ADON | ADC_CR2_DMA | ADC_CR2_DDS;
    s_t0 = time_us();
    s_started_ms = HAL_GetTick();
    s_state = A_WARM;
}

static void sort_u16(uint16_t *a, uint32_t n)
{
    for (uint32_t i = 1; i < n; i++) {
        uint16_t v = a[i];
        uint32_t j = i;
        while (j > 0 && a[j - 1] > v) {
            a[j] = a[j - 1];
            j--;
        }
        a[j] = v;
    }
}

/* trimmed mean of 16 samples; returns the mean in Q4 (x16) and p-p in raw LSB */
static uint32_t trimmed_q4(uint16_t *a, uint32_t *pp)
{
    sort_u16(a, N_ROUNDS);
    uint32_t sum = 0;
    for (uint32_t i = DROP; i < N_ROUNDS - DROP; i++) {
        sum += a[i];
    }
    *pp = (uint32_t)a[N_ROUNDS - 1u] - a[0];
    return sum * 2u;   /* (sum / 8) * 16 */
}

uint16_t adc_calc_vdda(uint16_t vrefint_cal, uint32_t raw_ref_q4)
{
    if (raw_ref_q4 == 0u) {
        return 0;
    }
    uint64_t v = (uint64_t)3300u * vrefint_cal * 16u / raw_ref_q4;
    return (uint16_t)(v > 65535u ? 65535u : v);
}

static void process(void)
{
    uint16_t b[N_ROUNDS], r[N_ROUNDS], t[N_ROUNDS];
    for (uint32_t i = 0; i < N_ROUNDS; i++) {
        b[i] = s_buf[i * N_CH + 0];
        r[i] = s_buf[i * N_CH + 1];
        t[i] = s_buf[i * N_CH + 2];
    }
    uint32_t ppb, ppr, ppt;
    uint32_t qb = trimmed_q4(b, &ppb);
    uint32_t qr = trimmed_q4(r, &ppr);
    uint32_t qt = trimmed_q4(t, &ppt);
    adc_result_t *x = &s_res;
    x->raw_bat = (uint16_t)((qb + 8u) >> 4);
    x->raw_ref = (uint16_t)((qr + 8u) >> 4);
    x->raw_temp = (uint16_t)((qt + 8u) >> 4);
    x->vdda_mv = adc_calc_vdda(VREFINT_CAL, qr);
    uint32_t vpin = (uint32_t)(((uint64_t)qb * x->vdda_mv + (4095u * 16u / 2u)) / (4095u * 16u));
    x->vpin_mv = (uint16_t)vpin;
    uint32_t unc = (vpin * 115u + 41u) / 82u;
    x->vbat_unc_mv = (uint16_t)(unc > 65535u ? 65535u : unc);
    x->noise_mv = (uint16_t)(((uint64_t)ppb * x->vdda_mv * 115u) / (4095u * 82u));
    uint32_t gain = g_hwcal.vbat_gain_q16 ? g_hwcal.vbat_gain_q16 : 65536u;
    int32_t vb = (int32_t)(((uint64_t)unc * gain + 32768u) >> 16) + g_hwcal.vbat_offset_mv;
    x->vbat_mv = (uint16_t)(vb < 0 ? 0 : (vb > 65535 ? 65535 : vb));
    /* die temperature: normalise the sensor code to 3.3 V */
    int32_t tq = (int32_t)(((uint64_t)qt * x->vdda_mv) / 3300u);
    int32_t c1 = TS_CAL1, c2 = TS_CAL2;
    x->temp_dC = (c2 > c1) ? (int16_t)(300 + (800 * (tq - c1 * 16)) / (16 * (c2 - c1))) : 0;

    /* presence: rolling window of the last 8 vbat values */
    s_win[s_win_n % WIN] = (int16_t)x->vbat_mv;
    s_win_n++;
    uint32_t n = s_win_n < WIN ? s_win_n : WIN;
    int32_t lo = 100000, hi = -100000;
    for (uint32_t i = 0; i < n; i++) {
        if (s_win[i] < lo) lo = s_win[i];
        if (s_win[i] > hi) hi = s_win[i];
    }
    x->spread_mv = (uint16_t)(hi - lo);
    bool bad_v = (x->vbat_mv < 2700u || x->vbat_mv > 4350u);
    if (x->spread_mv > 200u || bad_v) {
        x->presence = ADC_PRES_NONE;
    } else {
        x->presence = (n >= 4u) ? ADC_PRES_BATT : ADC_PRES_UNKNOWN;
    }
    x->seq++;
    x->t_ms = HAL_GetTick();
    (void)ppr;
    (void)ppt;
}

void adc_task(void)
{
    uint32_t now = HAL_GetTick();
    switch (s_state) {
    case A_IDLE:
        if (s_bg && (uint32_t)(now - s_last_req_ms) >= 250u) {
            s_last_req_ms = now;
            adc_request();
        }
        break;
    case A_WARM:
        if ((uint32_t)(time_us() - s_t0) >= 30u) {      /* tSTAB + sensor wake-up */
            ADC1->CR2 |= ADC_CR2_CONT | ADC_CR2_SWSTART;
            s_state = A_RUN;
        }
        break;
    case A_RUN:
        if (DMA2->HISR & S4_TC) {
            ADC1->CR2 &= ~ADC_CR2_CONT;
            process();
            adc_shutdown();
        } else if ((uint32_t)(now - s_started_ms) > 50u) {
            adc_shutdown();     /* never completed */
        }
        break;
    }
}

bool adc_measure(uint32_t timeout_ms)
{
    uint32_t t0 = HAL_GetTick();
    while (s_state != A_IDLE) {                 /* let a running batch finish first */
        adc_task();
        if ((uint32_t)(HAL_GetTick() - t0) > timeout_ms) {
            return false;
        }
    }
    uint32_t seq = s_res.seq;
    adc_request();
    while (s_res.seq == seq) {
        adc_task();
        if ((uint32_t)(HAL_GetTick() - t0) > timeout_ms) {
            adc_shutdown();
            return false;
        }
    }
    return true;
}

/* ---------------------------------------------------------------- calibration points */

static adc_cal_point_t s_pts[ADC_CAL_MAX];
static int s_npts;

bool adc_cal_add(uint16_t meter_vbat_mv, uint16_t meter_v3v3_mv)
{
    if (s_npts >= ADC_CAL_MAX || !adc_measure(100)) {
        return false;
    }
    s_pts[s_npts].meter_vbat_mv = meter_vbat_mv;
    s_pts[s_npts].meter_v3v3_mv = meter_v3v3_mv;
    s_pts[s_npts].unc_vbat_mv = s_res.vbat_unc_mv;
    s_pts[s_npts].vdda_mv = s_res.vdda_mv;
    s_npts++;
    return true;
}

void adc_cal_clear(void) { s_npts = 0; }
int adc_cal_count(void) { return s_npts; }
const adc_cal_point_t *adc_cal_get(int i) { return (i >= 0 && i < s_npts) ? &s_pts[i] : NULL; }

bool adc_cal_fit(uint32_t *gain_q16, int16_t *offset_mv, int32_t *max_err_mv)
{
    if (s_npts < 1) {
        return false;
    }
    float g, b;
    if (s_npts == 1) {
        if (s_pts[0].unc_vbat_mv == 0) {
            return false;
        }
        g = (float)s_pts[0].meter_vbat_mv / (float)s_pts[0].unc_vbat_mv;
        b = 0.0f;
    } else {
        int64_t sx = 0, sy = 0, sxx = 0, sxy = 0, n = s_npts;
        for (int i = 0; i < s_npts; i++) {
            int64_t x = s_pts[i].unc_vbat_mv, y = s_pts[i].meter_vbat_mv;
            sx += x;
            sy += y;
            sxx += x * x;
            sxy += x * y;
        }
        int64_t den = n * sxx - sx * sx;
        if (den < 1000) {                     /* all points at the same voltage: gain only */
            g = (float)sy / (float)sx;
            b = 0.0f;
        } else {
            g = (float)(n * sxy - sx * sy) / (float)den;
            b = ((float)sy - g * (float)sx) / (float)n;
        }
    }
    if (g < 0.8f || g > 1.25f || b < -300.0f || b > 300.0f) {
        return false;                         /* implausible: refuse */
    }
    uint32_t gq = (uint32_t)(g * 65536.0f + 0.5f);
    int16_t off = (int16_t)(b < 0 ? b - 0.5f : b + 0.5f);
    int32_t me = 0;
    for (int i = 0; i < s_npts; i++) {
        int32_t v = (int32_t)(((uint64_t)s_pts[i].unc_vbat_mv * gq + 32768u) >> 16) + off;
        int32_t e = v - (int32_t)s_pts[i].meter_vbat_mv;
        if (e < 0) e = -e;
        if (e > me) me = e;
    }
    *gain_q16 = gq;
    *offset_mv = off;
    *max_err_mv = me;
    return true;
}
