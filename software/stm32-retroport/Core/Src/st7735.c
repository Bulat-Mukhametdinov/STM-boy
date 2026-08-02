#include "st7735.h"

#include "display_config.h"

#define ST7735_SWRESET 0x01
#define ST7735_SLPOUT 0x11
#define ST7735_INVOFF 0x20
#define ST7735_INVON 0x21
#define ST7735_GAMSET 0x26
#define ST7735_COLMOD 0x3A
#define ST7735_CMD_MADCTL 0x36
#define ST7735_CASET 0x2A
#define ST7735_RASET 0x2B
#define ST7735_RAMWR 0x2C
#define ST7735_NORON 0x13
#define ST7735_DISPON 0x29
#define ST7735_FRMCTR1 0xB1
#define ST7735_FRMCTR2 0xB2
#define ST7735_FRMCTR3 0xB3
#define ST7735_INVCTR 0xB4
#define ST7735_PWCTR1 0xC0
#define ST7735_PWCTR2 0xC1
#define ST7735_PWCTR3 0xC2
#define ST7735_PWCTR4 0xC3
#define ST7735_PWCTR5 0xC4
#define ST7735_VMCTR1 0xC5
#define ST7735_GMCTRP1 0xE0
#define ST7735_GMCTRN1 0xE1

static SPI_HandleTypeDef hspi_st7735;

static void st7735_select(void)
{
    HAL_GPIO_WritePin(ST7735_CS_GPIO_PORT, ST7735_CS_PIN, GPIO_PIN_RESET);
}

static void st7735_unselect(void)
{
    HAL_GPIO_WritePin(ST7735_CS_GPIO_PORT, ST7735_CS_PIN, GPIO_PIN_SET);
}

static void st7735_command_mode(void)
{
    HAL_GPIO_WritePin(ST7735_DC_GPIO_PORT, ST7735_DC_PIN, GPIO_PIN_RESET);
}

static void st7735_data_mode(void)
{
    HAL_GPIO_WritePin(ST7735_DC_GPIO_PORT, ST7735_DC_PIN, GPIO_PIN_SET);
}

static HAL_StatusTypeDef st7735_write_bytes(const uint8_t *data, size_t length)
{
    while (length > 0U) {
        uint16_t chunk = length > 0xFFFFU ? 0xFFFFU : (uint16_t)length;
        HAL_StatusTypeDef status = HAL_SPI_Transmit(&hspi_st7735, (uint8_t *)data, chunk, HAL_MAX_DELAY);
        if (status != HAL_OK) {
            return status;
        }

        data += chunk;
        length -= chunk;
    }

    return HAL_OK;
}

static HAL_StatusTypeDef st7735_write_command(uint8_t command)
{
    st7735_command_mode();
    return st7735_write_bytes(&command, 1U);
}

static HAL_StatusTypeDef st7735_write_data(const uint8_t *data, size_t length)
{
    st7735_data_mode();
    return st7735_write_bytes(data, length);
}

static HAL_StatusTypeDef st7735_write_command_data(uint8_t command, const uint8_t *data, size_t length)
{
    HAL_StatusTypeDef status = st7735_write_command(command);
    if (status != HAL_OK || length == 0U) {
        return status;
    }

    return st7735_write_data(data, length);
}

static HAL_StatusTypeDef st7735_set_address_window(uint16_t x, uint16_t y, uint16_t width, uint16_t height)
{
    uint16_t x1 = x + ST7735_X_SHIFT;
    uint16_t y1 = y + ST7735_Y_SHIFT;
    uint16_t x2 = x1 + width - 1U;
    uint16_t y2 = y1 + height - 1U;
    uint8_t data[4];

    HAL_StatusTypeDef status = st7735_write_command(ST7735_CASET);
    if (status != HAL_OK) {
        return status;
    }

    data[0] = (uint8_t)(x1 >> 8);
    data[1] = (uint8_t)x1;
    data[2] = (uint8_t)(x2 >> 8);
    data[3] = (uint8_t)x2;
    status = st7735_write_data(data, sizeof(data));
    if (status != HAL_OK) {
        return status;
    }

    status = st7735_write_command(ST7735_RASET);
    if (status != HAL_OK) {
        return status;
    }

    data[0] = (uint8_t)(y1 >> 8);
    data[1] = (uint8_t)y1;
    data[2] = (uint8_t)(y2 >> 8);
    data[3] = (uint8_t)y2;
    status = st7735_write_data(data, sizeof(data));
    if (status != HAL_OK) {
        return status;
    }

    return st7735_write_command(ST7735_RAMWR);
}

static void st7735_enable_gpio_clock(GPIO_TypeDef *port)
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

static void st7735_gpio_output_init(GPIO_TypeDef *port, uint16_t pin)
{
    GPIO_InitTypeDef gpio = {0};

    st7735_enable_gpio_clock(port);

    gpio.Pin = pin;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(port, &gpio);
}

