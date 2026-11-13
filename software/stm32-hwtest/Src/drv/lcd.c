/* ST7735S driver, register-level SPI1. Init sequence from stm32-bringup
   Src/st7735.c; optional gamma table from stm32-retroport Core/Src/st7735.c. */

#include "lcd.h"
#include "board.h"
#include "stm32f4xx_hal.h"

#define SPI_WAIT_LIMIT 200000u

lcd_cfg_t lcd_cfg;

static uint16_t s_w = 160, s_h = 128;

void lcd_cfg_defaults(lcd_cfg_t *c)
{
    c->madctl = LCD_MADCTL_MY | LCD_MADCTL_MV; /* 0xA0 */
    c->xoff = 3;
    c->yoff = 1;
    c->bgr = 0;
    c->invert = 0;
    c->gamma_id = 0;
    c->spi_div = 8;
}

/* ---------------------------------------------------------------- SPI1 */

static inline void spi_tx8(uint8_t b)
{
    while (!(SPI1->SR & SPI_SR_TXE)) {
    }
    *(volatile uint8_t *)&SPI1->DR = b;
}

static void spi_wait_idle(void)
{
    uint32_t n = 0;
    while (!(SPI1->SR & SPI_SR_TXE) && ++n < SPI_WAIT_LIMIT) {
    }
    n = 0;
    while ((SPI1->SR & SPI_SR_BSY) && ++n < SPI_WAIT_LIMIT) {
    }
}

static uint32_t div_to_br(uint16_t div)
{
    uint32_t br = 0;
    uint32_t d = 2;
    while (d < div && br < 7u) {
        d <<= 1;
        br++;
    }
    return br;
}

static void spi1_setup(uint16_t div)
{
    RCC->APB2ENR |= RCC_APB2ENR_SPI1EN;
    (void)RCC->APB2ENR;
    if (SPI1->CR1 & SPI_CR1_SPE) {
        spi_wait_idle(); /* let the last frame finish before disabling */
    }
    SPI1->CR1 = 0; /* disable before changing BR */
    SPI1->CR2 = 0;
    SPI1->CR1 = SPI_CR1_BIDIMODE | SPI_CR1_BIDIOE | SPI_CR1_MSTR | SPI_CR1_SSM |
                SPI_CR1_SSI | (div_to_br(div) << SPI_CR1_BR_Pos);
    SPI1->CR1 |= SPI_CR1_SPE;
}

void lcd_set_spi_div(uint16_t div)
{
    lcd_cfg.spi_div = (uint16_t)(2u << div_to_br(div));
    spi1_setup(lcd_cfg.spi_div);
}

uint32_t lcd_spi_hz(void)
{
    return HAL_RCC_GetPCLK2Freq() / lcd_cfg.spi_div;
}

/* ---------------------------------------------------------------- pins */

#define CS_LOW()  gpio_clr(LCD_CS_PORT, LCD_CS_PIN)
#define CS_HIGH() gpio_set(LCD_CS_PORT, LCD_CS_PIN)
#define DC_CMD()  gpio_clr(LCD_DC_PORT, LCD_DC_PIN)
#define DC_DATA() gpio_set(LCD_DC_PORT, LCD_DC_PIN)

static void cmd(uint8_t c)
{
    spi_wait_idle(); /* previous data must be fully shifted out before DC flips */
    DC_CMD();
    CS_LOW();
    spi_tx8(c);
    spi_wait_idle();
    DC_DATA();
}

static void data8(uint8_t d) { spi_tx8(d); }

static void cmd_data(uint8_t c, const uint8_t *d, uint32_t n)
{
    cmd(c);
    for (uint32_t i = 0; i < n; i++) {
        spi_tx8(d[i]);
    }
}

void lcd_end(void)
{
    spi_wait_idle();
    CS_HIGH();
}

void lcd_backlight(bool on)
{
    if (on) {
        gpio_set(LCD_BLK_PORT, LCD_BLK_PIN);
    } else {
        gpio_clr(LCD_BLK_PORT, LCD_BLK_PIN);
    }
}

