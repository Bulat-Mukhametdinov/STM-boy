#include "st7735.h"
#include "pinmap.h"
#include "font5x7.h"
#include <string.h>

/* ST7735S physical GDDRAM is 132 x 162; the visible panel is a window inside it. */
#define GDDRAM_COLS 132
#define GDDRAM_ROWS 162

/* Calibrated from observed panel gap: 3px missing on the CASET (x) axis,
   1px missing on the RASET (y) axis, measured in the panel's native portrait mode. */
#define COLSTART 3
#define ROWSTART 1

/* MADCTL: MY|MV -> landscape, X along the wide (160px) side, RGB colour order.
   Bit3 (BGR) cleared: this panel is RGB, so leaving it set swapped red<->blue.
   If the image comes up mirrored or the offset gap is on the wrong edge,
   try 0x60 (MX|MV) instead. */
#define MADCTL_VALUE 0xA0

extern SPI_HandleTypeDef hspi1;

static void CS_LOW(void)  { HAL_GPIO_WritePin(DISP_CS_PORT, DISP_CS_PIN, GPIO_PIN_RESET); }
static void CS_HIGH(void) { HAL_GPIO_WritePin(DISP_CS_PORT, DISP_CS_PIN, GPIO_PIN_SET); }
static void DC_CMD(void)  { HAL_GPIO_WritePin(DISP_DC_PORT, DISP_DC_PIN, GPIO_PIN_RESET); }
static void DC_DATA(void) { HAL_GPIO_WritePin(DISP_DC_PORT, DISP_DC_PIN, GPIO_PIN_SET); }

static void WriteCmd(uint8_t cmd)
{
  DC_CMD();
  CS_LOW();
  HAL_SPI_Transmit(&hspi1, &cmd, 1, HAL_MAX_DELAY);
  CS_HIGH();
}

static void WriteData(const uint8_t *data, uint16_t len)
{
  DC_DATA();
  CS_LOW();
  HAL_SPI_Transmit(&hspi1, (uint8_t *)data, len, HAL_MAX_DELAY);
  CS_HIGH();
}

static void WriteData1(uint8_t d)
{
  WriteData(&d, 1);
}

void ST7735_BacklightOn(void)
{
  HAL_GPIO_WritePin(DISP_BLK_PORT, DISP_BLK_PIN, GPIO_PIN_SET);
}

static void SetAddrWindow(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
  uint8_t buf[4];

  x0 += COLSTART; x1 += COLSTART;
  y0 += ROWSTART; y1 += ROWSTART;

  WriteCmd(0x2A); /* CASET */
  buf[0] = x0 >> 8; buf[1] = x0 & 0xFF; buf[2] = x1 >> 8; buf[3] = x1 & 0xFF;
  WriteData(buf, 4);

  WriteCmd(0x2B); /* RASET */
  buf[0] = y0 >> 8; buf[1] = y0 & 0xFF; buf[2] = y1 >> 8; buf[3] = y1 & 0xFF;
  WriteData(buf, 4);

  WriteCmd(0x2C); /* RAMWR */
}

/* Blank the entire physical GDDRAM (ignoring the visible-area offset) so that
   any pixels outside the drawn window read black instead of power-on garbage. */
static void RawClearBlack(void)
{
  uint8_t buf[4];
  uint8_t line[GDDRAM_COLS * 2];

  WriteCmd(0x2A); /* CASET: full column range */
  buf[0] = 0; buf[1] = 0; buf[2] = 0; buf[3] = GDDRAM_COLS - 1;
  WriteData(buf, 4);

  WriteCmd(0x2B); /* RASET: full row range */
  buf[0] = 0; buf[1] = 0; buf[2] = 0; buf[3] = GDDRAM_ROWS - 1;
  WriteData(buf, 4);

  WriteCmd(0x2C); /* RAMWR */

  memset(line, 0, sizeof(line));
  DC_DATA();
  CS_LOW();
  for (uint16_t r = 0; r < GDDRAM_ROWS; r++)
  {
    HAL_SPI_Transmit(&hspi1, line, sizeof(line), HAL_MAX_DELAY);
  }
  CS_HIGH();
}

