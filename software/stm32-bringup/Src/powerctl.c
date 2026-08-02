#include "powerctl.h"
#include "pinmap.h"

/* OFF_ACK polarity the ATTINY firmware expects. ATTINY here reads ACK as
   active-HIGH (drive PA3 high to confirm shutdown). Set to 0 if driving low. */
#define OFF_ACK_ACTIVE_HIGH 1

#if OFF_ACK_ACTIVE_HIGH
#define OFF_ACK_ASSERT_LEVEL    GPIO_PIN_SET
#define OFF_ACK_DEASSERT_LEVEL  GPIO_PIN_RESET
#else
#define OFF_ACK_ASSERT_LEVEL    GPIO_PIN_RESET
#define OFF_ACK_DEASSERT_LEVEL  GPIO_PIN_SET
#endif

/* Debounce time OFF_REQ must stay asserted (low) before we accept it. */
#define OFF_REQ_DEBOUNCE_MS 30U

void PowerCtl_Init(void)
{
  GPIO_InitTypeDef gi = {0};

  __HAL_RCC_GPIOA_CLK_ENABLE();

  /* OFF_REQ: input. Level is set by the external divider (R18/R17), idle ~3V3,
     ~0.3V when the ATTINY asserts. No internal pull needed. */
  gi.Pin = OFF_REQ_PIN;
  gi.Mode = GPIO_MODE_INPUT;
  gi.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(OFF_REQ_PORT, &gi);

  /* OFF_ACK: drive the deasserted level from boot so the ATTINY always reads a
     clean level while it waits for acknowledge (no floating line). Preset ODR
     before switching to output to avoid a glitch. */
  HAL_GPIO_WritePin(OFF_ACK_PORT, OFF_ACK_PIN, OFF_ACK_DEASSERT_LEVEL);
  gi.Pin = OFF_ACK_PIN;
  gi.Mode = GPIO_MODE_OUTPUT_PP;
  gi.Pull = GPIO_NOPULL;
  gi.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(OFF_ACK_PORT, &gi);
}

uint8_t PowerCtl_PollShutdown(void)
{
  static uint8_t armed = 0;       /* only trigger after OFF_REQ was seen idle-high */
  static uint8_t counting = 0;
  static uint32_t lowStart = 0;

  uint8_t reqLow = (HAL_GPIO_ReadPin(OFF_REQ_PORT, OFF_REQ_PIN) == GPIO_PIN_RESET) ? 1 : 0;

  if (!reqLow)
  {
    /* Idle. Seeing a clean high arms us and prevents a boot-time false trigger
       if OFF_REQ happens to sit low at power-up. */
    armed = 1;
    counting = 0;
    return 0;
  }

  if (!armed)
  {
    return 0;
  }

  if (!counting)
  {
    counting = 1;
    lowStart = HAL_GetTick();
    return 0;
  }

  if ((uint32_t)(HAL_GetTick() - lowStart) >= OFF_REQ_DEBOUNCE_MS)
  {
    armed = 0;      /* fire once */
    counting = 0;
    return 1;
  }

  return 0;
}

void PowerCtl_AcknowledgeShutdown(void)
{
  /* OFF_ACK is already a push-pull output (set up in Init); just assert it. */
  HAL_GPIO_WritePin(OFF_ACK_PORT, OFF_ACK_PIN, OFF_ACK_ASSERT_LEVEL);
}
