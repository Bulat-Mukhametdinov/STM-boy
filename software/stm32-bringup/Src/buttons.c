#include "buttons.h"
#include "pinmap.h"

const char *ButtonNames[BTN_COUNT] = {
  "UP", "DOWN", "LEFT", "RIGHT", "CE", "A", "B", "X", "Y"
};

typedef struct
{
  GPIO_TypeDef *port;
  uint16_t pin;
} ButtonGpio;

static const ButtonGpio btnGpio[BTN_COUNT] = {
  { BTN_UP_PORT,    BTN_UP_PIN },
  { BTN_DOWN_PORT,  BTN_DOWN_PIN },
  { BTN_LEFT_PORT,  BTN_LEFT_PIN },
  { BTN_RIGHT_PORT, BTN_RIGHT_PIN },
  { BTN_CE_PORT,    BTN_CE_PIN },
  { BTN_A_PORT,     BTN_A_PIN },
  { BTN_B_PORT,     BTN_B_PIN },
  { BTN_X_PORT,     BTN_X_PIN },
  { BTN_Y_PORT,     BTN_Y_PIN },
};

#define DEBOUNCE_STABLE_COUNT 3

static uint8_t debouncedState[BTN_COUNT];   /* 1 = pressed */
static uint8_t prevDebouncedState[BTN_COUNT];
static uint8_t candidateState[BTN_COUNT];
static uint8_t candidateCount[BTN_COUNT];

void Buttons_Init(void)
{
  GPIO_InitTypeDef gi = {0};

  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  gi.Mode = GPIO_MODE_INPUT;
  gi.Pull = GPIO_PULLUP;
  gi.Speed = GPIO_SPEED_FREQ_LOW;

  for (int i = 0; i < BTN_COUNT; i++)
  {
    gi.Pin = btnGpio[i].pin;
    HAL_GPIO_Init(btnGpio[i].port, &gi);
    debouncedState[i] = 0;
    prevDebouncedState[i] = 0;
    candidateState[i] = 0;
    candidateCount[i] = 0;
  }
}

void Buttons_Scan(void)
{
  for (int i = 0; i < BTN_COUNT; i++)
  {
    uint8_t raw = (HAL_GPIO_ReadPin(btnGpio[i].port, btnGpio[i].pin) == GPIO_PIN_RESET) ? 1 : 0;

    prevDebouncedState[i] = debouncedState[i];

    if (raw == candidateState[i])
    {
      if (candidateCount[i] < DEBOUNCE_STABLE_COUNT)
      {
        candidateCount[i]++;
      }
      if (candidateCount[i] >= DEBOUNCE_STABLE_COUNT)
      {
        debouncedState[i] = raw;
      }
    }
    else
    {
      candidateState[i] = raw;
      candidateCount[i] = 0;
    }
  }
}

uint8_t Buttons_IsPressed(ButtonId id)
{
  return debouncedState[id];
}

uint8_t Buttons_WasPressedEdge(ButtonId id)
{
  return (debouncedState[id] && !prevDebouncedState[id]) ? 1 : 0;
}

uint8_t Buttons_WasReleasedEdge(ButtonId id)
{
  return (!debouncedState[id] && prevDebouncedState[id]) ? 1 : 0;
}