void ST7735_Init(void)
{
  HAL_GPIO_WritePin(DISP_RST_PORT, DISP_RST_PIN, GPIO_PIN_RESET);
  HAL_Delay(20);
  HAL_GPIO_WritePin(DISP_RST_PORT, DISP_RST_PIN, GPIO_PIN_SET);
  HAL_Delay(150);

  WriteCmd(0x01); /* SWRESET */
  HAL_Delay(150);
  WriteCmd(0x11); /* SLPOUT */
  HAL_Delay(255);

  WriteCmd(0xB1); /* FRMCTR1 */
  WriteData1(0x01); WriteData1(0x2C); WriteData1(0x2D);
  WriteCmd(0xB2); /* FRMCTR2 */
  WriteData1(0x01); WriteData1(0x2C); WriteData1(0x2D);
  WriteCmd(0xB3); /* FRMCTR3 */
  WriteData1(0x01); WriteData1(0x2C); WriteData1(0x2D);
  WriteData1(0x01); WriteData1(0x2C); WriteData1(0x2D);

  WriteCmd(0xB4); /* INVCTR */
  WriteData1(0x07);

  WriteCmd(0xC0); /* PWCTR1 */
  WriteData1(0xA2); WriteData1(0x02); WriteData1(0x84);
  WriteCmd(0xC1); /* PWCTR2 */
  WriteData1(0xC5);
  WriteCmd(0xC2); /* PWCTR3 */
  WriteData1(0x0A); WriteData1(0x00);
  WriteCmd(0xC3); /* PWCTR4 */
  WriteData1(0x8A); WriteData1(0x2A);
  WriteCmd(0xC4); /* PWCTR5 */
  WriteData1(0x8A); WriteData1(0xEE);

  WriteCmd(0xC5); /* VMCTR1 */
  WriteData1(0x0E);

  WriteCmd(0x20); /* INVOFF */

  WriteCmd(0x36); /* MADCTL */
  WriteData1(MADCTL_VALUE);

  WriteCmd(0x3A); /* COLMOD */
  WriteData1(0x05); /* 16-bit color */

  WriteCmd(0x13); /* NORON */
  HAL_Delay(10);

  WriteCmd(0x29); /* DISPON */
  HAL_Delay(100);

  RawClearBlack();
}

void ST7735_FillRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color)
{
  uint8_t line[2 * ST7735_WIDTH];
  uint32_t i;

  if ((x >= ST7735_WIDTH) || (y >= ST7735_HEIGHT))
  {
    return;
  }
  if ((x + w) > ST7735_WIDTH)  { w = ST7735_WIDTH - x; }
  if ((y + h) > ST7735_HEIGHT) { h = ST7735_HEIGHT - y; }

  SetAddrWindow(x, y, x + w - 1, y + h - 1);

  for (i = 0; i < w; i++)
  {
    line[2 * i] = color >> 8;
    line[2 * i + 1] = color & 0xFF;
  }

  DC_DATA();
  CS_LOW();
  for (i = 0; i < h; i++)
  {
    HAL_SPI_Transmit(&hspi1, line, 2 * w, HAL_MAX_DELAY);
  }
  CS_HIGH();
}

void ST7735_FillScreen(uint16_t color)
{
  ST7735_FillRect(0, 0, ST7735_WIDTH, ST7735_HEIGHT, color);
}

void ST7735_DrawRectOutline(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color)
{
  ST7735_FillRect(x, y, w, 2, color);
  ST7735_FillRect(x, y + h - 2, w, 2, color);
  ST7735_FillRect(x, y, 2, h, color);
  ST7735_FillRect(x + w - 2, y, 2, h, color);
}

void ST7735_DrawChar(uint16_t x, uint16_t y, char c, uint16_t fg, uint16_t bg, uint8_t scale)
{
  const uint8_t *glyph = Font5x7_GetGlyph(c);
  uint16_t cw = 5 * scale;
  uint16_t ch = 7 * scale;
  static uint8_t line[2 * 5 * 8]; /* up to scale 8 wide */

  if (scale > 8) { scale = 8; }

  if ((x >= ST7735_WIDTH) || (y >= ST7735_HEIGHT)) { return; }
  if ((x + cw) > ST7735_WIDTH)  { cw = ST7735_WIDTH - x; }
  if ((y + ch) > ST7735_HEIGHT) { ch = ST7735_HEIGHT - y; }

  SetAddrWindow(x, y, x + cw - 1, y + ch - 1);

  DC_DATA();
  CS_LOW();
  for (uint16_t row = 0; row < ch; row++)
  {
    uint8_t fontRow = row / scale;
    uint16_t idx = 0;

    for (uint16_t col = 0; col < cw; col++)
    {
      uint8_t fontCol = col / scale;
      uint8_t bit = (glyph[fontCol] >> fontRow) & 0x01;
      uint16_t px = bit ? fg : bg;

      line[idx++] = px >> 8;
      line[idx++] = px & 0xFF;
    }
    HAL_SPI_Transmit(&hspi1, line, idx, HAL_MAX_DELAY);
  }
  CS_HIGH();
}

void ST7735_DrawString(uint16_t x, uint16_t y, const char *s, uint16_t fg, uint16_t bg, uint8_t scale)
{
  uint16_t cx = x;
  uint16_t step = (uint16_t)((5 * scale) + scale);

  while (*s)
  {
    ST7735_DrawChar(cx, y, *s, fg, bg, scale);
    cx += step;
    s++;
  }
}
