#include "power.h"
#include "pinmap.h"

/* Battery divider: BAT_STAT = VBAT * R13/(R12+R13) = VBAT * 82/115 */
#define BATT_DIVIDER_NUM   115
#define BATT_DIVIDER_DEN   82

#define VDDA_MV            3300U
#define ADC_MAX            4095U

#define BATT_EMPTY_MV      3300U
#define BATT_FULL_MV       4200U

/* Best-effort bounds for "a real single-cell LiPo is actually connected".
   BQ24075-managed packs sit within this range in normal operation. */
#define BATT_PRESENT_MIN_MV 2700U
#define BATT_PRESENT_MAX_MV 4300U

/* Battery-presence discriminator: a real cell holds a steady DC voltage, while
   "no battery + USB" leaves the BAT node a meander (charger cycling). We sample
   the pin into a rolling window and look at the spread (max-min): a stable cell
   gives a few mV, a meander gives hundreds. */
#define BATT_WIN               32       /* samples in the window */
#define BATT_SAMPLE_MS         20       /* -> ~640ms window, spans several meander periods */
#define BATT_PRESENT_SPREAD_MV 200U     /* above this spread => treated as no battery */

static ADC_HandleTypeDef hadc1;

static uint16_t pinRing[BATT_WIN];   /* pin voltage samples, mV */
static uint8_t  ringIdx = 0;
static uint8_t  ringCount = 0;
static uint32_t lastSampleMs = 0;

void Power_Init(void)
{
  GPIO_InitTypeDef gi = {0};
  ADC_ChannelConfTypeDef sConfig = {0};

  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_ADC1_CLK_ENABLE();

  /* BAT_STAT on PA1 = ADC1_IN1, analog */
  gi.Pin = BAT_STAT_PIN;
  gi.Mode = GPIO_MODE_ANALOG;
  gi.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(BAT_STAT_PORT, &gi);

  /* CHRG_STAT: open-drain from charger, external pull-up already present */
  gi.Pin = CHRG_STAT_PIN;
  gi.Mode = GPIO_MODE_INPUT;
  gi.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(CHRG_STAT_PORT, &gi);

  /* VBUS sense: with no USB the +5V rail floats to ~2.5V (charger back-feed),
     putting the divided node (~1.75V) in the GPIO's indeterminate band. An
     internal pull-down drags the no-USB node clearly below VIL, while a real
     5V VBUS still divides high enough to read HIGH. */
  gi.Pin = VBUS_SNS_PIN;
  gi.Mode = GPIO_MODE_INPUT;
  gi.Pull = GPIO_PULLDOWN;
  HAL_GPIO_Init(VBUS_SNS_PORT, &gi);

  hadc1.Instance = ADC1;
  hadc1.Init.ClockPrescaler = ADC_CLOCK_SYNC_PCLK_DIV4;
  hadc1.Init.Resolution = ADC_RESOLUTION_12B;
  hadc1.Init.ScanConvMode = DISABLE;
  hadc1.Init.ContinuousConvMode = DISABLE;
  hadc1.Init.DiscontinuousConvMode = DISABLE;
  hadc1.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_NONE;
  hadc1.Init.DataAlign = ADC_DATAALIGN_RIGHT;
  hadc1.Init.NbrOfConversion = 1;
  hadc1.Init.DMAContinuousRequests = DISABLE;
  hadc1.Init.EOCSelection = ADC_EOC_SINGLE_CONV;
  HAL_ADC_Init(&hadc1);

  sConfig.Channel = ADC_CHANNEL_1;
  sConfig.Rank = 1;
  sConfig.SamplingTime = ADC_SAMPLETIME_84CYCLES;
  HAL_ADC_ConfigChannel(&hadc1, &sConfig);
}

#define ADC_AVG_SAMPLES 16

