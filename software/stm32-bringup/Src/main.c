#include "main.h"
#include "usbd_core.h"
#include "usbd_desc.h"
#include "usbd_cdc.h"
#include "usbd_cdc_if.h"
#include "pinmap.h"
#include "st7735.h"
#include "buttons.h"
#include "buzzer.h"
#include "power.h"
#include "powerctl.h"
#include <stdio.h>
#include <string.h>

USBD_HandleTypeDef hUsbDeviceFS;
SPI_HandleTypeDef hspi1;

static void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_SPI1_Init(void);
static void DrawStatus(void);
static void PerformShutdown(void);

/* --- Button test grid layout (screen 128x160) --- */
typedef struct
{
  ButtonId id;
  uint16_t x, y, w, h;
  uint16_t color;
} BtnBox;

#define BOX 16

static const BtnBox boxes[BTN_COUNT] = {
  { BTN_UP,    32, 65, BOX, BOX, COLOR_RED },
  { BTN_DOWN,  32, 105, BOX, BOX, COLOR_RED },
  { BTN_LEFT,  12, 85, BOX, BOX, COLOR_RED },
  { BTN_RIGHT, 52, 85, BOX, BOX, COLOR_RED },
  { BTN_CE,    32, 85, BOX, BOX, COLOR_YELLOW },
  /* Physical gamepad layout: Y=top(12), B=right(3), A=bottom(6), X=left(9).
     Box positions mirror the physical placement; each id still maps to its
     documented GPIO, so this only fixes where the on-screen box appears. */
  { BTN_Y,     112, 65, BOX, BOX, COLOR_YELLOW },
  { BTN_B,     132, 85, BOX, BOX, COLOR_RED },
  { BTN_A,     112, 105, BOX, BOX, COLOR_GREEN },
  { BTN_X,     92, 85, BOX, BOX, COLOR_BLUE },
};

/* --- Status panel layout --- */
#define BAT_BAR_X   4
#define BAT_BAR_Y   4
#define BAT_BAR_W   90
#define BAT_BAR_H   14
#define CHG_SQ_X    98
#define USB_SQ_X    118
#define SQ_Y        4
#define SQ_SZ       14
#define TEXT_LINE1_Y 24
#define TEXT_LINE2_Y 44
#define TEXT_LINE3_Y 66

/* distinct beep pitch per button, in Hz */
static const uint16_t beepFreq[BTN_COUNT] = {
  784, 659, 587, 880, 1046, 523, 698, 392, 932
};

int main(void)
{
  char msg[96];
  uint32_t lastStatusMs = 0;
  uint32_t lastPrintMs = 0;

  HAL_Init();
  SystemClock_Config();
  MX_GPIO_Init();
  MX_SPI1_Init();

  Buttons_Init();
  Buzzer_Init();
  Power_Init();
  PowerCtl_Init();

  USBD_Init(&hUsbDeviceFS, &VCP_Desc, 0);
  USBD_RegisterClass(&hUsbDeviceFS, USBD_CDC_CLASS);
  USBD_CDC_RegisterInterface(&hUsbDeviceFS, &USBD_Interface_fops_FS);
  USBD_Start(&hUsbDeviceFS);

  ST7735_Init();
  ST7735_FillScreen(COLOR_BLACK);
  ST7735_BacklightOn();

  for (int i = 0; i < BTN_COUNT; i++)
  {
    ST7735_DrawRectOutline(boxes[i].x, boxes[i].y, boxes[i].w, boxes[i].h, boxes[i].color);
  }
  DrawStatus();

  CDC_Transmit_FS((uint8_t *)"PocketConsole system monitor ready\r\n", 37);

  while (1)
  {
    if (PowerCtl_PollShutdown())
    {
      PerformShutdown();
    }

    Power_Poll();
    Buttons_Scan();

    for (int i = 0; i < BTN_COUNT; i++)
    {
      if (Buttons_WasPressedEdge(boxes[i].id))
      {
        ST7735_FillRect(boxes[i].x, boxes[i].y, boxes[i].w, boxes[i].h, COLOR_WHITE);
        Buzzer_Beep(beepFreq[boxes[i].id], 60);

        int n = snprintf(msg, sizeof(msg), "BTN %s down\r\n", ButtonNames[boxes[i].id]);
        CDC_Transmit_FS((uint8_t *)msg, (uint16_t)n);
      }
      else if (Buttons_WasReleasedEdge(boxes[i].id))
      {
        ST7735_DrawRectOutline(boxes[i].x, boxes[i].y, boxes[i].w, boxes[i].h, boxes[i].color);

        int n = snprintf(msg, sizeof(msg), "BTN %s up\r\n", ButtonNames[boxes[i].id]);
        CDC_Transmit_FS((uint8_t *)msg, (uint16_t)n);
      }
    }

    uint32_t now = HAL_GetTick();
    if ((now - lastStatusMs) >= 400)
    {
      lastStatusMs = now;
      DrawStatus();
    }
    if ((now - lastPrintMs) >= 1000)
    {
      lastPrintMs = now;
      int n = snprintf(msg, sizeof(msg), "BATT=%umV PIN=%umV (%u%%) PRESENT=%u CHG=%u USB=%u\r\n",
                        Power_ReadBatteryMillivolts(), Power_ReadPinMillivolts(),
                        Power_ReadBatteryPercent(), Power_IsBatteryPresent(),
                        Power_IsCharging(), Power_IsUsbConnected());
      CDC_Transmit_FS((uint8_t *)msg, (uint16_t)n);
    }
  }
}

