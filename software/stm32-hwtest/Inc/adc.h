#ifndef ADC_H
#define ADC_H

/* Battery / VDDA / die-temperature measurement (P2).
   ADC1 scan IN1 (PA1, battery divider 33k/82k, ~23.5k source, no cap) +
   IN17 (VREFINT) + IN18 (temperature), 16 rounds x 3 channels at 480 cycles,
   ADCCLK = PCLK2/4 = 24 MHz (never /2), DMA2 stream 4 channel 0, trimmed mean
   (4 low + 4 high dropped). VDDA = 3300 * VREFINT_CAL / raw_vref.
   Vbat = Vpin * 115/82 * gain_q16/65536 + offset (HWCAL). ~1 ms per batch;
   the ADC is powered down between batches. */

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    ADC_PRES_UNKNOWN = 0,   /* window not filled yet */
    ADC_PRES_BATT,
    ADC_PRES_NONE,          /* spread > 200 mV in the window or V outside 2.7..4.35 V */
} adc_pres_t;

typedef struct {
    uint16_t vdda_mv;
    uint16_t vpin_mv;
    uint16_t vbat_unc_mv;   /* before HWCAL gain/offset */
    uint16_t vbat_mv;
    int16_t temp_dC;        /* die temperature, 0.1 degC */
    uint16_t raw_bat, raw_ref, raw_temp;  /* trimmed means (12 bit) */
    uint16_t noise_mv;      /* peak-to-peak of the 16 raw battery samples, in battery mV */
    uint16_t spread_mv;     /* rolling window (8 batches) peak-to-peak of vbat_mv */
    uint8_t presence;       /* adc_pres_t */
    uint32_t seq;           /* completed batches since boot */
    uint32_t t_ms;          /* HAL tick at completion */
} adc_result_t;

void adc_init(void);                 /* reset state (does not touch the hardware) */
void adc_set_pre(uint32_t pre);      /* ADCPRE field 0..3 (/2 /4 /6 /8), effective from the next batch */
uint32_t adc_pre_div(void);          /* current ADC prescaler as a divider */
void adc_request(void);              /* start one batch (non-blocking) */
void adc_task(void);                 /* call often: drives the batch; with background on it requests every 250 ms */
void adc_background(bool on);        /* main-loop periodic sampling (250 ms) */
bool adc_busy(void);
const adc_result_t *adc_result(void);
bool adc_measure(uint32_t timeout_ms);   /* blocking: wait for a fresh batch */
void adc_shutdown(void);             /* ADC + DMA stream off, ADC clock gated */

/* Calibration points (RAM) for `power.adccal` / `adccal`. */
#define ADC_CAL_MAX 8
typedef struct {
    uint16_t meter_vbat_mv, meter_v3v3_mv;
    uint16_t unc_vbat_mv;   /* adc vbat_unc_mv at capture */
    uint16_t vdda_mv;       /* adc VDDA estimate at capture */
} adc_cal_point_t;

bool adc_cal_add(uint16_t meter_vbat_mv, uint16_t meter_v3v3_mv);   /* captures a fresh result */
void adc_cal_clear(void);
int adc_cal_count(void);
const adc_cal_point_t *adc_cal_get(int i);
/* 1 point: gain only; >= 2: least squares. Does not touch g_hwcal. */
bool adc_cal_fit(uint32_t *gain_q16, int16_t *offset_mv, int32_t *max_err_mv);

uint16_t adc_calc_vdda(uint16_t vrefint_cal, uint32_t raw_ref_q4);

#endif
