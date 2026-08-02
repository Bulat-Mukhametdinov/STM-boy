#include "power.h"

#include "board_pins.h"
#include "main.h"

#define POWER_ADC_MAX 4095u
#define POWER_ADC_REF_MV 3300u
#define POWER_SAMPLE_PERIOD_MS 40u
#define POWER_ADC_TIMEOUT_MS 2u
#define POWER_QUICK_READS 4u
/* Rolling window spanning ~1s so the charger/meander ripple on the BAT node is
 * averaged out instead of making the gauge jump every update. */
#define POWER_WINDOW_SAMPLES 25u
#define POWER_OFF_REQ_DEBOUNCE_MS 30u

typedef struct {
    uint16_t millivolts;
    uint8_t percent;
} BatteryCurvePoint;

static const BatteryCurvePoint battery_curve[] = {
    {4200u, 100u},
    {4100u, 90u},
    {4000u, 75u},
    {3900u, 55u},
    {3800u, 35u},
    {3700u, 20u},
    {3600u, 10u},
    {3500u, 5u},
    {3300u, 0u},
};

static uint16_t battery_raw;
static uint16_t battery_mv;
static uint8_t battery_percent;
static uint16_t raw_window[POWER_WINDOW_SAMPLES];
static uint8_t raw_window_count;
static uint8_t raw_window_index;
static bool charging;
static bool usb_present;
static bool off_requested;
static uint32_t last_sample_ms;
static bool power_ready;

static void power_enable_gpio_clock(GPIO_TypeDef *port)
{
    if (port == GPIOA) {
        __HAL_RCC_GPIOA_CLK_ENABLE();
    } else if (port == GPIOB) {
        __HAL_RCC_GPIOB_CLK_ENABLE();
    } else if (port == GPIOC) {
        __HAL_RCC_GPIOC_CLK_ENABLE();
    } else if (port == GPIOD) {
        __HAL_RCC_GPIOD_CLK_ENABLE();
    } else if (port == GPIOE) {
        __HAL_RCC_GPIOE_CLK_ENABLE();
    }
}

static uint8_t battery_percent_from_mv(uint16_t mv)
{
    if (mv >= POWER_BATTERY_FULL_MV) {
        return 100u;
    }
    if (mv <= POWER_BATTERY_EMPTY_MV) {
        return 0u;
    }

    for (uint32_t i = 0; i + 1u < (sizeof(battery_curve) / sizeof(battery_curve[0])); ++i) {
        const BatteryCurvePoint *hi = &battery_curve[i];
        const BatteryCurvePoint *lo = &battery_curve[i + 1u];

        if (mv <= hi->millivolts && mv >= lo->millivolts) {
            uint32_t span_mv = (uint32_t)hi->millivolts - lo->millivolts;
            uint32_t span_pct = (uint32_t)hi->percent - lo->percent;
            uint32_t above_lo = (uint32_t)mv - lo->millivolts;
            return (uint8_t)(lo->percent + ((above_lo * span_pct + (span_mv / 2u)) / span_mv));
        }
    }

    return (uint8_t)(((uint32_t)(mv - POWER_BATTERY_EMPTY_MV) * 100u) /
                     (POWER_BATTERY_FULL_MV - POWER_BATTERY_EMPTY_MV));
}

static uint16_t battery_mv_from_raw(uint16_t raw)
{
    uint64_t sense_mv = ((uint64_t)raw * POWER_ADC_REF_MV + (POWER_ADC_MAX / 2u)) / POWER_ADC_MAX;
    uint64_t numerator = (uint64_t)(POWER_BATTERY_DIVIDER_HIGH_OHMS + POWER_BATTERY_DIVIDER_LOW_OHMS);
    uint64_t mv = (sense_mv * numerator + (POWER_BATTERY_DIVIDER_LOW_OHMS / 2u)) /
                  POWER_BATTERY_DIVIDER_LOW_OHMS;

    if (mv > 0xFFFFu) {
        return 0xFFFFu;
    }
    return (uint16_t)mv;
}