static void PerformShutdown(void)
{
  /* Shutdown housekeeping: silence the amp, tell the user, then hand off to the
     ATTINY which cuts system power. (Add SPI-flash flush here once FS exists.) */
  HAL_GPIO_WritePin(AMP_SD_PORT, AMP_SD_PIN, GPIO_PIN_RESET);

  CDC_Transmit_FS((uint8_t *)"Shutdown requested, powering off\r\n", 34);

  ST7735_FillScreen(COLOR_BLACK);
  ST7735_DrawString(24, 56, "POWER OFF", COLOR_WHITE, COLOR_BLACK, 2);
  HAL_Delay(400);

  /* Optionally drop the backlight so the panel goes dark before power is cut. */
  HAL_GPIO_WritePin(DISP_BLK_PORT, DISP_BLK_PIN, GPIO_PIN_RESET);

  PowerCtl_AcknowledgeShutdown();

  /* Power will be removed momentarily; if the ATTINY's force-off timeout is the
     one that fires instead, we simply wait here until the rail collapses. */
  while (1)
  {
  }
}

static void DrawStatus(void)
{
  char line[20];

  uint8_t present = Power_IsBatteryPresent();
  uint16_t battMv = Power_ReadBatteryMillivolts();
  uint16_t pinMv = Power_ReadPinMillivolts();
  uint8_t percent = Power_ReadBatteryPercent();
  uint8_t chg = Power_IsCharging();
  uint8_t usb = Power_IsUsbConnected();

  /* Battery gauge bar */
  ST7735_DrawRectOutline(BAT_BAR_X, BAT_BAR_Y, BAT_BAR_W, BAT_BAR_H, COLOR_WHITE);
  ST7735_FillRect(BAT_BAR_X + 2, BAT_BAR_Y + 2, BAT_BAR_W - 4, BAT_BAR_H - 4, COLOR_DARKGRAY);
  if (present)
  {
    uint16_t barColor = COLOR_GREEN;
    uint16_t fillW = (uint16_t)(((BAT_BAR_W - 4) * percent) / 100U);

    if (percent < 20) { barColor = COLOR_RED; }
    else if (percent < 50) { barColor = COLOR_YELLOW; }

    if (fillW > 0)
    {
      ST7735_FillRect(BAT_BAR_X + 2, BAT_BAR_Y + 2, fillW, BAT_BAR_H - 4, barColor);
    }
  }

  /* Line 1: computed battery voltage (always shown, even when 'absent', for debug).
     Trailing spaces clear any leftover digits from a previous, longer value. */
  snprintf(line, sizeof(line), "%uMV  ", battMv);
  ST7735_DrawString(4, TEXT_LINE1_Y, line, COLOR_WHITE, COLOR_BLACK, 2);

  /* Line 2: charge percent, or NO BAT when the reading is out of plausible range */
  if (present)
  {
    snprintf(line, sizeof(line), "%u%%    ", percent);
    ST7735_DrawString(4, TEXT_LINE2_Y, line, COLOR_WHITE, COLOR_BLACK, 2);
  }
  else
  {
    ST7735_DrawString(4, TEXT_LINE2_Y, "NO BAT", COLOR_ORANGE, COLOR_BLACK, 2);
  }

  /* Line 3 (small): raw voltage the ADC pin actually sees, for divider debugging */
  snprintf(line, sizeof(line), "ADC PIN:%umV  ", pinMv);
  ST7735_DrawString(4, TEXT_LINE3_Y, line, COLOR_GRAY, COLOR_BLACK, 1);

  /* Status squares with labels */
  ST7735_FillRect(CHG_SQ_X, SQ_Y, SQ_SZ, SQ_SZ, chg ? COLOR_GREEN : COLOR_DARKGRAY);
  ST7735_FillRect(USB_SQ_X, SQ_Y, SQ_SZ, SQ_SZ, usb ? COLOR_BLUE : COLOR_DARKGRAY);
  ST7735_DrawString(CHG_SQ_X - 2, SQ_Y + SQ_SZ + 2, "CHG", COLOR_WHITE, COLOR_BLACK, 1);
  ST7735_DrawString(USB_SQ_X - 2, SQ_Y + SQ_SZ + 2, "USB", COLOR_WHITE, COLOR_BLACK, 1);
}

