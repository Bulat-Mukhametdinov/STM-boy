#include "buzzer.h"
#include "pinmap.h"

/* Timer input clock after prescaler: 1 MHz -> 1 us per tick */
#define TONE_TIMER_TICK_HZ 1000000U

static TIM_HandleTypeDef htim1;

void Buzzer_Init(void)
{
  GPIO_InitTypeDef gi = {0};
  TIM_OC_InitTypeDef sConfigOC = {0};

  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_TIM1_CLK_ENABLE();

  /* AMP_SD: enable the amplifier (active high) */
  gi.Pin = AMP_SD_PIN;
  gi.Mode = GPIO_MODE_OUTPUT_PP;
  gi.Pull = GPIO_NOPULL;
  gi.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(AMP_SD_PORT, &gi);
  HAL_GPIO_WritePin(AMP_SD_PORT, AMP_SD_PIN, GPIO_PIN_SET);

  /* PA8 = TIM1_CH1, AF1 */
  gi.Pin = GPIO_PIN_8;
  gi.Mode = GPIO_MODE_AF_PP;
  gi.Pull = GPIO_NOPULL;
  gi.Speed = GPIO_SPEED_FREQ_HIGH;
  gi.Alternate = GPIO_AF1_TIM1;
  HAL_GPIO_Init(GPIOA, &gi);

  htim1.Instance = TIM1;
  htim1.Init.Prescaler = (uint32_t)(HAL_RCC_GetPCLK2Freq() / TONE_TIMER_TICK_HZ) - 1;
  htim1.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim1.Init.Period = (TONE_TIMER_TICK_HZ / 1000) - 1; /* placeholder 1kHz */
  htim1.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim1.Init.RepetitionCounter = 0;
  htim1.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  HAL_TIM_PWM_Init(&htim1);

  sConfigOC.OCMode = TIM_OCMODE_PWM1;
  sConfigOC.Pulse = (TONE_TIMER_TICK_HZ / 1000) / 2;
  sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
  sConfigOC.OCNPolarity = TIM_OCNPOLARITY_HIGH;
  sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
  sConfigOC.OCIdleState = TIM_OCIDLESTATE_RESET;
  sConfigOC.OCNIdleState = TIM_OCNIDLESTATE_RESET;
  HAL_TIM_PWM_ConfigChannel(&htim1, &sConfigOC, TIM_CHANNEL_1);
}

void Buzzer_Beep(uint32_t freq_hz, uint32_t duration_ms)
{
  uint32_t period;

  if (freq_hz == 0)
  {
    return;
  }

  period = (TONE_TIMER_TICK_HZ / freq_hz);
  if (period < 2)
  {
    period = 2;
  }

  __HAL_TIM_SET_AUTORELOAD(&htim1, period - 1);
  __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, period / 2);

  HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_1);
  HAL_Delay(duration_ms);
  HAL_TIM_PWM_Stop(&htim1, TIM_CHANNEL_1);
}