static bool power_adc_read(uint16_t *raw)
{
    uint32_t start_ms;

    if (raw == NULL) {
        return false;
    }

    ADC1->SR = 0u;
    ADC1->CR2 |= ADC_CR2_SWSTART;
    start_ms = HAL_GetTick();

    while ((ADC1->SR & ADC_SR_EOC) == 0u) {
        if ((uint32_t)(HAL_GetTick() - start_ms) > POWER_ADC_TIMEOUT_MS) {
            return false;
        }
    }

    *raw = (uint16_t)(ADC1->DR & POWER_ADC_MAX);
    return true;
}

/* Take a few back-to-back conversions and return their average, or false if the
 * ADC did not respond. This only smooths sampling noise, not the slow meander. */
static bool power_read_quick_average(uint16_t *out)
{
    uint32_t sum = 0u;
    uint32_t count = 0u;
    uint16_t raw = 0u;

    for (uint32_t i = 0; i < POWER_QUICK_READS; ++i) {
        if (power_adc_read(&raw)) {
            sum += raw;
            ++count;
        }
    }

    if (count == 0u) {
        return false;
    }
    *out = (uint16_t)((sum + (count / 2u)) / count);
    return true;
}

static void power_window_push(uint16_t sample)
{
    uint32_t sum = 0u;

    raw_window[raw_window_index] = sample;
    raw_window_index = (uint8_t)((raw_window_index + 1u) % POWER_WINDOW_SAMPLES);
    if (raw_window_count < POWER_WINDOW_SAMPLES) {
        ++raw_window_count;
    }

    for (uint8_t i = 0; i < raw_window_count; ++i) {
        sum += raw_window[i];
    }

    battery_raw = (uint16_t)((sum + (raw_window_count / 2u)) / raw_window_count);
    battery_mv = battery_mv_from_raw(battery_raw);
    battery_percent = battery_percent_from_mv(battery_mv);
}

static void power_sample(void)
{
    uint16_t sample = 0u;

    if (power_read_quick_average(&sample)) {
        power_window_push(sample);
    }

    charging = HAL_GPIO_ReadPin(POWER_CHRG_STAT_GPIO_PORT, POWER_CHRG_STAT_PIN) == GPIO_PIN_RESET;
    usb_present = HAL_GPIO_ReadPin(POWER_VBUS_GPIO_PORT, POWER_VBUS_PIN) == GPIO_PIN_SET;
    off_requested = HAL_GPIO_ReadPin(POWER_OFF_REQ_GPIO_PORT, POWER_OFF_REQ_PIN) == GPIO_PIN_RESET;
    last_sample_ms = HAL_GetTick();
}

static void power_gpio_init(void)
{
    GPIO_InitTypeDef gpio = {0};

    power_enable_gpio_clock(POWER_BAT_STAT_GPIO_PORT);
    power_enable_gpio_clock(POWER_CHRG_STAT_GPIO_PORT);
    power_enable_gpio_clock(POWER_OFF_REQ_GPIO_PORT);
    power_enable_gpio_clock(POWER_OFF_ACK_GPIO_PORT);
    power_enable_gpio_clock(POWER_VBUS_GPIO_PORT);

    gpio.Pin = POWER_BAT_STAT_PIN;
    gpio.Mode = GPIO_MODE_ANALOG;
    gpio.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(POWER_BAT_STAT_GPIO_PORT, &gpio);

    gpio.Pin = POWER_CHRG_STAT_PIN;
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(POWER_CHRG_STAT_GPIO_PORT, &gpio);

    gpio.Pin = POWER_OFF_REQ_PIN;
    HAL_GPIO_Init(POWER_OFF_REQ_GPIO_PORT, &gpio);

    gpio.Pin = POWER_VBUS_PIN;
    HAL_GPIO_Init(POWER_VBUS_GPIO_PORT, &gpio);

    gpio.Pin = POWER_OFF_ACK_PIN;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(POWER_OFF_ACK_GPIO_PORT, &gpio);
    HAL_GPIO_WritePin(POWER_OFF_ACK_GPIO_PORT, POWER_OFF_ACK_PIN, GPIO_PIN_RESET);
}