static void st7735_gpio_spi_init(GPIO_TypeDef *port, uint16_t pin)
{
    GPIO_InitTypeDef gpio = {0};

    st7735_enable_gpio_clock(port);

    gpio.Pin = pin;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio.Alternate = ST7735_SPI_AF;
    HAL_GPIO_Init(port, &gpio);
}

static HAL_StatusTypeDef st7735_gpio_init(void)
{
    if (ST7735_SPI_INSTANCE == SPI1) {
        __HAL_RCC_SPI1_CLK_ENABLE();
    } else if (ST7735_SPI_INSTANCE == SPI2) {
        __HAL_RCC_SPI2_CLK_ENABLE();
    } else if (ST7735_SPI_INSTANCE == SPI3) {
        __HAL_RCC_SPI3_CLK_ENABLE();
    }

    st7735_gpio_output_init(ST7735_CS_GPIO_PORT, ST7735_CS_PIN);
    st7735_gpio_output_init(ST7735_DC_GPIO_PORT, ST7735_DC_PIN);
    st7735_gpio_output_init(ST7735_RST_GPIO_PORT, ST7735_RST_PIN);
    st7735_gpio_output_init(ST7735_BL_GPIO_PORT, ST7735_BL_PIN);

    HAL_GPIO_WritePin(ST7735_CS_GPIO_PORT, ST7735_CS_PIN, GPIO_PIN_SET);
    HAL_GPIO_WritePin(ST7735_DC_GPIO_PORT, ST7735_DC_PIN, GPIO_PIN_SET);
    HAL_GPIO_WritePin(ST7735_RST_GPIO_PORT, ST7735_RST_PIN, GPIO_PIN_SET);
    HAL_GPIO_WritePin(ST7735_BL_GPIO_PORT, ST7735_BL_PIN, ST7735_BL_ACTIVE_STATE);

    st7735_gpio_spi_init(ST7735_SCK_GPIO_PORT, ST7735_SCK_PIN);
    st7735_gpio_spi_init(ST7735_MOSI_GPIO_PORT, ST7735_MOSI_PIN);

    return HAL_OK;
}

static HAL_StatusTypeDef st7735_spi_init(void)
{
    hspi_st7735.Instance = ST7735_SPI_INSTANCE;
    hspi_st7735.Init.Mode = SPI_MODE_MASTER;
    hspi_st7735.Init.Direction = SPI_DIRECTION_2LINES;
    hspi_st7735.Init.DataSize = SPI_DATASIZE_8BIT;
    hspi_st7735.Init.CLKPolarity = SPI_POLARITY_LOW;
    hspi_st7735.Init.CLKPhase = SPI_PHASE_1EDGE;
    hspi_st7735.Init.NSS = SPI_NSS_SOFT;
    hspi_st7735.Init.BaudRatePrescaler = ST7735_SPI_BAUDRATE_PRESCALER;
    hspi_st7735.Init.FirstBit = SPI_FIRSTBIT_MSB;
    hspi_st7735.Init.TIMode = SPI_TIMODE_DISABLE;
    hspi_st7735.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
    hspi_st7735.Init.CRCPolynomial = 10;

    return HAL_SPI_Init(&hspi_st7735);
}

