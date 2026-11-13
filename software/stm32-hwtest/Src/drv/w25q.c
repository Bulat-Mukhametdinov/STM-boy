#include "w25q.h"
#include "board.h"
#include "stm32f4xx_hal.h"

#define CMD_WRITE_ENABLE   0x06u
#define CMD_READ_SR1       0x05u
#define CMD_PAGE_PROGRAM   0x02u
#define CMD_READ_DATA      0x03u
#define CMD_SECTOR_ERASE   0x20u
#define CMD_JEDEC_ID       0x9Fu
#define CMD_UNIQUE_ID      0x4Bu
#define CMD_POWER_DOWN     0xB9u
#define CMD_RELEASE_PD     0xABu
#define SR1_BUSY           0x01u
#define SR1_WEL            0x02u

#define TIMEOUT_PAGE_MS    10u    /* tPP max 3 ms */
#define TIMEOUT_ERASE_MS   1000u  /* tSE max 400 ms */
#define SPI_SPIN_LIMIT     100000u

static bool s_present;
static uint16_t s_div = 4;

#define CS_LOW()  gpio_clr(FLASH_CS_PORT, FLASH_CS_PIN)
#define CS_HIGH() gpio_set(FLASH_CS_PORT, FLASH_CS_PIN)

/* Full-duplex byte exchange: every write is paired with a DR read so OVR
   never latches. Bounded spins so a dead bus cannot hang the firmware. */
static uint8_t xfer(uint8_t b)
{
    uint32_t n = 0;
    while (!(SPI3->SR & SPI_SR_TXE) && ++n < SPI_SPIN_LIMIT) {
    }
    *(volatile uint8_t *)&SPI3->DR = b;
    n = 0;
    while (!(SPI3->SR & SPI_SR_RXNE) && ++n < SPI_SPIN_LIMIT) {
    }
    return *(volatile uint8_t *)&SPI3->DR;
}

static void end(void)
{
    uint32_t n = 0;
    while ((SPI3->SR & SPI_SR_BSY) && ++n < SPI_SPIN_LIMIT) {
    }
    CS_HIGH();
}

static void spi3_setup(uint16_t div)
{
    uint32_t br = 0, d = 2;
    while (d < div && br < 7u) {
        d <<= 1;
        br++;
    }
    s_div = (uint16_t)d;
    RCC->APB1ENR |= RCC_APB1ENR_SPI3EN;
    (void)RCC->APB1ENR;
    SPI3->CR1 = 0;
    SPI3->CR2 = 0;
    SPI3->CR1 = SPI_CR1_MSTR | SPI_CR1_SSM | SPI_CR1_SSI | (br << SPI_CR1_BR_Pos); /* mode 0 */
    SPI3->CR1 |= SPI_CR1_SPE;
    (void)SPI3->DR;
    (void)SPI3->SR;
}

void w25q_set_div(uint16_t div) { spi3_setup(div); }
uint32_t w25q_spi_hz(void) { return HAL_RCC_GetPCLK1Freq() / s_div; }

bool w25q_init(void)
{
    gpio_set(FLASH_CS_PORT, FLASH_CS_PIN);
    gpio_config(FLASH_CS_PORT, FLASH_CS_PIN, GPIO_MODE_OUT_, GPIO_PULL_NONE_, GPIO_SPEED_VHIGH_, 0);
    /* PB3/PB4 default to JTAG (JTDO/NJTRST): explicitly switch to AF6. */
    gpio_config(FLASH_SCK_PORT, FLASH_SCK_PIN, GPIO_MODE_AF_, GPIO_PULL_NONE_, GPIO_SPEED_VHIGH_, 6);
    gpio_config(FLASH_MISO_PORT, FLASH_MISO_PIN, GPIO_MODE_AF_, GPIO_PULL_UP_, GPIO_SPEED_VHIGH_, 6);
    gpio_config(FLASH_MOSI_PORT, FLASH_MOSI_PIN, GPIO_MODE_AF_, GPIO_PULL_NONE_, GPIO_SPEED_VHIGH_, 6);
    spi3_setup(4); /* PCLK1 48 MHz / 4 = 12 MHz */

    (void)w25q_release_power_down();
    uint32_t id = w25q_read_jedec();
    s_present = (id != 0u) && (id != 0xFFFFFFu) && ((id & 0xFFu) == 0x18u);
    return s_present;
}

bool w25q_present(void) { return s_present; }

