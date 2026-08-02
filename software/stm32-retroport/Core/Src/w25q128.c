#include "w25q128.h"

#include "board_pins.h"
#include "main.h"

#define W25Q_CMD_WRITE_ENABLE 0x06u
#define W25Q_CMD_READ_STATUS1 0x05u
#define W25Q_CMD_PAGE_PROGRAM 0x02u
#define W25Q_CMD_READ_DATA 0x03u
#define W25Q_CMD_SECTOR_ERASE 0x20u
#define W25Q_CMD_BLOCK64_ERASE 0xD8u
#define W25Q_CMD_JEDEC_ID 0x9Fu
#define W25Q_STATUS_BUSY 0x01u
#define W25Q_TIMEOUT_MS 1000u
#define W25Q_ERASE_TIMEOUT_MS 5000u

static SPI_HandleTypeDef hspi_flash;
static bool initialized;
static bool present;

static void cs_low(void)
{
    HAL_GPIO_WritePin(W25Q_CS_GPIO_PORT, W25Q_CS_PIN, GPIO_PIN_RESET);
}

static void cs_high(void)
{
    HAL_GPIO_WritePin(W25Q_CS_GPIO_PORT, W25Q_CS_PIN, GPIO_PIN_SET);
}

static bool tx(const uint8_t *data, uint16_t size)
{
    return HAL_SPI_Transmit(&hspi_flash, (uint8_t *)data, size, W25Q_TIMEOUT_MS) == HAL_OK;
}

static bool rx(uint8_t *data, uint16_t size)
{
    return HAL_SPI_Receive(&hspi_flash, data, size, W25Q_TIMEOUT_MS) == HAL_OK;
}

static bool address_valid(uint32_t address, size_t size)
{
    return address < W25Q128_SIZE_BYTES && size <= (W25Q128_SIZE_BYTES - address);
}

static bool read_status(uint8_t *status)
{
    uint8_t cmd = W25Q_CMD_READ_STATUS1;
    cs_low();
    bool ok = tx(&cmd, 1) && rx(status, 1);
    cs_high();
    return ok;
}

static bool wait_ready(uint32_t timeout_ms)
{
    uint32_t start = HAL_GetTick();
    uint8_t status = 0;

    do {
        if (!read_status(&status)) {
            return false;
        }
        if ((status & W25Q_STATUS_BUSY) == 0u) {
            return true;
        }
    } while ((HAL_GetTick() - start) < timeout_ms);

    return false;
}

static bool write_enable(void)
{
    uint8_t cmd = W25Q_CMD_WRITE_ENABLE;
    cs_low();
    bool ok = tx(&cmd, 1);
    cs_high();
    return ok;
}

static void put_address(uint8_t *cmd, uint32_t address)
{
    cmd[1] = (uint8_t)(address >> 16);
    cmd[2] = (uint8_t)(address >> 8);
    cmd[3] = (uint8_t)address;
}

static void w25q_enable_gpio_clock(GPIO_TypeDef *port)
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

static void w25q_enable_spi_clock(void)
{
    if (W25Q_SPI_INSTANCE == SPI1) {
        __HAL_RCC_SPI1_CLK_ENABLE();
    } else if (W25Q_SPI_INSTANCE == SPI2) {
        __HAL_RCC_SPI2_CLK_ENABLE();
    } else if (W25Q_SPI_INSTANCE == SPI3) {
        __HAL_RCC_SPI3_CLK_ENABLE();
    }
}

static void w25q_gpio_spi_init(GPIO_TypeDef *port, uint16_t pin)
{
    GPIO_InitTypeDef gpio = {0};

    w25q_enable_gpio_clock(port);

    gpio.Pin = pin;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio.Alternate = W25Q_SPI_AF;
    HAL_GPIO_Init(port, &gpio);
}