static void update_geometry(void)
{
    if (lcd_cfg.madctl & LCD_MADCTL_MV) {
        s_w = 160;
        s_h = 128;
    } else {
        s_w = 128;
        s_h = 160;
    }
}

uint16_t lcd_width(void) { return s_w; }
uint16_t lcd_height(void) { return s_h; }

static uint8_t madctl_value(void)
{
    return (uint8_t)((lcd_cfg.madctl & ~LCD_MADCTL_BGR) | (lcd_cfg.bgr ? LCD_MADCTL_BGR : 0u));
}

static const uint8_t s_gamma_pos[16] = {0x02, 0x1C, 0x07, 0x12, 0x37, 0x32, 0x29, 0x2D,
                                        0x29, 0x25, 0x2B, 0x39, 0x00, 0x01, 0x03, 0x10};
static const uint8_t s_gamma_neg[16] = {0x03, 0x1D, 0x07, 0x06, 0x2E, 0x2C, 0x29, 0x2D,
                                        0x2E, 0x2E, 0x37, 0x3F, 0x00, 0x00, 0x02, 0x10};

static void send_window_raw(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
    uint8_t b[4];
    b[0] = (uint8_t)(x0 >> 8); b[1] = (uint8_t)x0; b[2] = (uint8_t)(x1 >> 8); b[3] = (uint8_t)x1;
    cmd_data(0x2A, b, 4); /* CASET */
    b[0] = (uint8_t)(y0 >> 8); b[1] = (uint8_t)y0; b[2] = (uint8_t)(y1 >> 8); b[3] = (uint8_t)y1;
    cmd_data(0x2B, b, 4); /* RASET */
    cmd(0x2C);            /* RAMWR, CS stays low, DC = data */
}

void lcd_clear_gddram(uint16_t color)
{
    /* MADCTL 0 so the window maps 1:1 onto the physical 132x162 RAM. */
    uint8_t m = 0x00;
    cmd_data(0x36, &m, 1);
    send_window_raw(0, 0, LCD_GDDRAM_W - 1u, LCD_GDDRAM_H - 1u);
    lcd_push_color(color, (uint32_t)LCD_GDDRAM_W * LCD_GDDRAM_H);
    m = madctl_value();
    cmd_data(0x36, &m, 1);
    lcd_end();
}

void lcd_apply_cfg(void)
{
    spi1_setup(lcd_cfg.spi_div);
    uint8_t m = madctl_value();
    cmd_data(0x36, &m, 1);
    cmd(lcd_cfg.invert ? 0x21 : 0x20);
    if (lcd_cfg.gamma_id == 1u) {
        cmd_data(0xE0, s_gamma_pos, sizeof(s_gamma_pos));
        cmd_data(0xE1, s_gamma_neg, sizeof(s_gamma_neg));
    }
    lcd_end();
    update_geometry();
}