uint32_t w25q_read_jedec(void)
{
    CS_LOW();
    xfer(CMD_JEDEC_ID);
    uint32_t id = (uint32_t)xfer(0xFF) << 16;
    id |= (uint32_t)xfer(0xFF) << 8;
    id |= xfer(0xFF);
    end();
    return id;
}

w25q_status_t w25q_read_uid(uint8_t uid[8])
{
    CS_LOW();
    xfer(CMD_UNIQUE_ID);
    for (int i = 0; i < 4; i++) {
        xfer(0xFF); /* 4 dummy bytes */
    }
    for (int i = 0; i < 8; i++) {
        uid[i] = xfer(0xFF);
    }
    end();
    return W25Q_OK;
}

uint8_t w25q_read_sr1(void)
{
    CS_LOW();
    xfer(CMD_READ_SR1);
    uint8_t sr = xfer(0xFF);
    end();
    return sr;
}

static w25q_status_t wait_ready(uint32_t timeout_ms)
{
    uint32_t t0 = HAL_GetTick();
    for (;;) {
        if ((w25q_read_sr1() & SR1_BUSY) == 0u) {
            return W25Q_OK;
        }
        if ((uint32_t)(HAL_GetTick() - t0) > timeout_ms) {
            return W25Q_ERR_TIMEOUT;
        }
    }
}

static void write_enable(void)
{
    CS_LOW();
    xfer(CMD_WRITE_ENABLE);
    end();
}

static void send_cmd_addr(uint8_t cmd, uint32_t addr)
{
    xfer(cmd);
    xfer((uint8_t)(addr >> 16));
    xfer((uint8_t)(addr >> 8));
    xfer((uint8_t)addr);
}

static bool range_ok(uint32_t addr, size_t len)
{
    return addr < W25Q_SIZE && len <= (W25Q_SIZE - addr);
}

w25q_status_t w25q_read(uint32_t addr, void *buf, size_t len)
{
    uint8_t *p = buf;
    if (!range_ok(addr, len) || (buf == NULL && len)) {
        return W25Q_ERR_PARAM;
    }
    CS_LOW();
    send_cmd_addr(CMD_READ_DATA, addr);
    for (size_t i = 0; i < len; i++) {
        p[i] = xfer(0xFF);
    }
    end();
    return W25Q_OK;
}

w25q_status_t w25q_program(uint32_t addr, const void *buf, size_t len)
{
    const uint8_t *p = buf;
    if (!range_ok(addr, len) || (buf == NULL && len)) {
        return W25Q_ERR_PARAM;
    }
    while (len) {
        uint32_t room = W25Q_PAGE_SIZE - (addr % W25Q_PAGE_SIZE);
        uint32_t chunk = (len < room) ? (uint32_t)len : room;
        w25q_status_t st = wait_ready(TIMEOUT_PAGE_MS);
        if (st != W25Q_OK) {
            return st;
        }
        write_enable();
        CS_LOW();
        send_cmd_addr(CMD_PAGE_PROGRAM, addr);
        for (uint32_t i = 0; i < chunk; i++) {
            xfer(p[i]);
        }
        end();
        st = wait_ready(TIMEOUT_PAGE_MS);
        if (st != W25Q_OK) {
            return st;
        }
        addr += chunk;
        p += chunk;
        len -= chunk;
    }
    return W25Q_OK;
}

w25q_status_t w25q_erase_sector(uint32_t addr)
{
    if ((addr % W25Q_SECTOR_SIZE) != 0u || !range_ok(addr, W25Q_SECTOR_SIZE)) {
        return W25Q_ERR_PARAM;
    }
    w25q_status_t st = wait_ready(TIMEOUT_PAGE_MS);
    if (st != W25Q_OK) {
        return st;
    }
    write_enable();
    CS_LOW();
    send_cmd_addr(CMD_SECTOR_ERASE, addr);
    end();
    return wait_ready(TIMEOUT_ERASE_MS);
}

w25q_status_t w25q_power_down(void)
{
    CS_LOW();
    xfer(CMD_POWER_DOWN);
    end();
    return W25Q_OK;
}

w25q_status_t w25q_release_power_down(void)
{
    CS_LOW();
    xfer(CMD_RELEASE_PD);
    end();
    /* tRES1 = 3 us */
    for (volatile uint32_t i = 0; i < 500u; i++) {
    }
    return W25Q_OK;
}

const char *w25q_status_str(w25q_status_t s)
{
    switch (s) {
    case W25Q_OK: return "ok";
    case W25Q_ERR_PARAM: return "param";
    case W25Q_ERR_TIMEOUT: return "timeout";
    case W25Q_ERR_NOT_PRESENT: return "not_present";
    default: return "?";
    }
}