bool W25Q128_Init(void)
{
    GPIO_InitTypeDef gpio = {0};

    if (initialized) {
        return present;
    }

    w25q_enable_gpio_clock(W25Q_CS_GPIO_PORT);
    w25q_enable_spi_clock();

    gpio.Pin = W25Q_CS_PIN;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(W25Q_CS_GPIO_PORT, &gpio);
    cs_high();

    w25q_gpio_spi_init(W25Q_SCK_GPIO_PORT, W25Q_SCK_PIN);
    w25q_gpio_spi_init(W25Q_MISO_GPIO_PORT, W25Q_MISO_PIN);
    w25q_gpio_spi_init(W25Q_MOSI_GPIO_PORT, W25Q_MOSI_PIN);

    hspi_flash.Instance = W25Q_SPI_INSTANCE;
    hspi_flash.Init.Mode = SPI_MODE_MASTER;
    hspi_flash.Init.Direction = SPI_DIRECTION_2LINES;
    hspi_flash.Init.DataSize = SPI_DATASIZE_8BIT;
    hspi_flash.Init.CLKPolarity = SPI_POLARITY_LOW;
    hspi_flash.Init.CLKPhase = SPI_PHASE_1EDGE;
    hspi_flash.Init.NSS = SPI_NSS_SOFT;
    hspi_flash.Init.BaudRatePrescaler = W25Q_SPI_BAUDRATE_PRESCALER;
    hspi_flash.Init.FirstBit = SPI_FIRSTBIT_MSB;
    hspi_flash.Init.TIMode = SPI_TIMODE_DISABLE;
    hspi_flash.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
    hspi_flash.Init.CRCPolynomial = 10;

    if (HAL_SPI_Init(&hspi_flash) != HAL_OK) {
        return false;
    }

    initialized = true;
    uint32_t id = W25Q128_ReadJedecId();
    present = id != 0u && id != 0xFFFFFFu && (id & 0xFFu) == 0x18u;
    return present;
}

uint32_t W25Q128_ReadJedecId(void)
{
    uint8_t cmd = W25Q_CMD_JEDEC_ID;
    uint8_t id[3] = {0};

    cs_low();
    bool ok = tx(&cmd, 1) && rx(id, sizeof(id));
    cs_high();

    if (!ok) {
        return 0;
    }

    return ((uint32_t)id[0] << 16) | ((uint32_t)id[1] << 8) | id[2];
}

bool W25Q128_Read(uint32_t address, uint8_t *data, size_t size)
{
    uint8_t cmd[4] = {W25Q_CMD_READ_DATA, 0, 0, 0};

    if (!initialized || data == NULL || !address_valid(address, size)) {
        return false;
    }

    put_address(cmd, address);
    cs_low();
    bool ok = tx(cmd, sizeof(cmd)) && rx(data, (uint16_t)size);
    cs_high();
    return ok;
}

bool W25Q128_EraseSector(uint32_t address)
{
    uint8_t cmd[4] = {W25Q_CMD_SECTOR_ERASE, 0, 0, 0};

    if (!initialized || (address % W25Q128_SECTOR_SIZE) != 0u || !address_valid(address, W25Q128_SECTOR_SIZE)) {
        return false;
    }

    if (!wait_ready(W25Q_TIMEOUT_MS) || !write_enable()) {
        return false;
    }

    put_address(cmd, address);
    cs_low();
    bool ok = tx(cmd, sizeof(cmd));
    cs_high();
    return ok && wait_ready(W25Q_ERASE_TIMEOUT_MS);
}

bool W25Q128_EraseBlock64(uint32_t address)
{
    uint8_t cmd[4] = {W25Q_CMD_BLOCK64_ERASE, 0, 0, 0};

    if (!initialized || (address % W25Q128_BLOCK64_SIZE) != 0u || !address_valid(address, W25Q128_BLOCK64_SIZE)) {
        return false;
    }

    if (!wait_ready(W25Q_TIMEOUT_MS) || !write_enable()) {
        return false;
    }

    put_address(cmd, address);
    cs_low();
    bool ok = tx(cmd, sizeof(cmd));
    cs_high();
    return ok && wait_ready(W25Q_ERASE_TIMEOUT_MS);
}

bool W25Q128_Write(uint32_t address, const uint8_t *data, size_t size)
{
    if (!initialized || data == NULL || !address_valid(address, size)) {
        return false;
    }

    while (size > 0u) {
        uint32_t page_left = W25Q128_PAGE_SIZE - (address % W25Q128_PAGE_SIZE);
        uint16_t chunk = (size < page_left) ? (uint16_t)size : (uint16_t)page_left;
        uint8_t cmd[4] = {W25Q_CMD_PAGE_PROGRAM, 0, 0, 0};

        if (!wait_ready(W25Q_TIMEOUT_MS) || !write_enable()) {
            return false;
        }

        put_address(cmd, address);
        cs_low();
        bool ok = tx(cmd, sizeof(cmd)) && tx(data, chunk);
        cs_high();
        if (!ok || !wait_ready(W25Q_TIMEOUT_MS)) {
            return false;
        }

        address += chunk;
        data += chunk;
        size -= chunk;
    }

    return true;
}