void lcd_init(void)
{
    /* GPIO: SCK/MOSI AF5, CS/DC/RST/BLK outputs. CS high is preset by board_init. */
    gpio_config(LCD_SCK_PORT, LCD_SCK_PIN, GPIO_MODE_AF_, GPIO_PULL_NONE_, GPIO_SPEED_VHIGH_, 5);
    gpio_config(LCD_MOSI_PORT, LCD_MOSI_PIN, GPIO_MODE_AF_, GPIO_PULL_NONE_, GPIO_SPEED_VHIGH_, 5);
    gpio_set(LCD_CS_PORT, LCD_CS_PIN);
    gpio_config(LCD_CS_PORT, LCD_CS_PIN, GPIO_MODE_OUT_, GPIO_PULL_NONE_, GPIO_SPEED_VHIGH_, 0);
    gpio_set(LCD_DC_PORT, LCD_DC_PIN);
    gpio_config(LCD_DC_PORT, LCD_DC_PIN, GPIO_MODE_OUT_, GPIO_PULL_NONE_, GPIO_SPEED_VHIGH_, 0);
    gpio_config(LCD_RST_PORT, LCD_RST_PIN, GPIO_MODE_OUT_, GPIO_PULL_NONE_, GPIO_SPEED_LOW_, 0);
    gpio_clr(LCD_BLK_PORT, LCD_BLK_PIN);
    gpio_config(LCD_BLK_PORT, LCD_BLK_PIN, GPIO_MODE_OUT_, GPIO_PULL_NONE_, GPIO_SPEED_LOW_, 0);

    spi1_setup(lcd_cfg.spi_div);

    gpio_clr(LCD_RST_PORT, LCD_RST_PIN);
    HAL_Delay(20);
    gpio_set(LCD_RST_PORT, LCD_RST_PIN);
    HAL_Delay(150);

    cmd(0x01); /* SWRESET */
    lcd_end();
    HAL_Delay(150);
    cmd(0x11); /* SLPOUT */
    lcd_end();
    HAL_Delay(255);

    static const uint8_t frm[6] = {0x01, 0x2C, 0x2D, 0x01, 0x2C, 0x2D};
    cmd_data(0xB1, frm, 3);           /* FRMCTR1 */
    cmd_data(0xB2, frm, 3);           /* FRMCTR2 */
    cmd_data(0xB3, frm, 6);           /* FRMCTR3 */
    cmd(0xB4); data8(0x07);           /* INVCTR */
    static const uint8_t pw1[3] = {0xA2, 0x02, 0x84};
    cmd_data(0xC0, pw1, 3);           /* PWCTR1 */
    cmd(0xC1); data8(0xC5);           /* PWCTR2 */
    static const uint8_t pw3[2] = {0x0A, 0x00};
    cmd_data(0xC2, pw3, 2);           /* PWCTR3 */
    static const uint8_t pw4[2] = {0x8A, 0x2A};
    cmd_data(0xC3, pw4, 2);           /* PWCTR4 */
    static const uint8_t pw5[2] = {0x8A, 0xEE};
    cmd_data(0xC4, pw5, 2);           /* PWCTR5 */
    cmd(0xC5); data8(0x0E);           /* VMCTR1 */
    cmd(lcd_cfg.invert ? 0x21 : 0x20);/* INVOFF/INVON */
    cmd(0x3A); data8(0x05);           /* COLMOD 16 bpp */
    if (lcd_cfg.gamma_id == 1u) {
        cmd_data(0xE0, s_gamma_pos, sizeof(s_gamma_pos));
        cmd_data(0xE1, s_gamma_neg, sizeof(s_gamma_neg));
    }
    cmd(0x13);                        /* NORON */
    lcd_end();
    HAL_Delay(10);
    cmd(0x29);                        /* DISPON */
    lcd_end();
    HAL_Delay(100);

    /* Clear the full 132x162 RAM (MADCTL 0), then set the real MADCTL. */
    lcd_clear_gddram(0x0000);
    update_geometry();
}

void lcd_set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
    send_window_raw((uint16_t)(x0 + lcd_cfg.xoff), (uint16_t)(y0 + lcd_cfg.yoff),
                    (uint16_t)(x1 + lcd_cfg.xoff), (uint16_t)(y1 + lcd_cfg.yoff));
}

void lcd_push_pixels(const uint16_t *px, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++) {
        uint16_t c = px[i];
        spi_tx8((uint8_t)(c >> 8));
        spi_tx8((uint8_t)c);
    }
}

void lcd_push_color(uint16_t color, uint32_t n)
{
    uint8_t hi = (uint8_t)(color >> 8), lo = (uint8_t)color;
    for (uint32_t i = 0; i < n; i++) {
        spi_tx8(hi);
        spi_tx8(lo);
    }
}

void lcd_fill_rect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color)
{
    int32_t x0 = x, y0 = y, x1 = (int32_t)x + w, y1 = (int32_t)y + h;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > (int32_t)s_w) x1 = s_w;
    if (y1 > (int32_t)s_h) y1 = s_h;
    if (x1 <= x0 || y1 <= y0) {
        return;
    }
    lcd_set_window((uint16_t)x0, (uint16_t)y0, (uint16_t)(x1 - 1), (uint16_t)(y1 - 1));
    lcd_push_color(color, (uint32_t)(x1 - x0) * (uint32_t)(y1 - y0));
    lcd_end();
}