static void power_adc_init(void)
{
    __HAL_RCC_ADC1_CLK_ENABLE();

    ADC1->CR1 = 0u;
    ADC1->CR2 = 0u;
    ADC1->SQR1 = 0u;
    ADC1->SQR2 = 0u;
    ADC1->SQR3 = (POWER_BAT_ADC_CHANNEL << ADC_SQR3_SQ1_Pos) & ADC_SQR3_SQ1;
    ADC1->SMPR2 &= ~ADC_SMPR2_SMP0;
    ADC1->SMPR2 |= ADC_SMPR2_SMP0;
    ADC->CCR &= ~ADC_CCR_ADCPRE;
    ADC->CCR |= ADC_CCR_ADCPRE_0;
    ADC1->CR2 |= ADC_CR2_ADON;
}

void Power_Init(void)
{
    if (power_ready) {
        return;
    }

    power_gpio_init();
    power_adc_init();
    HAL_Delay(1u);

    battery_percent = 0u;
    battery_mv = 0u;

    /* Seed the whole window with one reading so the gauge starts at a sensible
     * value instead of ramping up from zero as the window fills. */
    {
        uint16_t seed = 0u;
        if (power_read_quick_average(&seed)) {
            for (uint8_t i = 0; i < POWER_WINDOW_SAMPLES; ++i) {
                raw_window[i] = seed;
            }
            raw_window_count = POWER_WINDOW_SAMPLES;
            raw_window_index = 0u;
            battery_raw = seed;
            battery_mv = battery_mv_from_raw(seed);
            battery_percent = battery_percent_from_mv(battery_mv);
        }
    }

    power_sample();
    power_ready = true;
}

void Power_Task(void)
{
    uint32_t now_ms;

    if (!power_ready) {
        return;
    }

    now_ms = HAL_GetTick();
    if ((uint32_t)(now_ms - last_sample_ms) >= POWER_SAMPLE_PERIOD_MS) {
        power_sample();
    }
}

uint8_t Power_GetBatteryPercent(void)
{
    return battery_percent;
}

uint16_t Power_GetBatteryMillivolts(void)
{
    return battery_mv;
}

bool Power_IsCharging(void)
{
    return charging;
}

bool Power_IsUsbPresent(void)
{
    return usb_present;
}

bool Power_IsOffRequested(void)
{
    return off_requested;
}

bool Power_PollShutdown(void)
{
    static bool armed = false;
    static bool counting = false;
    static uint32_t low_start_ms;
    bool req_low;

    if (!power_ready) {
        return false;
    }

    req_low = HAL_GPIO_ReadPin(POWER_OFF_REQ_GPIO_PORT, POWER_OFF_REQ_PIN) == GPIO_PIN_RESET;

    if (!req_low) {
        /* OFF_REQ idles high; only arm after we have actually seen it high so a
         * pin held low at boot cannot trigger an immediate shutdown. */
        armed = true;
        counting = false;
        return false;
    }
    if (!armed) {
        return false;
    }
    if (!counting) {
        counting = true;
        low_start_ms = HAL_GetTick();
        return false;
    }
    if ((uint32_t)(HAL_GetTick() - low_start_ms) >= POWER_OFF_REQ_DEBOUNCE_MS) {
        armed = false;
        counting = false;
        return true;
    }
    return false;
}

void Power_SetOffAck(bool acknowledged)
{
    HAL_GPIO_WritePin(POWER_OFF_ACK_GPIO_PORT,
                      POWER_OFF_ACK_PIN,
                      acknowledged ? GPIO_PIN_SET : GPIO_PIN_RESET);
}