HAL_StatusTypeDef ST7735_Init(void)
{
    static const uint8_t frame_control[] = {0x01, 0x2C, 0x2D};
    static const uint8_t frame_control_partial[] = {0x01, 0x2C, 0x2D, 0x01, 0x2C, 0x2D};
    static const uint8_t power_control_1[] = {0xA2, 0x02, 0x84};
    static const uint8_t power_control_3[] = {0x0A, 0x00};
    static const uint8_t power_control_4[] = {0x8A, 0x2A};
    static const uint8_t power_control_5[] = {0x8A, 0xEE};
    static const uint8_t gamma_positive[] = {
        0x02, 0x1C, 0x07, 0x12,
        0x37, 0x32, 0x29, 0x2D,
        0x29, 0x25, 0x2B, 0x39,
        0x00, 0x01, 0x03, 0x10
    };
    static const uint8_t gamma_negative[] = {
        0x03, 0x1D, 0x07, 0x06,
        0x2E, 0x2C, 0x29, 0x2D,
        0x2E, 0x2E, 0x37, 0x3F,
        0x00, 0x00, 0x02, 0x10
    };

    HAL_StatusTypeDef status = st7735_gpio_init();
    if (status != HAL_OK) {
        return status;
    }

    status = st7735_spi_init();
    if (status != HAL_OK) {
        return status;
    }

    HAL_GPIO_WritePin(ST7735_RST_GPIO_PORT, ST7735_RST_PIN, GPIO_PIN_RESET);
    HAL_Delay(20);
    HAL_GPIO_WritePin(ST7735_RST_GPIO_PORT, ST7735_RST_PIN, GPIO_PIN_SET);
    HAL_Delay(120);

    st7735_select();

    status = st7735_write_command(ST7735_SWRESET);
    if (status == HAL_OK) {
        HAL_Delay(150);
        status = st7735_write_command(ST7735_SLPOUT);
    }
    if (status == HAL_OK) {
        HAL_Delay(500);
        status = st7735_write_command_data(ST7735_FRMCTR1, frame_control, sizeof(frame_control));
    }
    if (status == HAL_OK) {
        status = st7735_write_command_data(ST7735_FRMCTR2, frame_control, sizeof(frame_control));
    }
    if (status == HAL_OK) {
        status = st7735_write_command_data(ST7735_FRMCTR3, frame_control_partial, sizeof(frame_control_partial));
    }
    if (status == HAL_OK) {
        uint8_t inversion_control = 0x07;
        status = st7735_write_command_data(ST7735_INVCTR, &inversion_control, 1U);
    }
    if (status == HAL_OK) {
        status = st7735_write_command_data(ST7735_PWCTR1, power_control_1, sizeof(power_control_1));
    }
    if (status == HAL_OK) {
        uint8_t power_control_2 = 0xC5;
        status = st7735_write_command_data(ST7735_PWCTR2, &power_control_2, 1U);
    }
    if (status == HAL_OK) {
        status = st7735_write_command_data(ST7735_PWCTR3, power_control_3, sizeof(power_control_3));
    }
    if (status == HAL_OK) {
        status = st7735_write_command_data(ST7735_PWCTR4, power_control_4, sizeof(power_control_4));
    }
    if (status == HAL_OK) {
        status = st7735_write_command_data(ST7735_PWCTR5, power_control_5, sizeof(power_control_5));
    }
    if (status == HAL_OK) {
        uint8_t vcom_control = 0x0E;
        status = st7735_write_command_data(ST7735_VMCTR1, &vcom_control, 1U);
    }
    if (status == HAL_OK) {
        status = st7735_write_command(ST7735_INVOFF);
    }
    if (status == HAL_OK) {
        uint8_t madctl = ST7735_MADCTL;
        status = st7735_write_command(ST7735_CMD_MADCTL);
        if (status == HAL_OK) {
            status = st7735_write_data(&madctl, 1U);
        }
    }
    if (status == HAL_OK) {
        uint8_t color_mode = 0x05;
        status = st7735_write_command_data(ST7735_COLMOD, &color_mode, 1U);
    }
    if (status == HAL_OK) {
        status = st7735_write_command_data(ST7735_GMCTRP1, gamma_positive, sizeof(gamma_positive));
    }
    if (status == HAL_OK) {
        status = st7735_write_command_data(ST7735_GMCTRN1, gamma_negative, sizeof(gamma_negative));
    }
    if (status == HAL_OK) {
        status = st7735_write_command(ST7735_NORON);
    }
    if (status == HAL_OK) {
        HAL_Delay(10);
        status = st7735_write_command(ST7735_DISPON);
    }

    st7735_unselect();

    if (status == HAL_OK) {
        ST7735_SetBacklight(1U);
    }

    return status;
}

void ST7735_SetBacklight(uint8_t enabled)
{
    GPIO_PinState active = ST7735_BL_ACTIVE_STATE;
    GPIO_PinState inactive = active == GPIO_PIN_SET ? GPIO_PIN_RESET : GPIO_PIN_SET;

    HAL_GPIO_WritePin(ST7735_BL_GPIO_PORT, ST7735_BL_PIN, enabled ? active : inactive);
}

HAL_StatusTypeDef ST7735_WritePixels(uint16_t x, uint16_t y, uint16_t width, uint16_t height,
                                     const uint8_t *pixels, size_t length)
{
    if (pixels == NULL || width == 0U || height == 0U) {
        return HAL_ERROR;
    }

    st7735_select();
    HAL_StatusTypeDef status = st7735_set_address_window(x, y, width, height);
    if (status == HAL_OK) {
        status = st7735_write_data(pixels, length);
    }
    st7735_unselect();

    return status;
}

HAL_StatusTypeDef ST7735_FillScreen(uint16_t color)
{
    return ST7735_FillRect(0U, 0U, ST7735_WIDTH, ST7735_HEIGHT, color);
}

HAL_StatusTypeDef ST7735_FillRect(uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint16_t color)
{
    uint8_t line[ST7735_WIDTH * 2U];
    uint8_t hi = (uint8_t)(color >> 8);
    uint8_t lo = (uint8_t)color;

    if (width == 0U || height == 0U || width > ST7735_WIDTH || height > ST7735_HEIGHT) {
        return HAL_ERROR;
    }

    for (uint16_t i = 0; i < width; ++i) {
        line[i * 2U] = hi;
        line[i * 2U + 1U] = lo;
    }

    st7735_select();
    HAL_StatusTypeDef status = st7735_set_address_window(x, y, width, height);
    if (status == HAL_OK) {
        for (uint16_t row = 0; row < height; ++row) {
            status = st7735_write_data(line, (size_t)width * 2U);
            if (status != HAL_OK) {
                break;
            }
        }
    }
    st7735_unselect();

    return status;
}

SPI_HandleTypeDef *ST7735_GetSpiHandle(void)
{
    return &hspi_st7735;
}