static uint16_t ReadAdcRaw(void)
{
  uint32_t sum = 0;
  uint16_t n = 0;

  for (uint16_t i = 0; i < ADC_AVG_SAMPLES; i++)
  {
    HAL_ADC_Start(&hadc1);
    if (HAL_ADC_PollForConversion(&hadc1, 10) == HAL_OK)
    {
      sum += HAL_ADC_GetValue(&hadc1);
      n++;
    }
    HAL_ADC_Stop(&hadc1);
  }

  return (n > 0) ? (uint16_t)(sum / n) : 0;
}

static uint16_t ReadPinMvOnce(void)
{
  uint32_t raw = ReadAdcRaw();
  return (uint16_t)((raw * VDDA_MV) / ADC_MAX);
}

/* Call frequently from the main loop; self-throttles to BATT_SAMPLE_MS. */
void Power_Poll(void)
{
  uint32_t now = HAL_GetTick();

  if ((uint32_t)(now - lastSampleMs) < BATT_SAMPLE_MS)
  {
    return;
  }
  lastSampleMs = now;

  pinRing[ringIdx] = ReadPinMvOnce();
  ringIdx = (uint8_t)((ringIdx + 1) % BATT_WIN);
  if (ringCount < BATT_WIN)
  {
    ringCount++;
  }
}

static void RingStats(uint16_t *pmin, uint16_t *pmax, uint16_t *pavg)
{
  uint16_t mn = 0xFFFF, mx = 0;
  uint32_t sum = 0;

  if (ringCount == 0)
  {
    *pmin = *pmax = *pavg = 0;
    return;
  }

  for (uint8_t i = 0; i < ringCount; i++)
  {
    uint16_t v = pinRing[i];
    if (v < mn) { mn = v; }
    if (v > mx) { mx = v; }
    sum += v;
  }
  *pmin = mn;
  *pmax = mx;
  *pavg = (uint16_t)(sum / ringCount);
}

uint16_t Power_ReadPinMillivolts(void)
{
  uint16_t mn, mx, av;

  if (ringCount == 0)
  {
    return ReadPinMvOnce();
  }
  RingStats(&mn, &mx, &av);
  return av;
}

uint16_t Power_ReadBatteryMillivolts(void)
{
  uint32_t pinMv = Power_ReadPinMillivolts();
  uint32_t battMv = (pinMv * BATT_DIVIDER_NUM) / BATT_DIVIDER_DEN;

  return (uint16_t)battMv;
}

uint8_t Power_ReadBatteryPercent(void)
{
  int32_t mv = (int32_t)Power_ReadBatteryMillivolts();

  if (mv <= (int32_t)BATT_EMPTY_MV) { return 0; }
  if (mv >= (int32_t)BATT_FULL_MV)  { return 100; }

  return (uint8_t)(((mv - (int32_t)BATT_EMPTY_MV) * 100) / ((int32_t)BATT_FULL_MV - (int32_t)BATT_EMPTY_MV));
}

uint8_t Power_IsBatteryPresent(void)
{
  uint16_t mn, mx, av;
  uint32_t battMv;

  /* Need a reasonably full window before trusting the verdict. */
  if (ringCount < (BATT_WIN / 2))
  {
    return 0;
  }

  RingStats(&mn, &mx, &av);

  /* A meander (no battery + charger cycling) has a large spread. */
  if ((uint16_t)(mx - mn) > BATT_PRESENT_SPREAD_MV)
  {
    return 0;
  }

  /* Stable, and within a plausible LiPo range -> a real cell is present. */
  battMv = ((uint32_t)av * BATT_DIVIDER_NUM) / BATT_DIVIDER_DEN;
  return (battMv >= BATT_PRESENT_MIN_MV && battMv <= BATT_PRESENT_MAX_MV) ? 1 : 0;
}

uint8_t Power_IsCharging(void)
{
  /* BQ24075 #CHG is open-drain, active low while charging */
  return (HAL_GPIO_ReadPin(CHRG_STAT_PORT, CHRG_STAT_PIN) == GPIO_PIN_RESET) ? 1 : 0;
}

uint8_t Power_IsUsbConnected(void)
{
  return (HAL_GPIO_ReadPin(VBUS_SNS_PORT, VBUS_SNS_PIN) == GPIO_PIN_SET) ? 1 : 0;
}