static void MX_SPI1_Init(void)
{
  GPIO_InitTypeDef gi = {0};

  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_SPI1_CLK_ENABLE();

  gi.Pin = DISP_SCK_PIN | DISP_MOSI_PIN;
  gi.Mode = GPIO_MODE_AF_PP;
  gi.Pull = GPIO_NOPULL;
  gi.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  gi.Alternate = GPIO_AF5_SPI1;
  HAL_GPIO_Init(GPIOA, &gi);

  gi.Pin = DISP_CS_PIN | DISP_BLK_PIN;
  gi.Mode = GPIO_MODE_OUTPUT_PP;
  gi.Pull = GPIO_NOPULL;
  gi.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOA, &gi);
  HAL_GPIO_WritePin(GPIOA, DISP_CS_PIN, GPIO_PIN_SET);
  HAL_GPIO_WritePin(GPIOA, DISP_BLK_PIN, GPIO_PIN_RESET);

  gi.Pin = DISP_DC_PIN | DISP_RST_PIN;
  gi.Mode = GPIO_MODE_OUTPUT_PP;
  gi.Pull = GPIO_NOPULL;
  gi.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &gi);

  hspi1.Instance = SPI1;
  hspi1.Init.Mode = SPI_MODE_MASTER;
  hspi1.Init.Direction = SPI_DIRECTION_2LINES;
  hspi1.Init.DataSize = SPI_DATASIZE_8BIT;
  hspi1.Init.CLKPolarity = SPI_POLARITY_LOW;
  hspi1.Init.CLKPhase = SPI_PHASE_1EDGE;
  hspi1.Init.NSS = SPI_NSS_SOFT;
  hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_8;
  hspi1.Init.FirstBit = SPI_FIRSTBIT_MSB;
  hspi1.Init.TIMode = SPI_TIMODE_DISABLE;
  hspi1.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
  HAL_SPI_Init(&hspi1);
  __HAL_SPI_ENABLE(&hspi1);
}

static void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 4;
  RCC_OscInitStruct.PLL.PLLN = 96;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = 4;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK
                               | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;
  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_3) != HAL_OK)
  {
    Error_Handler();
  }
}

static void MX_GPIO_Init(void)
{
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
}

void Error_Handler(void)
{
  __disable_irq();
  while (1)
  {
  }
}

void SysTick_Handler(void)
{
  HAL_IncTick();
}
